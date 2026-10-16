#ifndef LMM_KERNEL_LAYOUT_H
#define LMM_KERNEL_LAYOUT_H

#include <linux/types.h>

#define LMM_KERNEL_LAYOUT_MAGIC 0x4c4d4c59U
#define LMM_KERNEL_LAYOUT_VERSION 1U

struct lmm_kernel_layout {
    __u32 magic;
    __u32 version;
    __u32 task_restart_offset;
    __u32 task_pid_offset;
    __u32 pid_size;
    __u32 restart_size;
    __u32 callback_offset;
    __u32 first_argument_offset;
    __u32 second_argument_offset;
    __u32 execmem_default;
    char release[64];
};

#endif
