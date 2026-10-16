#ifndef LMM_LIVE_INTERNAL_H
#define LMM_LIVE_INTERNAL_H

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "lmm_live.h"
#include "lmm_kernel_layout.h"

#include <sched.h>
#include <signal.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define LMM_LIVE_PAGE_SIZE 4096
#define LMM_LIVE_SCRATCH_SIZE 64
#define LMM_LIVE_BLOCK_MAX 256

enum lmm_live_symbol {
    LMM_SYM_RESTART,
    LMM_SYM_TOP,
    LMM_SYM_PAGE_OFFSET,
    LMM_SYM_PERCPU_OFFSET,
    LMM_SYM_CURRENT_TASK,
    LMM_SYM_FLUSH_TLB,
    LMM_SYM_EXECMEM_ALLOC,
    LMM_SYM_EXECMEM_SEAL,
    LMM_SYM_EXECMEM_FREE,
    LMM_SYM_PGDIR_SHIFT,
    LMM_SYM_COUNT,
};

enum lmm_live_operation {
    LMM_LIVE_ALLOCATE,
    LMM_LIVE_TWO_ARGUMENTS,
    LMM_LIVE_FREE,
};

struct lmm_live {
    struct lmm_rw rw;
    uint64_t symbols[LMM_SYM_COUNT];
    uint64_t scratch_address;
    uint64_t pte_address;
    uint64_t original_pte;
    uint64_t direct_map;
    uint64_t physical_mask;
    uint64_t block_address;
    struct lmm_kernel_layout layout;
    unsigned pgdir_shift;
    uint64_t base;
    size_t size;
    size_t executable_size;
    unsigned char original_buffer[LMM_LIVE_SCRATCH_SIZE];
    unsigned char original_block[LMM_LIVE_BLOCK_MAX];
    cpu_set_t affinity;
    sigset_t signals;
    bool pinned;
    bool signals_blocked;
    bool buffer_saved;
    bool block_saved;
    bool pte_changed;
};

extern const struct lmm_map_ops lmm_live_map_ops;

int lmm_live_profile(struct lmm_live *live);
int lmm_live_running_build_id(unsigned char id[20]);
int lmm_live_image_symbols(const char *path, const char *const names[LMM_SYM_COUNT], uint64_t values[LMM_SYM_COUNT], unsigned char id[20]);
int lmm_live_read_layout(const char *path, struct lmm_kernel_layout *layout);
int lmm_live_read(struct lmm_live *live, uint64_t address, void *destination, size_t size);
int lmm_live_write(struct lmm_live *live, uint64_t address, const void *source, size_t size);
int lmm_live_write_checked(struct lmm_live *live, uint64_t address, const void *source, size_t size);
int lmm_live_read_word(struct lmm_live *live, uint64_t address, uint64_t *value);
int lmm_live_permissions(struct lmm_live *live, uint64_t address, bool *executable, bool *writable, uint64_t *pte_address, uint64_t *pte_value);
int lmm_live_invoke(struct lmm_live *live, uint64_t target, uint64_t first, uint64_t second, int64_t *result);
int lmm_live_bridge_call(struct lmm_live *live, uint64_t target, enum lmm_live_operation operation, uint64_t first, uint64_t second, int64_t *result);

#endif
