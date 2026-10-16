#define _GNU_SOURCE
#include "lmm_elf.h"

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

static int file_range_ok(size_t size, uint64_t offset, uint64_t length)
{
    return offset <= size && length <= size - (size_t)offset;
}

static int is_power_of_two(uint64_t number)
{
    return !number || !(number & (number - 1));
}

int lmm_elf_parse(const void *input, size_t size, struct lmm_elf *out)
{
    struct lmm_elf image = {0};
    const Elf64_Ehdr *header;
    const Elf64_Shdr *sections;
    const Elf64_Shdr *names_section;

    if (!out) return -EINVAL;
    memset(out, 0, sizeof(*out));
    if (!input || size < sizeof(Elf64_Ehdr)) return -ENOEXEC;
    if (size > LMM_ELF_MAX_FILE_SIZE) return -EFBIG;

    image.bytes = malloc(size);
    if (!image.bytes) return -ENOMEM;
    memcpy(image.bytes, input, size);
    image.size = size;
    header = (const Elf64_Ehdr *)(const void *)image.bytes;

    if (memcmp(header->e_ident, ELFMAG, SELFMAG) ||
        header->e_ident[EI_CLASS] != ELFCLASS64 ||
        header->e_ident[EI_DATA] != ELFDATA2LSB ||
        header->e_ident[EI_VERSION] != EV_CURRENT ||
        header->e_ident[EI_OSABI] != ELFOSABI_SYSV ||
        header->e_version != EV_CURRENT ||
        header->e_type != ET_REL ||
        header->e_machine != EM_X86_64 ||
        header->e_ehsize != sizeof(Elf64_Ehdr) ||
        header->e_shentsize != sizeof(Elf64_Shdr) ||
        !header->e_shnum || header->e_shnum > LMM_ELF_MAX_SECTIONS ||
        header->e_shstrndx == SHN_UNDEF ||
        header->e_shstrndx == SHN_XINDEX ||
        header->e_shstrndx >= header->e_shnum ||
        !file_range_ok(size, header->e_shoff,
                       (uint64_t)header->e_shnum * sizeof(Elf64_Shdr)))
        goto malformed;

    sections = (const Elf64_Shdr *)(const void *)(image.bytes + header->e_shoff);
    names_section = &sections[header->e_shstrndx];

    if (names_section->sh_type != SHT_STRTAB ||
        !names_section->sh_size ||
        !file_range_ok(size, names_section->sh_offset, names_section->sh_size))
        goto malformed;

    image.header = header;
    image.sections = sections;
    image.names = (const char *)(image.bytes + names_section->sh_offset);
    image.names_size = (size_t)names_section->sh_size;

    for (size_t i = 0; i < header->e_shnum; i++) {
        const Elf64_Shdr *s = &sections[i];

        if (s->sh_name >= image.names_size ||
            !memchr(image.names + s->sh_name, '\0',
                    image.names_size - s->sh_name) ||
            !is_power_of_two(s->sh_addralign))
            goto malformed;

        if (s->sh_type != SHT_NOBITS &&
            !file_range_ok(size, s->sh_offset, s->sh_size))
            goto malformed;

        if (s->sh_type == SHT_SYMTAB || s->sh_type == SHT_DYNSYM) {
            if (s->sh_entsize != sizeof(Elf64_Sym) ||
                s->sh_size % sizeof(Elf64_Sym) ||
                s->sh_link >= header->e_shnum ||
                sections[s->sh_link].sh_type != SHT_STRTAB)
                goto malformed;
        }

        if (s->sh_type == SHT_RELA || s->sh_type == SHT_REL) {
            size_t entsize = s->sh_type == SHT_RELA ?
                             sizeof(Elf64_Rela) : sizeof(Elf64_Rel);
            if (s->sh_entsize != entsize || s->sh_size % entsize ||
                s->sh_link >= header->e_shnum ||
                s->sh_info >= header->e_shnum ||
                sections[s->sh_link].sh_type != SHT_SYMTAB)
                goto malformed;
        }
    }

    *out = image;
    return 0;

malformed:
    free(image.bytes);
    return -ENOEXEC;
}

