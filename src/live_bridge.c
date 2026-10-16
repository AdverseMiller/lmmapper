#define _GNU_SOURCE

#include "live_internal.h"

#include <cpuid.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <unistd.h>

#define LMM_PRESENT UINT64_C(1)
#define LMM_WRITABLE UINT64_C(2)
#define LMM_LARGE UINT64_C(0x80)
#define LMM_NX UINT64_C(0x8000000000000000)

int lmm_live_read(struct lmm_live *live, uint64_t address, void *destination, size_t size) {
    if (!live) return -EINVAL;
    return lmm_rw_read(&live->rw, address, destination, size);
}

int lmm_live_write(struct lmm_live *live, uint64_t address, const void *source, size_t size) {
    if (!live) return -EINVAL;
    return lmm_rw_write(&live->rw, address, source, size);
}

int lmm_live_write_checked(struct lmm_live *live, uint64_t address, const void *source, size_t size) {
    unsigned char observed[LMM_LIVE_BLOCK_MAX];
    int result;

    if (size > sizeof(observed)) return -EMSGSIZE;
    result = lmm_live_write(live, address, source, size);
    if (result) return result;
    result = lmm_live_read(live, address, observed, size);
    if (result) return result;
    return memcmp(observed, source, size) ? -EIO : 0;
}

int lmm_live_read_word(struct lmm_live *live, uint64_t address, uint64_t *value) {
    if (!value) return -EINVAL;
    return lmm_live_read(live, address, value, sizeof(*value));
}

int lmm_live_permissions(struct lmm_live *live, uint64_t address, bool *executable, bool *writable, uint64_t *pte_address, uint64_t *pte_value) {
    unsigned shifts[5] = {48, 39, 30, 21, 12};
    size_t first;
    uint64_t table;
    bool x = true;
    bool w = true;

    if (!live || !executable || !writable) return -EINVAL;
    first = live->pgdir_shift == 48 ? 0 : 1;
    table = live->symbols[LMM_SYM_TOP];
    for (size_t i = first; i < 5; i++) {
        uint64_t location = table + ((address >> shifts[i] & 0x1ff) * 8);
        uint64_t entry;
        int result = lmm_live_read_word(live, location, &entry);

        if (result) return result;
        if (!(entry & LMM_PRESENT)) return -EFAULT;
        if (entry & LMM_NX) x = false;
        if (!(entry & LMM_WRITABLE)) w = false;
        if (i == 4) {
            if (pte_address) *pte_address = location;
            if (pte_value) *pte_value = entry;
            *executable = x;
            *writable = w;
            return 0;
        }
        if (i == 2 || i == 3) {
            if (entry & LMM_LARGE) return -EOPNOTSUPP;
        }
        table = live->direct_map + (entry & live->physical_mask);
    }
    return -EFAULT;
}

int lmm_live_invoke(struct lmm_live *live, uint64_t target, uint64_t first, uint64_t second, int64_t *result) {
    unsigned char changed[LMM_LIVE_BLOCK_MAX];
    long observed;
    int write_result;
    int restore_result;

    if (!live || !live->block_saved || !result) return -EINVAL;
    memcpy(changed, live->original_block, live->layout.restart_size);
    memcpy(changed + live->layout.first_argument_offset, &first, 8);
    memcpy(changed + live->layout.callback_offset, &target, 8);
    memcpy(changed + live->layout.second_argument_offset, &second, 8);
    write_result = lmm_live_write_checked(live, live->block_address, changed, live->layout.restart_size);
    if (write_result) return write_result;
    errno = 0;
    observed = syscall(SYS_restart_syscall);
    *result = observed == -1 ? -errno : observed;
    restore_result = lmm_live_write_checked(live, live->block_address, live->original_block, live->layout.restart_size);
    if (restore_result) return restore_result;
    return 0;
}

static void emit_load(unsigned char *code, size_t *length, unsigned char destination, uint32_t displacement) {
    code[(*length)++] = 0x48;
    code[(*length)++] = 0x8b;
    code[(*length)++] = destination;
    memcpy(code + *length, &displacement, sizeof(displacement));
    *length += sizeof(displacement);
}

