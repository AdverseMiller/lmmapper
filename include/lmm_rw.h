#ifndef LMM_RW_H
#define LMM_RW_H

#include <stddef.h>
#include <stdint.h>

/* The mapper owns execution and allocation. An RW source supplies only kernel virtual-memory transfers and a writable scratch region it is safe to borrow temporarily. */
struct lmm_rw_ops {
    int (*read)(void *state, uint64_t address, void *destination, size_t size);
    int (*write)(void *state, uint64_t address, const void *source, size_t size);
    void (*close)(void *state);
};

struct lmm_rw {
    const struct lmm_rw_ops *ops;
    void *state;
    uint64_t scratch_address;
    size_t scratch_size;
    size_t max_transfer;
};

int lmm_rw_bind(struct lmm_rw *rw, const struct lmm_rw_ops *ops, void *state, uint64_t scratch_address, size_t scratch_size, size_t max_transfer);
int lmm_rw_read(const struct lmm_rw *rw, uint64_t address, void *destination, size_t size);
int lmm_rw_write(const struct lmm_rw *rw, uint64_t address, const void *source, size_t size);
void lmm_rw_close(struct lmm_rw *rw);

/* Example adapter backed by the module/ RW-only test device. */
int lmm_rw_open_lmmapper(struct lmm_rw *rw);

#endif
