#define _GNU_SOURCE

#include "lmm_live.h"
#include "lmm_rw.h"
#include "lmmapper.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <unistd.h>

struct lmmapper_device {
    int fd;
};

static int transfer(void *state, unsigned long command, uint64_t address, void *buffer, size_t size) {
    struct lmmapper_device *device = state;
    struct lmmapper_request request = {.address = address, .buffer = (uint64_t)(uintptr_t)buffer, .size = size};

    if (ioctl(device->fd, command, &request)) return -errno;
    return 0;
}

static int read_kernel(void *state, uint64_t address, void *destination, size_t size) {
    return transfer(state, LMMAPPER_IOCTL_READ, address, destination, size);
}

static int write_kernel(void *state, uint64_t address, const void *source, size_t size) {
    return transfer(state, LMMAPPER_IOCTL_WRITE, address, (void *)(uintptr_t)source, size);
}

static void close_device(void *state) {
    struct lmmapper_device *device = state;

    if (!device) return;
    close(device->fd);
    free(device);
}

static const struct lmm_rw_ops lmmapper_ops = {
    .read = read_kernel,
    .write = write_kernel,
    .close = close_device,
};

int lmm_rw_open_lmmapper(struct lmm_rw *rw) {
    struct lmmapper_device *device;
    struct lmmapper_info info;
    int result;

    if (!rw) return -EINVAL;
    *rw = (struct lmm_rw) {0};
    device = calloc(1, sizeof(*device));
    if (!device) return -ENOMEM;
    device->fd = open("/dev/" LMMAPPER_DEVICE_NAME, O_RDWR | O_CLOEXEC);
    if (device->fd < 0) {
        result = -errno;
        free(device);
        return result;
    }
    if (ioctl(device->fd, LMMAPPER_IOCTL_GET_INFO, &info)) {
        result = -errno;
        close_device(device);
        return result;
    }
    if (info.api_version != LMMAPPER_API_VERSION || !info.test_address || !info.test_size || !info.max_transfer || info.max_transfer > LMMAPPER_MAX_TRANSFER || info.test_size > SIZE_MAX || info.max_transfer > SIZE_MAX) {
        close_device(device);
        return -EPROTO;
    }
    result = lmm_rw_bind(rw, &lmmapper_ops, device, info.test_address, (size_t)info.test_size, (size_t)info.max_transfer);
    if (result) close_device(device);
    return result;
}

int lmm_live_open(struct lmm_live **out, struct lmm_map_backend *backend) {
    struct lmm_rw rw = {0};
    int result;

    if (!out || !backend) return -EINVAL;
    result = lmm_rw_open_lmmapper(&rw);
    if (result) return result;
    return lmm_live_open_with_rw(&rw, out, backend);
}
