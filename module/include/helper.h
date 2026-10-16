#ifndef HELPER_H
#define HELPER_H

#include <linux/ioctl.h>
#include <linux/types.h>

#define HELPER_DEVICE_NAME "helper"
#define HELPER_API_VERSION 1
#define HELPER_MAX_TRANSFER (1024ULL * 1024ULL)
#define HELPER_IOCTL_MAGIC 0xb7

struct helper_request {
    __u64 address;
    __u64 buffer;
    __u64 size;
};

struct helper_info {
    __u64 api_version;
    __u64 max_transfer;
    __u64 test_address;
    __u64 test_size;
};

#define HELPER_IOCTL_READ _IOW(HELPER_IOCTL_MAGIC, 0x01, struct helper_request)
#define HELPER_IOCTL_WRITE _IOW(HELPER_IOCTL_MAGIC, 0x02, struct helper_request)
#define HELPER_IOCTL_GET_INFO _IOR(HELPER_IOCTL_MAGIC, 0x03, struct helper_info)

#endif