int lmm_elf_open(const char *path, struct lmm_elf *out)
{
    unsigned char *bytes;
    struct stat st;
    size_t length;
    size_t received = 0;
    int fd;
    int result;

    if (!path || !out) return -EINVAL;
    fd = open(path, O_RDONLY | O_CLOEXEC | O_NONBLOCK);
    if (fd < 0) return -errno;
    if (fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_size <= 0) {
        result = -EINVAL;
        goto fail_fd;
    }
    if (st.st_size > LMM_ELF_MAX_FILE_SIZE) {
        result = -EFBIG;
        goto fail_fd;
    }

    length = (size_t)st.st_size;
    bytes = malloc(length);
    if (!bytes) {
        result = -ENOMEM;
        goto fail_fd;
    }
    while (received < length) {
        ssize_t n = read(fd, bytes + received, length - received);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) {
            result = n < 0 ? -errno : -EIO;
            goto fail_bytes;
        }
        received += (size_t)n;
    }
    result = lmm_elf_parse(bytes, length, out);
fail_bytes:
    free(bytes);
fail_fd:
    close(fd);
    return result;
}

void lmm_elf_close(struct lmm_elf *image)
{
    if (!image) return;
    free(image->bytes);
    memset(image, 0, sizeof(*image));
}

const char *lmm_elf_section_name(const struct lmm_elf *image, size_t index)
{
    if (!image || !image->header || index >= image->header->e_shnum)
        return NULL;
    return image->names + image->sections[index].sh_name;
}

int lmm_elf_stage_live(const struct lmm_elf *image, struct lmm_stage *out, size_t *executable_size) {
    struct lmm_stage stage = {0};
    size_t cursor = 0;
    size_t executable = 0;

    if (!image || !image->header || !out || !executable_size) return -EINVAL;
    *out = (struct lmm_stage) {0};
    *executable_size = 0;
    stage.section_count = image->header->e_shnum;
    stage.section_offsets = malloc(stage.section_count * sizeof(size_t));
    if (!stage.section_offsets) return -ENOMEM;
    for (size_t i = 0; i < stage.section_count; i++) stage.section_offsets[i] = SIZE_MAX;

    for (int writable = 0; writable <= 1; writable++) {
        if (writable) {
            if (cursor > LMM_ELF_MAX_STAGE_SIZE - 4095) goto too_big;
            cursor = (cursor + 4095) & ~(size_t)4095;
            executable = cursor;
        }
        for (size_t i = 0; i < stage.section_count; i++) {
            const Elf64_Shdr *section = &image->sections[i];
            size_t align;

            if (!(section->sh_flags & SHF_ALLOC) || !!(section->sh_flags & SHF_WRITE) != writable) continue;
            if (section->sh_size > LMM_ELF_MAX_STAGE_SIZE || section->sh_addralign > 4096) goto too_big;
            align = section->sh_addralign ? section->sh_addralign : 1;
            if (cursor > LMM_ELF_MAX_STAGE_SIZE - (align - 1)) goto too_big;
            cursor = (cursor + align - 1) & ~(align - 1);
            if (section->sh_size > LMM_ELF_MAX_STAGE_SIZE - cursor) goto too_big;
            stage.section_offsets[i] = cursor;
            cursor += section->sh_size;
        }
    }
    if (!executable || !cursor) {
        free(stage.section_offsets);
        return -ENOEXEC;
    }
    stage.bytes = calloc(1, cursor);
    if (!stage.bytes) {
        free(stage.section_offsets);
        return -ENOMEM;
    }
    stage.size = cursor;
    for (size_t i = 0; i < stage.section_count; i++) {
        const Elf64_Shdr *section = &image->sections[i];

        if (stage.section_offsets[i] == SIZE_MAX || section->sh_type == SHT_NOBITS || !section->sh_size) continue;
        memcpy(stage.bytes + stage.section_offsets[i], image->bytes + section->sh_offset, section->sh_size);
    }
    *executable_size = executable;
    *out = stage;
    return 0;

too_big:
    free(stage.section_offsets);
    return -EFBIG;
}

void lmm_stage_close(struct lmm_stage *stage)
{
    if (!stage) return;
    free(stage->bytes);
    free(stage->section_offsets);
    memset(stage, 0, sizeof(*stage));
}
