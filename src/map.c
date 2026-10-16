#include "lmm_map.h"

#include <elf.h>
#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

static int lmm_map_validate(const struct lmm_elf *image) {
    if (!image || !image->header) return -EINVAL;
    for (size_t i = 0; i < image->header->e_shnum; i++) {
        const Elf64_Shdr *section = &image->sections[i];
        const char *name = lmm_elf_section_name(image, i);

        if (!(section->sh_flags & SHF_ALLOC)) continue;
        if ((section->sh_flags & SHF_TLS) || ((section->sh_flags & SHF_WRITE) && (section->sh_flags & SHF_EXECINSTR))) return -EOPNOTSUPP;
        if (section->sh_type != SHT_PROGBITS && section->sh_type != SHT_NOBITS && section->sh_type != SHT_NOTE) return -EOPNOTSUPP;
        if (section->sh_addralign > 4096 || !strcmp(name, ".modinfo") || !strcmp(name, ".gnu.linkonce.this_module")) return -EOPNOTSUPP;
    }
    return 0;
}

static int lmm_map_entry(const struct lmm_elf *image, const struct lmm_stage *stage, const char *name, uint64_t *offset) {
    bool found = false;

    if (!name || !*name || !offset) return -EINVAL;
    for (size_t i = 0; i < image->header->e_shnum; i++) {
        const Elf64_Shdr *table = &image->sections[i];
        const Elf64_Shdr *strings;
        const Elf64_Sym *symbols;
        size_t count;

        if (table->sh_type != SHT_SYMTAB) continue;
        strings = &image->sections[table->sh_link];
        symbols = (const void *)(image->bytes + table->sh_offset);
        count = table->sh_size / sizeof(*symbols);
        for (size_t j = 0; j < count; j++) {
            const Elf64_Sym *symbol = &symbols[j];
            const Elf64_Shdr *section;
            const char *symbol_name;

            if (symbol->st_name >= strings->sh_size) return -ENOEXEC;
            symbol_name = (const char *)(image->bytes + strings->sh_offset + symbol->st_name);
            if (!memchr(symbol_name, 0, strings->sh_size - symbol->st_name)) return -ENOEXEC;
            if (strcmp(symbol_name, name)) continue;
            if (found) return -ENOTUNIQ;
            if (symbol->st_shndx >= image->header->e_shnum || ELF64_ST_TYPE(symbol->st_info) != STT_FUNC || ELF64_ST_BIND(symbol->st_info) != STB_GLOBAL) return -ENOEXEC;
            section = &image->sections[symbol->st_shndx];
            if (!(section->sh_flags & SHF_EXECINSTR) || stage->section_offsets[symbol->st_shndx] == SIZE_MAX || symbol->st_value >= section->sh_size) return -ENOEXEC;
            *offset = stage->section_offsets[symbol->st_shndx] + symbol->st_value;
            found = true;
        }
    }
    return found ? 0 : -ENOENT;
}

static int lmm_map_transfer(const struct lmm_map_backend *backend, const struct lmm_stage *stage, uint64_t base) {
    unsigned char *observed = malloc(LMM_MAP_TRANSFER_CHUNK);
    int result = 0;

    if (!observed) return -ENOMEM;
    for (size_t offset = 0; offset < stage->size; offset += LMM_MAP_TRANSFER_CHUNK) {
        size_t size = stage->size - offset;

        if (size > LMM_MAP_TRANSFER_CHUNK) size = LMM_MAP_TRANSFER_CHUNK;
        result = backend->ops->upload(backend->state, offset, stage->bytes + offset, size);
        if (result) break;
        result = backend->ops->read(backend->state, base + offset, observed, size);
        if (result) break;
        if (memcmp(observed, stage->bytes + offset, size)) {
            result = -EIO;
            break;
        }
    }
    free(observed);
    return result;
}

int lmm_map_image(const struct lmm_elf *image, const char *entry, uint64_t argument1, uint64_t argument2, const struct lmm_map_backend *backend, lmm_symbol_resolver resolver, void *resolver_context, uint64_t *mapped_address, int64_t *call_result, struct lmm_reloc_report *report) {
    struct lmm_stage stage = {0};
    uint64_t base = 0;
    uint64_t entry_offset = 0;
    size_t executable_size = 0;
    bool allocated = false;
    bool call_attempted = false;
    int result;

    if (!backend || !backend->ops || !backend->ops->allocate || !backend->ops->upload || !backend->ops->read || !backend->ops->seal || !backend->ops->call || !backend->ops->release || !mapped_address || !call_result || !report) return -EINVAL;
    *mapped_address = 0;
    *call_result = 0;
    *report = (struct lmm_reloc_report) {0};
    result = lmm_map_validate(image);
    if (result) return result;
    result = lmm_elf_stage_live(image, &stage, &executable_size);
    if (result) return result;
    if (stage.size > LMM_MAP_MAX_IMAGE) {
        result = -EFBIG;
        goto out;
    }
    result = lmm_map_entry(image, &stage, entry, &entry_offset);
    if (result) goto out;
    result = backend->ops->allocate(backend->state, stage.size, &base);
    if (result) goto out;
    allocated = true;
    if (!base) {
        result = -EFAULT;
        goto out;
    }
    result = lmm_stage_relocate(image, &stage, base, resolver, resolver_context, report);
    if (result) goto out;
    if (report->deferred || report->unsupported) {
        result = -EOPNOTSUPP;
        goto out;
    }
    result = lmm_map_transfer(backend, &stage, base);
    if (result) goto out;
    result = backend->ops->seal(backend->state, executable_size);
    if (result) goto out;
    call_attempted = true;
    *mapped_address = base;
    result = backend->ops->call(backend->state, entry_offset, argument1, argument2, call_result);

out:
    if (allocated && result && !call_attempted) {
        backend->ops->release(backend->state);
    }
    lmm_stage_close(&stage);
    return result;
}
