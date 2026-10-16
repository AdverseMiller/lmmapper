#ifndef LMM_RELOC_H
#define LMM_RELOC_H

#include <stddef.h>
#include <stdint.h>

#include "lmm_elf.h"

/*
 * ELF relocation into an owned userspace byte array. virtual_base may be a
 * synthetic test address or the address of a separate kernel allocation.
 */
typedef int (*lmm_symbol_resolver)(void *context, const char *name, uint64_t *value);

struct lmm_reloc_report {
    size_t total;        /* relocations targeting staged SHF_ALLOC sections */
    size_t applied;
    size_t deferred;     /* external symbols without a synthetic definition */
    size_t unsupported;  /* relocation types the offline builder cannot handle */
    size_t skipped;      /* NONE, or relocation sections for non-alloc sections */
};

/*
 * Applies supported x86_64 RELA relocations to stage.bytes, atomically.
 * An unresolved symbol leaves that relocation untouched, and increments
 * report.deferred. The supplied resolver may use the running kernel's symbols.
 * On a malformed or overflowing relocation, the original stage is unchanged.
 *
 * Calling this function never accesses kernel memory or runs the staged code.
 */
int lmm_stage_relocate(const struct lmm_elf *image,
                       struct lmm_stage *stage,
                       uint64_t virtual_base,
                       lmm_symbol_resolver resolve,
                       void *resolver_context,
                       struct lmm_reloc_report *report);

#endif
