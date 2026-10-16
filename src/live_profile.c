#include "live_internal.h"
#include "lmm_symbols.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/utsname.h>
#include <unistd.h>

static const char *const required_symbols[LMM_SYM_COUNT] = {
    [LMM_SYM_RESTART] = "do_no_restart_syscall",
    [LMM_SYM_TOP] = "init_top_pgt",
    [LMM_SYM_PAGE_OFFSET] = "page_offset_base",
    [LMM_SYM_PERCPU_OFFSET] = "__per_cpu_offset",
    [LMM_SYM_CURRENT_TASK] = "current_task",
    [LMM_SYM_FLUSH_TLB] = "flush_tlb_all",
    [LMM_SYM_EXECMEM_ALLOC] = "execmem_alloc_rw",
    [LMM_SYM_EXECMEM_SEAL] = "execmem_restore_rox",
    [LMM_SYM_EXECMEM_FREE] = "execmem_free",
    [LMM_SYM_PGDIR_SHIFT] = "pgdir_shift",
};

static bool overlap(unsigned first, unsigned second) {
    return first < second + 8 && second < first + 8;
}

static int default_layout_path(char path[PATH_MAX]) {
    static const char suffix[] = "/tools/kernel_layout/layout_probe.o";
    char executable[PATH_MAX];
    char *separator;
    ssize_t length = readlink("/proc/self/exe", executable, sizeof(executable) - 1);

    if (length < 0) return -errno;
    executable[length] = 0;
    for (int level = 0; level < 2; level++) {
        int written;

        separator = strrchr(executable, '/');
        if (!separator) return -ENOENT;
        *separator = 0;
        written = snprintf(path, PATH_MAX, "%s%s", executable, suffix);
        if (written < 0 || written >= PATH_MAX) return -ENAMETOOLONG;
        if (!access(path, R_OK)) return 0;
    }
    return -ENOENT;
}

static int validate_layout(const struct lmm_kernel_layout *layout, const char *release) {
    if (layout->magic != LMM_KERNEL_LAYOUT_MAGIC || layout->version != LMM_KERNEL_LAYOUT_VERSION) return -EPROTO;
    if (!memchr(layout->release, 0, sizeof(layout->release)) || strcmp(layout->release, release)) return -ESTALE;
    if (layout->task_restart_offset >= 16384 || layout->task_pid_offset >= 16384 || layout->pid_size != sizeof(uint32_t)) return -EOPNOTSUPP;
    if (layout->restart_size < 24 || layout->restart_size > LMM_LIVE_BLOCK_MAX) return -EOPNOTSUPP;
    if (layout->callback_offset > layout->restart_size - 8 || layout->first_argument_offset > layout->restart_size - 8 || layout->second_argument_offset > layout->restart_size - 8) return -EOPNOTSUPP;
    if (overlap(layout->callback_offset, layout->first_argument_offset) || overlap(layout->callback_offset, layout->second_argument_offset) || overlap(layout->first_argument_offset, layout->second_argument_offset)) return -EOPNOTSUPP;
    return 0;
}

int lmm_live_profile(struct lmm_live *live) {
    struct utsname system;
    uint64_t image_symbols[LMM_SYM_COUNT] = {0};
    unsigned char running_id[20];
    unsigned char image_id[20];
    const char *image_path = getenv("LMM_VMLINUX");
    const char *layout_path = getenv("LMM_KERNEL_LAYOUT");
    char default_image_path[256];
    char resolved_layout_path[PATH_MAX];
    uint64_t restart;
    uint64_t slide;
    int result;

    if (!live) return -EINVAL;
    if (uname(&system)) return -errno;
    if (strcmp(system.machine, "x86_64")) return -EOPNOTSUPP;
    if (!image_path || !*image_path) {
        int length = snprintf(default_image_path, sizeof(default_image_path), "/lib/modules/%s/build/vmlinux", system.release);
        if (length < 0 || (size_t)length >= sizeof(default_image_path)) return -ENAMETOOLONG;
        image_path = default_image_path;
    }
    if (!layout_path || !*layout_path) {
        result = default_layout_path(resolved_layout_path);
        if (result) return result;
        layout_path = resolved_layout_path;
    }
    result = lmm_live_running_build_id(running_id);
    if (result) return result;
    result = lmm_live_image_symbols(image_path, required_symbols, image_symbols, image_id);
    if (result) return result;
    if (memcmp(running_id, image_id, sizeof(running_id))) return -ESTALE;
    result = lmm_live_read_layout(layout_path, &live->layout);
    if (result) return result;
    result = validate_layout(&live->layout, system.release);
    if (result) return result;
    result = lmm_kallsyms_resolve(NULL, "do_no_restart_syscall", &restart);
    if (result) return result;
    slide = restart - image_symbols[LMM_SYM_RESTART];
    if (slide & (LMM_LIVE_PAGE_SIZE - 1) || slide > UINT64_C(0x100000000)) return -ESTALE;
    for (size_t i = 0; i < LMM_SYM_COUNT; i++) live->symbols[i] = image_symbols[i] ? image_symbols[i] + slide : 0;
    return 0;
}
