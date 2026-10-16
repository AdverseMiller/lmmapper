#include <linux/capability.h>
#include <linux/cred.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/uidgid.h>

#include "lmmapper.h"

#define LMMAPPER_TEST_REGION_SIZE 64

static u8 lmmapper_test_region[LMMAPPER_TEST_REGION_SIZE] = "lmmapper controlled kernel read/write test region";

static bool lmmapper_access_allowed(void) {
    return uid_eq(current_euid(), GLOBAL_ROOT_UID) && ns_capable(&init_user_ns, CAP_SYS_RAWIO);
}

static int lmmapper_open(struct inode *inode, struct file *file) {
    if (!lmmapper_access_allowed()) return -EPERM;
    return nonseekable_open(inode, file);
}

static int lmmapper_validate_request(const struct lmmapper_request *request) {
    unsigned long address;

    if (!request->size || request->size > LMMAPPER_MAX_TRANSFER) return -EINVAL;
    if ((u64)(unsigned long)request->address != request->address) return -EOVERFLOW;
    if ((u64)(unsigned long)request->buffer != request->buffer) return -EOVERFLOW;
    if (request->address + request->size < request->address) return -EOVERFLOW;
    address = (unsigned long)request->address;
    if (access_ok((const void __user *)address, request->size)) return -EINVAL;
    return 0;
}

static long lmmapper_copy_to_kernel_nofault(void *destination, const void *source, size_t size) {
    u8 *destination_bytes = destination;
    const u8 *source_bytes = source;

    pagefault_disable();
    while (size) {
        __put_kernel_nofault(destination_bytes, source_bytes, u8, fault);
        destination_bytes++;
        source_bytes++;
        size--;
    }
    pagefault_enable();
    return 0;

fault:
    pagefault_enable();
    return -EFAULT;
}

static long lmmapper_read(const struct lmmapper_request *request) {
    void *buffer;
    long result;

    result = lmmapper_validate_request(request);
    if (result) return result;
    buffer = kvmalloc(request->size, GFP_KERNEL);
    if (!buffer) return -ENOMEM;
    result = copy_from_kernel_nofault(buffer, (const void *)(unsigned long)request->address, request->size);
    if (!result && copy_to_user(u64_to_user_ptr(request->buffer), buffer, request->size)) result = -EFAULT;
    kvfree(buffer);
    return result;
}

static long lmmapper_write(const struct lmmapper_request *request) {
    void *buffer;
    long result;

    result = lmmapper_validate_request(request);
    if (result) return result;
    buffer = kvmalloc(request->size, GFP_KERNEL);
    if (!buffer) return -ENOMEM;
    if (copy_from_user(buffer, u64_to_user_ptr(request->buffer), request->size)) {
        kvfree(buffer);
        return -EFAULT;
    }
    result = lmmapper_copy_to_kernel_nofault((void *)(unsigned long)request->address, buffer, request->size);
    kvfree(buffer);
    return result;
}

static long lmmapper_ioctl(struct file *file, unsigned int command, unsigned long argument) {
    struct lmmapper_request request;
    struct lmmapper_info info;

    (void)file;
    if (!lmmapper_access_allowed()) return -EPERM;
    switch (command) {
    case LMMAPPER_IOCTL_READ:
        if (copy_from_user(&request, (const void __user *)argument, sizeof(request))) return -EFAULT;
        return lmmapper_read(&request);
    case LMMAPPER_IOCTL_WRITE:
        if (copy_from_user(&request, (const void __user *)argument, sizeof(request))) return -EFAULT;
        return lmmapper_write(&request);
    case LMMAPPER_IOCTL_GET_INFO:
        info.api_version = LMMAPPER_API_VERSION;
        info.max_transfer = LMMAPPER_MAX_TRANSFER;
        info.test_address = (unsigned long)lmmapper_test_region;
        info.test_size = sizeof(lmmapper_test_region);
        if (copy_to_user((void __user *)argument, &info, sizeof(info))) return -EFAULT;
        return 0;
    default:
        return -ENOTTY;
    }
}

static const struct file_operations lmmapper_file_operations = {
    .owner = THIS_MODULE,
    .open = lmmapper_open,
    .unlocked_ioctl = lmmapper_ioctl,
#ifdef CONFIG_COMPAT
    .compat_ioctl = lmmapper_ioctl,
#endif
};

static struct miscdevice lmmapper_device = {
    .minor = MISC_DYNAMIC_MINOR,
    .name = LMMAPPER_DEVICE_NAME,
    .fops = &lmmapper_file_operations,
    .mode = 0600,
};

static int __init lmmapper_init(void) {
    int result = misc_register(&lmmapper_device);

    if (result) return result;
    pr_info("lmmapper: registered /dev/%s\n", LMMAPPER_DEVICE_NAME);
    return 0;
}

static void __exit lmmapper_exit(void) {
    misc_deregister(&lmmapper_device);
    pr_info("lmmapper: unloaded\n");
}

module_init(lmmapper_init);
module_exit(lmmapper_exit);

MODULE_DESCRIPTION("Root-only kernel virtual memory read/write interface");
MODULE_LICENSE("GPL");
