#include "live_internal.h"

#include <errno.h>

static bool is_kernel_page(uint64_t address) {
    return address >= UINT64_C(0xffff800000000000) && !(address & (LMM_LIVE_PAGE_SIZE - 1));
}

static int live_release(void *context) {
    struct lmm_live *live = context;
    int64_t ignored;
    int result;

    if (!live || !live->base) return -EINVAL;
    result = lmm_live_bridge_call(live, live->symbols[LMM_SYM_EXECMEM_FREE], LMM_LIVE_FREE, live->base, 0, &ignored);
    if (result) return result;
    live->base = 0;
    live->size = 0;
    live->executable_size = 0;
    return 0;
}

static int live_allocate(void *context, size_t size, uint64_t *address) {
    struct lmm_live *live = context;
    int64_t answer;
    uint64_t base;
    bool executable;
    bool writable;
    int result;

    if (!live || !address || !size || size > LMM_MAP_MAX_IMAGE) return -EINVAL;
    if (live->base) return -EBUSY;
    result = lmm_live_bridge_call(live, live->symbols[LMM_SYM_EXECMEM_ALLOC], LMM_LIVE_ALLOCATE, size, 0, &answer);
    if (result) return result;
    base = (uint64_t)answer;
    if (!base) return -ENOMEM;
    if (!is_kernel_page(base)) return -EFAULT;
    live->base = base;
    live->size = size;
    if (base > UINT64_MAX - size) result = -EOVERFLOW;
    else result = 0;
    for (size_t offset = 0; !result && offset < size; offset += LMM_LIVE_PAGE_SIZE) {
        result = lmm_live_permissions(live, base + offset, &executable, &writable, NULL, NULL);
        if (!result && (executable || !writable)) result = -EOPNOTSUPP;
    }
    if (result) {
        int release_result = live_release(live);
        if (release_result) return release_result;
        return result;
    }
    *address = base;
    return 0;
}

static int live_upload(void *context, uint64_t offset, const void *source, size_t size) {
    struct lmm_live *live = context;

    if (!live || !live->base || !source || !size || offset > live->size || size > live->size - offset) return -EINVAL;
    return lmm_live_write(live, live->base + offset, source, size);
}

static int live_read(void *context, uint64_t address, void *destination, size_t size) {
    struct lmm_live *live = context;

    if (!live || !live->base || !destination || !size || address < live->base || address - live->base > live->size || size > live->size - (address - live->base)) return -EINVAL;
    return lmm_live_read(live, address, destination, size);
}

static int live_seal(void *context, size_t executable_size) {
    struct lmm_live *live = context;
    int64_t answer;
    bool executable;
    bool writable;
    int result;

    if (!live || !live->base || !executable_size || executable_size > live->size || executable_size & (LMM_LIVE_PAGE_SIZE - 1)) return -EINVAL;
    result = lmm_live_bridge_call(live, live->symbols[LMM_SYM_EXECMEM_SEAL], LMM_LIVE_TWO_ARGUMENTS, live->base, executable_size, &answer);
    if (result) return result;
    if (answer) return answer < 0 && answer >= -4095 ? (int)answer : -EIO;
    for (size_t offset = 0; offset < live->size; offset += LMM_LIVE_PAGE_SIZE) {
        result = lmm_live_permissions(live, live->base + offset, &executable, &writable, NULL, NULL);
        if (result) return result;
        if (offset < executable_size && (!executable || writable)) return -EACCES;
        if (offset >= executable_size && (executable || !writable)) return -EACCES;
    }
    live->executable_size = executable_size;
    return 0;
}

static int live_call(void *context, uint64_t offset, uint64_t argument1, uint64_t argument2, int64_t *answer) {
    struct lmm_live *live = context;

    if (!live || !live->base || !live->executable_size || offset >= live->executable_size || !answer) return -EINVAL;
    return lmm_live_bridge_call(live, live->base + offset, LMM_LIVE_TWO_ARGUMENTS, argument1, argument2, answer);
}

const struct lmm_map_ops lmm_live_map_ops = {
    .allocate = live_allocate,
    .upload = live_upload,
    .read = live_read,
    .seal = live_seal,
    .call = live_call,
    .release = live_release,
};

int lmm_live_release(struct lmm_live *live) {
    return live_release(live);
}
