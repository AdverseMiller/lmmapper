#ifndef LMMAPPER_H
#define LMMAPPER_H

#include <linux/ioctl.h>
#include <linux/types.h>

#define LMMAPPER_DEVICE_NAME "lmmapper"
#define LMMAPPER_API_VERSION 1
#define LMMAPPER_MAX_TRANSFER (1024ULL * 1024ULL)
#define LMMAPPER_IOCTL_MAGIC 0xb7

struct lmmapper_request {
    __u64 address;
    __u64 buffer;
    __u64 size;
};

struct lmmapper_info {
    __u64 api_version;
    __u64 max_transfer;
    __u64 test_address;
    __u64 test_size;
};

#define LMMAPPER_IOCTL_READ _IOW(LMMAPPER_IOCTL_MAGIC, 0x01, struct lmmapper_request)
#define LMMAPPER_IOCTL_WRITE _IOW(LMMAPPER_IOCTL_MAGIC, 0x02, struct lmmapper_request)
#define LMMAPPER_IOCTL_GET_INFO _IOR(LMMAPPER_IOCTL_MAGIC, 0x03, struct lmmapper_info)

#endif
