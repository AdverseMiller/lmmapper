#define _GNU_SOURCE

#include "live_internal.h"

#include <elf.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

struct mapped_elf {
    void *bytes;
    size_t size;
    const Elf64_Ehdr *header;
    const Elf64_Shdr *sections;
};

static bool range_ok(size_t total, uint64_t offset, uint64_t size) {
    return offset <= total && size <= total - offset;
}

static int open_elf(const char *path, unsigned expected_type, struct mapped_elf *image) {
    struct stat st;
    const Elf64_Ehdr *header;
    int fd;

    memset(image, 0, sizeof(*image));
    fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return -errno;
    if (fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_size < (off_t)sizeof(Elf64_Ehdr) || st.st_size > 512 * 1024 * 1024) {
        close(fd);
        return -ENOEXEC;
    }
    image->size = (size_t)st.st_size;
    image->bytes = mmap(NULL, image->size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (image->bytes == MAP_FAILED) {
        image->bytes = NULL;
        return -errno;
    }
    header = image->bytes;
    if (memcmp(header->e_ident, ELFMAG, SELFMAG) || header->e_ident[EI_CLASS] != ELFCLASS64 || header->e_ident[EI_DATA] != ELFDATA2LSB || header->e_machine != EM_X86_64 || header->e_type != expected_type || header->e_shentsize != sizeof(Elf64_Shdr) || !header->e_shnum || !range_ok(image->size, header->e_shoff, (uint64_t)header->e_shnum * sizeof(Elf64_Shdr))) {
        munmap(image->bytes, image->size);
        memset(image, 0, sizeof(*image));
        return -ENOEXEC;
    }
    image->header = header;
    image->sections = (const void *)((const unsigned char *)image->bytes + header->e_shoff);
    return 0;
}

static void close_elf(struct mapped_elf *image) {
    if (image->bytes) munmap(image->bytes, image->size);
    memset(image, 0, sizeof(*image));
}

static int build_id_from_notes(const unsigned char *bytes, size_t size, unsigned char id[20]) {
    size_t offset = 0;

    while (offset <= size && size - offset >= 12) {
        uint32_t name_size;
        uint32_t data_size;
        uint32_t type;
        size_t name_offset;
        size_t data_offset;
        size_t next;

        memcpy(&name_size, bytes + offset, 4);
        memcpy(&data_size, bytes + offset + 4, 4);
        memcpy(&type, bytes + offset + 8, 4);
        name_offset = offset + 12;
        data_offset = name_offset + (((size_t)name_size + 3) & ~(size_t)3);
        if (data_offset > size || data_size > size - data_offset) return -ENOEXEC;
        next = data_offset + (((size_t)data_size + 3) & ~(size_t)3);
        if (name_size == 4 && type == NT_GNU_BUILD_ID && data_size == 20 && !memcmp(bytes + name_offset, "GNU", 4)) {
            memcpy(id, bytes + data_offset, 20);
            return 0;
        }
        if (next <= offset) return -ENOEXEC;
        offset = next;
    }
    return -ENOENT;
}

int lmm_live_running_build_id(unsigned char id[20]) {
    unsigned char notes[4096];
    ssize_t size;
    int saved_errno;
    int fd = open("/sys/kernel/notes", O_RDONLY | O_CLOEXEC);

    if (fd < 0) return -errno;
    size = read(fd, notes, sizeof(notes));
    saved_errno = errno;
    close(fd);
    if (size < 0) return -saved_errno;
    return build_id_from_notes(notes, (size_t)size, id);
}

static int image_build_id(const struct mapped_elf *image, unsigned char id[20]) {
    for (size_t i = 0; i < image->header->e_shnum; i++) {
        const Elf64_Shdr *section = &image->sections[i];
        int result;

        if (section->sh_type != SHT_NOTE) continue;
        if (!range_ok(image->size, section->sh_offset, section->sh_size)) return -ENOEXEC;
        result = build_id_from_notes((const unsigned char *)image->bytes + section->sh_offset, section->sh_size, id);
        if (!result) return 0;
        if (result != -ENOENT) return result;
    }
    return -ENOENT;
}

int lmm_live_image_symbols(const char *path, const char *const names[LMM_SYM_COUNT], uint64_t values[LMM_SYM_COUNT], unsigned char id[20]) {
    struct mapped_elf image;
    bool found[LMM_SYM_COUNT] = {0};
    int result = open_elf(path, ET_EXEC, &image);

    if (result) return result;
    result = image_build_id(&image, id);
    if (result) goto out;
    for (size_t i = 0; i < image.header->e_shnum; i++) {
        const Elf64_Shdr *section = &image.sections[i];
        const Elf64_Shdr *strings;
        const Elf64_Sym *symbols;
        size_t count;

        if (section->sh_type != SHT_SYMTAB) continue;
        if (section->sh_link >= image.header->e_shnum || section->sh_entsize != sizeof(Elf64_Sym) || section->sh_size % sizeof(Elf64_Sym) || !range_ok(image.size, section->sh_offset, section->sh_size)) {
            result = -ENOEXEC;
            goto out;
        }
        strings = &image.sections[section->sh_link];
        if (strings->sh_type != SHT_STRTAB || !range_ok(image.size, strings->sh_offset, strings->sh_size)) {
            result = -ENOEXEC;
            goto out;
        }
        symbols = (const void *)((const unsigned char *)image.bytes + section->sh_offset);
        count = section->sh_size / sizeof(*symbols);
        for (size_t j = 0; j < count; j++) {
            const Elf64_Sym *symbol = &symbols[j];
            const char *name;

            if (symbol->st_name >= strings->sh_size) continue;
            name = (const char *)image.bytes + strings->sh_offset + symbol->st_name;
            if (!memchr(name, 0, strings->sh_size - symbol->st_name)) {
                result = -ENOEXEC;
                goto out;
            }
            for (size_t k = 0; k < LMM_SYM_COUNT; k++) {
                if (strcmp(name, names[k])) continue;
                if (found[k]) {
                    result = -ENOTUNIQ;
                    goto out;
                }
                values[k] = symbol->st_value;
                found[k] = true;
            }
        }
    }
    for (size_t i = 0; i < LMM_SYM_COUNT; i++) {
        if (i == LMM_SYM_PGDIR_SHIFT) continue;
        if (!found[i] || !values[i]) {
            result = -ENOENT;
            goto out;
        }
    }
    result = 0;

out:
    close_elf(&image);
    return result;
}

int lmm_live_read_layout(const char *path, struct lmm_kernel_layout *layout) {
    struct mapped_elf image;
    bool found = false;
    int result = open_elf(path, ET_REL, &image);

    if (result) return result;
    if (image.header->e_shstrndx >= image.header->e_shnum) {
        result = -ENOEXEC;
        goto out;
    }
    const Elf64_Shdr *names = &image.sections[image.header->e_shstrndx];
    if (names->sh_type != SHT_STRTAB || !range_ok(image.size, names->sh_offset, names->sh_size)) {
        result = -ENOEXEC;
        goto out;
    }
    for (size_t i = 0; i < image.header->e_shnum; i++) {
        const Elf64_Shdr *section = &image.sections[i];
        const char *name;

        if (section->sh_name >= names->sh_size) continue;
        name = (const char *)image.bytes + names->sh_offset + section->sh_name;
        if (!memchr(name, 0, names->sh_size - section->sh_name)) {
            result = -ENOEXEC;
            goto out;
        }
        if (strcmp(name, ".lmm_layout")) continue;
        if (found || section->sh_type != SHT_PROGBITS || section->sh_size != sizeof(*layout) || !range_ok(image.size, section->sh_offset, section->sh_size)) {
            result = -ENOEXEC;
            goto out;
        }
        memcpy(layout, (const unsigned char *)image.bytes + section->sh_offset, sizeof(*layout));
        found = true;
    }
    result = found ? 0 : -ENOENT;

out:
    close_elf(&image);
    return result;
}
