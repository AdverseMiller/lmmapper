#include "lmm_rw.h"

#include <errno.h>
#include <string.h>

int lmm_rw_bind(struct lmm_rw *rw, const struct lmm_rw_ops *ops, void *state, uint64_t scratch_address, size_t scratch_size, size_t max_transfer) {
    if (!rw || !ops || !ops->read || !ops->write || !scratch_address || !scratch_size || !max_transfer) return -EINVAL;
    if (scratch_address > UINT64_MAX - (scratch_size - 1)) return -EOVERFLOW;
    *rw = (struct lmm_rw) {.ops = ops, .state = state, .scratch_address = scratch_address, .scratch_size = scratch_size, .max_transfer = max_transfer};
    return 0;
}

static int valid_transfer(const struct lmm_rw *rw, uint64_t address, const void *buffer, size_t size) {
    if (!rw || !rw->ops || !rw->ops->read || !rw->ops->write || !rw->max_transfer || !buffer || !size) return -EINVAL;
    if (address > UINT64_MAX - (size - 1)) return -EOVERFLOW;
    return 0;
}

int lmm_rw_read(const struct lmm_rw *rw, uint64_t address, void *destination, size_t size) {
    unsigned char *bytes = destination;
    int result = valid_transfer(rw, address, destination, size);

    if (result) return result;
    for (size_t offset = 0; offset < size;) {
        size_t chunk = size - offset;

        if (chunk > rw->max_transfer) chunk = rw->max_transfer;
        result = rw->ops->read(rw->state, address + offset, bytes + offset, chunk);
        if (result) return result;
        offset += chunk;
    }
    return 0;
}

int lmm_rw_write(const struct lmm_rw *rw, uint64_t address, const void *source, size_t size) {
    const unsigned char *bytes = source;
    int result = valid_transfer(rw, address, source, size);

    if (result) return result;
    for (size_t offset = 0; offset < size;) {
        size_t chunk = size - offset;

        if (chunk > rw->max_transfer) chunk = rw->max_transfer;
        result = rw->ops->write(rw->state, address + offset, bytes + offset, chunk);
        if (result) return result;
        offset += chunk;
    }
    return 0;
}

void lmm_rw_close(struct lmm_rw *rw) {
    if (!rw) return;
    if (rw->ops && rw->ops->close) rw->ops->close(rw->state);
    memset(rw, 0, sizeof(*rw));
}
