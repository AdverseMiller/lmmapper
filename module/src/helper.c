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

#include "helper.h"

#define HELPER_TEST_REGION_SIZE 64

static u8 helper_test_region[HELPER_TEST_REGION_SIZE] = "helper controlled kernel read/write test region";

static bool helper_access_allowed(void) {
    return uid_eq(current_euid(), GLOBAL_ROOT_UID) && ns_capable(&init_user_ns, CAP_SYS_RAWIO);
}

static int helper_open(struct inode *inode, struct file *file) {
    if (!helper_access_allowed()) return -EPERM;
    return nonseekable_open(inode, file);
}

static int helper_validate_request(const struct helper_request *request) {
    unsigned long address;

    if (!request->size || request->size > HELPER_MAX_TRANSFER) return -EINVAL;
    if ((u64)(unsigned long)request->address != request->address) return -EOVERFLOW;
    if ((u64)(unsigned long)request->buffer != request->buffer) return -EOVERFLOW;
    if (request->address + request->size < request->address) return -EOVERFLOW;
    address = (unsigned long)request->address;
    if (access_ok((const void __user *)address, request->size)) return -EINVAL;
    return 0;
}

static long helper_copy_to_kernel_nofault(void *destination, const void *source, size_t size) {
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

static long helper_read(const struct helper_request *request) {
    void *buffer;
    long result;

    result = helper_validate_request(request);
    if (result) return result;
    buffer = kvmalloc(request->size, GFP_KERNEL);
    if (!buffer) return -ENOMEM;
    result = copy_from_kernel_nofault(buffer, (const void *)(unsigned long)request->address, request->size);
    if (!result && copy_to_user(u64_to_user_ptr(request->buffer), buffer, request->size)) result = -EFAULT;
    kvfree(buffer);
    return result;
}

static long helper_write(const struct helper_request *request) {
    void *buffer;
    long result;

    result = helper_validate_request(request);
    if (result) return result;
    buffer = kvmalloc(request->size, GFP_KERNEL);
    if (!buffer) return -ENOMEM;
    if (copy_from_user(buffer, u64_to_user_ptr(request->buffer), request->size)) {
        kvfree(buffer);
        return -EFAULT;
    }
    result = helper_copy_to_kernel_nofault((void *)(unsigned long)request->address, buffer, request->size);
    kvfree(buffer);
    return result;
}

static long helper_ioctl(struct file *file, unsigned int command, unsigned long argument) {
    struct helper_request request;
    struct helper_info info;

    (void)file;
    if (!helper_access_allowed()) return -EPERM;
    switch (command) {
    case HELPER_IOCTL_READ:
        if (copy_from_user(&request, (const void __user *)argument, sizeof(request))) return -EFAULT;
        return helper_read(&request);
    case HELPER_IOCTL_WRITE:
        if (copy_from_user(&request, (const void __user *)argument, sizeof(request))) return -EFAULT;
        return helper_write(&request);
    case HELPER_IOCTL_GET_INFO:
        info.api_version = HELPER_API_VERSION;
        info.max_transfer = HELPER_MAX_TRANSFER;
        info.test_address = (unsigned long)helper_test_region;
        info.test_size = sizeof(helper_test_region);
        if (copy_to_user((void __user *)argument, &info, sizeof(info))) return -EFAULT;
        return 0;
    default:
        return -ENOTTY;
    }
}

static const struct file_operations helper_file_operations = {
    .owner = THIS_MODULE,
    .open = helper_open,
    .unlocked_ioctl = helper_ioctl,
#ifdef CONFIG_COMPAT
    .compat_ioctl = helper_ioctl,
#endif
};

static struct miscdevice helper_device = {
    .minor = MISC_DYNAMIC_MINOR,
    .name = HELPER_DEVICE_NAME,
    .fops = &helper_file_operations,
    .mode = 0600,
};

static int __init helper_init(void) {
    int result = misc_register(&helper_device);

    if (result) return result;
    pr_info("helper: registered /dev/%s\n", HELPER_DEVICE_NAME);
    return 0;
}

static void __exit helper_exit(void) {
    misc_deregister(&helper_device);
    pr_info("helper: unloaded\n");
}

module_init(helper_init);
module_exit(helper_exit);

MODULE_DESCRIPTION("Root-only kernel virtual memory read/write interface");
MODULE_LICENSE("GPL");
