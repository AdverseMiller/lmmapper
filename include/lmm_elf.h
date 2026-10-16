#ifndef LMM_ELF_H
#define LMM_ELF_H

#include <elf.h>
#include <stddef.h>
#include <stdint.h>

#define LMM_ELF_MAX_FILE_SIZE (64U * 1024U * 1024U)
#define LMM_ELF_MAX_SECTIONS 4096U
#define LMM_ELF_MAX_STAGE_SIZE (64U * 1024U * 1024U)

struct lmm_elf {
    unsigned char *bytes;  /* private owned copy of the entire file */
    size_t size;
    const Elf64_Ehdr *header;
    const Elf64_Shdr *sections;
    const char *names;
    size_t names_size;
};

struct lmm_stage {
    unsigned char *bytes;  /* owned userspace buffer, NEVER kernel memory */
    size_t size;
    size_t *section_offsets; /* SIZE_MAX for non-allocated sections */
    size_t section_count;
};

int lmm_elf_parse(const void *data, size_t size, struct lmm_elf *out);
int lmm_elf_open(const char *path, struct lmm_elf *out);
void lmm_elf_close(struct lmm_elf *image);
const char *lmm_elf_section_name(const struct lmm_elf *image, size_t index);
int lmm_elf_stage_live(const struct lmm_elf *image, struct lmm_stage *out, size_t *executable_size);
void lmm_stage_close(struct lmm_stage *stage);

#endif
