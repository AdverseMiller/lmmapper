#include "lmm_kernel_layout.h"

#include <generated/utsrelease.h>
#include <linux/build_bug.h>
#include <linux/execmem.h>
#include <linux/restart_block.h>
#include <linux/sched.h>
#include <linux/stddef.h>
#include <asm/tlbflush.h>

static_assert(__same_type(&execmem_alloc_rw, (void *(*)(enum execmem_type, size_t))0));
static_assert(__same_type(&execmem_restore_rox, (int (*)(void *, size_t))0));
static_assert(__same_type(&execmem_free, (void (*)(void *))0));
static_assert(__same_type(&flush_tlb_all, (void (*)(void))0));
static_assert(__same_type(&do_no_restart_syscall, ((struct restart_block *)0)->fn));

const struct lmm_kernel_layout lmm_kernel_layout __used __section(".lmm_layout") = {
    .magic = LMM_KERNEL_LAYOUT_MAGIC,
    .version = LMM_KERNEL_LAYOUT_VERSION,
    .task_restart_offset = offsetof(struct task_struct, restart_block),
    .task_pid_offset = offsetof(struct task_struct, pid),
    .pid_size = sizeof(((struct task_struct *)0)->pid),
    .restart_size = sizeof(struct restart_block),
    .callback_offset = offsetof(struct restart_block, fn),
    .first_argument_offset = offsetof(struct restart_block, arch_data),
    .second_argument_offset = offsetof(struct restart_block, futex),
    .execmem_default = EXECMEM_DEFAULT,
    .release = UTS_RELEASE,
};
