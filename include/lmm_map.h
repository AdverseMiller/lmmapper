#ifndef LMM_MAP_H
#define LMM_MAP_H

#include "lmm_elf.h"
#include "lmm_reloc.h"

#include <stddef.h>
#include <stdint.h>

#define LMM_MAP_MAX_IMAGE (16ULL * 1024ULL * 1024ULL)
#define LMM_MAP_TRANSFER_CHUNK (1024ULL * 1024ULL)

struct lmm_map_ops {
    int (*allocate)(void *state, size_t size, uint64_t *address);
    int (*upload)(void *state, uint64_t offset, const void *source, size_t size);
    int (*read)(void *state, uint64_t address, void *destination, size_t size);
    int (*seal)(void *state, size_t executable_size);
    int (*call)(void *state, uint64_t offset, uint64_t argument1, uint64_t argument2, int64_t *result);
    int (*release)(void *state);
    void (*close)(void *state);
};

struct lmm_map_backend {
    const struct lmm_map_ops *ops;
    void *state;
};

/* Once entry invocation is attempted, the caller owns the allocation even if the call reports an error; the entry may have registered callbacks. Earlier failures release it. */
int lmm_map_image(const struct lmm_elf *image, const char *entry, uint64_t argument1, uint64_t argument2, const struct lmm_map_backend *backend, lmm_symbol_resolver resolver, void *resolver_context, uint64_t *mapped_address, int64_t *call_result, struct lmm_reloc_report *report);

#endif