int lmm_live_bridge_call(struct lmm_live *live, uint64_t target, enum lmm_live_operation operation, uint64_t first, uint64_t second, int64_t *result) {
    unsigned char code[LMM_LIVE_SCRATCH_SIZE] = {0};
    size_t length = 0;
    static const unsigned char epilogue[] = {0xff, 0xd0, 0x48, 0x83, 0xc4, 0x08, 0xc3};
    int write_result;

    if (!live || !live->pte_changed || !result) return -EINVAL;
    code[length++] = 0x48;
    code[length++] = 0x83;
    code[length++] = 0xec;
    code[length++] = 0x08;
    if (operation == LMM_LIVE_ALLOCATE) {
        emit_load(code, &length, 0xb7, live->layout.first_argument_offset);
        code[length++] = 0xbf;
        memcpy(code + length, &live->layout.execmem_default, sizeof(live->layout.execmem_default));
        length += sizeof(live->layout.execmem_default);
    } else if (operation == LMM_LIVE_TWO_ARGUMENTS) {
        emit_load(code, &length, 0xb7, live->layout.second_argument_offset);
        emit_load(code, &length, 0xbf, live->layout.first_argument_offset);
    } else if (operation == LMM_LIVE_FREE) {
        emit_load(code, &length, 0xbf, live->layout.first_argument_offset);
    } else {
        return -EINVAL;
    }
    code[length++] = 0x48;
    code[length++] = 0xb8;
    memcpy(code + length, &target, sizeof(target));
    length += sizeof(target);
    memcpy(code + length, epilogue, sizeof(epilogue));
    length += sizeof(epilogue);
    if (length > live->rw.scratch_size) return -EOVERFLOW;
    write_result = lmm_live_write_checked(live, live->scratch_address, code, length);
    if (write_result) return write_result;
    return lmm_live_invoke(live, live->scratch_address, first, second, result);
}

static int physical_mask(uint64_t *mask) {
    unsigned eax;
    unsigned ebx;
    unsigned ecx;
    unsigned edx;
    unsigned bits;

    if (__get_cpuid_max(0x80000000, NULL) < 0x80000008) return -EOPNOTSUPP;
    __cpuid(0x80000008, eax, ebx, ecx, edx);
    bits = eax & 0xff;
    if (bits < 36 || bits > 52) return -EOPNOTSUPP;
    *mask = ((UINT64_C(1) << bits) - 1) & ~(UINT64_C(4096) - 1);
    return 0;
}

static int pin_task(struct lmm_live *live) {
    cpu_set_t selected;
    sigset_t blocked;
    int cpu = -1;
    int result;

    if (sched_getaffinity(0, sizeof(live->affinity), &live->affinity)) return -errno;
    for (int i = 0; i < CPU_SETSIZE; i++) {
        if (CPU_ISSET(i, &live->affinity)) {
            cpu = i;
            break;
        }
    }
    if (cpu < 0) return -EINVAL;
    CPU_ZERO(&selected);
    CPU_SET(cpu, &selected);
    if (sched_setaffinity(0, sizeof(selected), &selected)) return -errno;
    live->pinned = true;
    sigfillset(&blocked);
    sigdelset(&blocked, SIGSEGV);
    sigdelset(&blocked, SIGBUS);
    sigdelset(&blocked, SIGILL);
    sigdelset(&blocked, SIGFPE);
    sigdelset(&blocked, SIGTRAP);
    result = sigprocmask(SIG_BLOCK, &blocked, &live->signals);
    if (result) return -errno;
    live->signals_blocked = true;
    return 0;
}

static int find_current_task(struct lmm_live *live) {
    uint64_t per_cpu_base;
    uint64_t task;
    uint64_t task_slot;
    uint32_t pid;
    int cpu = sched_getcpu();
    int result;

    if (cpu < 0) return -errno;
    result = lmm_live_read_word(live, live->symbols[LMM_SYM_PERCPU_OFFSET] + (uint64_t)cpu * 8, &per_cpu_base);
    if (result) return result;
    task_slot = live->symbols[LMM_SYM_CURRENT_TASK] + per_cpu_base;
    result = lmm_live_read_word(live, task_slot, &task);
    if (result) return result;
    result = lmm_live_read(live, task + live->layout.task_pid_offset, &pid, sizeof(pid));
    if (result) return result;
    if (pid != (uint32_t)getpid()) return -ESTALE;
    live->block_address = task + live->layout.task_restart_offset;
    result = lmm_live_read(live, live->block_address, live->original_block, live->layout.restart_size);
    if (result) return result;
    live->block_saved = true;
    uint64_t callback;
    memcpy(&callback, live->original_block + live->layout.callback_offset, 8);
    if (callback != live->symbols[LMM_SYM_RESTART]) return -EBUSY;
    return 0;
}

int lmm_live_close(struct lmm_live *live) {
    int result = 0;
    int next;

    if (!live) return 0;
    if (live->pte_changed) {
        next = lmm_live_write_checked(live, live->pte_address, &live->original_pte, 8);
        if (next && !result) result = next;
        if (!next && live->block_saved) {
            int64_t ignored;
            next = lmm_live_invoke(live, live->symbols[LMM_SYM_FLUSH_TLB], 0, 0, &ignored);
            if (next && !result) result = next;
        }
        if (!next) live->pte_changed = false;
    }
    if (live->buffer_saved) {
        next = lmm_live_write_checked(live, live->scratch_address, live->original_buffer, sizeof(live->original_buffer));
        if (next && !result) result = next;
    }
    if (live->block_saved) {
        next = lmm_live_write_checked(live, live->block_address, live->original_block, live->layout.restart_size);
        if (next && !result) result = next;
    }
    if (result) return result;
    if (live->signals_blocked) sigprocmask(SIG_SETMASK, &live->signals, NULL);
    if (live->pinned) sched_setaffinity(0, sizeof(live->affinity), &live->affinity);
    lmm_rw_close(&live->rw);
    free(live);
    return 0;
}

int lmm_live_open_with_rw(struct lmm_rw *rw, struct lmm_live **out, struct lmm_map_backend *backend) {
    struct lmm_live *live;
    uint64_t changed_pte;
    bool executable;
    bool writable;
    int64_t ignored;
    int result;

    if (!rw || !rw->ops || !rw->ops->read || !rw->ops->write || !out || !backend) return -EINVAL;
    *out = NULL;
    *backend = (struct lmm_map_backend) {0};
    live = calloc(1, sizeof(*live));
    if (!live) {
        lmm_rw_close(rw);
        return -ENOMEM;
    }
    live->rw = *rw;
    *rw = (struct lmm_rw) {0};
    if (live->rw.scratch_size < LMM_LIVE_SCRATCH_SIZE || !live->rw.max_transfer || !live->rw.scratch_address) {
        result = -EINVAL;
        goto fail;
    }
    if ((live->rw.scratch_address & (LMM_LIVE_PAGE_SIZE - 1)) + LMM_LIVE_SCRATCH_SIZE > LMM_LIVE_PAGE_SIZE) {
        result = -EOPNOTSUPP;
        goto fail;
    }
    live->scratch_address = live->rw.scratch_address;
    result = lmm_live_profile(live);
    if (result) goto fail;
    result = physical_mask(&live->physical_mask);
    if (result) goto fail;
    result = lmm_live_read_word(live, live->symbols[LMM_SYM_PAGE_OFFSET], &live->direct_map);
    if (result) goto fail;
    live->pgdir_shift = 39;
    if (live->symbols[LMM_SYM_PGDIR_SHIFT]) {
        result = lmm_live_read(live, live->symbols[LMM_SYM_PGDIR_SHIFT], &live->pgdir_shift, sizeof(live->pgdir_shift));
        if (result) goto fail;
    }
    if (live->pgdir_shift != 39 && live->pgdir_shift != 48) {
        result = -EOPNOTSUPP;
        goto fail;
    }
    result = pin_task(live);
    if (result) goto fail;
    result = find_current_task(live);
    if (result) goto fail;
    result = lmm_live_permissions(live, live->scratch_address, &executable, &writable, &live->pte_address, &live->original_pte);
    if (result) goto fail;
    if (executable || !writable || !(live->original_pte & LMM_NX) || !(live->original_pte & LMM_WRITABLE)) {
        result = -EOPNOTSUPP;
        goto fail;
    }
    result = lmm_live_read(live, live->scratch_address, live->original_buffer, sizeof(live->original_buffer));
    if (result) goto fail;
    live->buffer_saved = true;
    result = lmm_live_invoke(live, live->symbols[LMM_SYM_FLUSH_TLB], 0, 0, &ignored);
    if (result) goto fail;
    changed_pte = live->original_pte & ~LMM_NX;
    live->pte_changed = true;
    result = lmm_live_write_checked(live, live->pte_address, &changed_pte, 8);
    if (result) goto fail;
    result = lmm_live_invoke(live, live->symbols[LMM_SYM_FLUSH_TLB], 0, 0, &ignored);
    if (result) goto fail;
    *out = live;
    backend->ops = &lmm_live_map_ops;
    backend->state = live;
    return 0;

fail:
    if (lmm_live_close(live)) return -EIO;
    return result;
}
