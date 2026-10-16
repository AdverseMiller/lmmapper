#include "lmm_reloc.h"

#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* GNU C extension is used solely to perform overflow-safe intermediate math. */
__extension__ typedef __int128 lmm_wide;

static size_t relocation_width(unsigned type)
{
    switch (type) {
    case R_X86_64_NONE: return 0;
    case R_X86_64_64:
    case R_X86_64_PC64: return 8;
    case R_X86_64_PC32:
    case R_X86_64_PLT32:
    case R_X86_64_32:
    case R_X86_64_32S: return 4;
    case R_X86_64_16:
    case R_X86_64_PC16: return 2;
    case R_X86_64_8:
    case R_X86_64_PC8: return 1;
    default: return SIZE_MAX;
    }
}

static int is_pc_relative(unsigned type)
{
    switch (type) {
    case R_X86_64_PC64:
    case R_X86_64_PC32:
    case R_X86_64_PLT32:
    case R_X86_64_PC16:
    case R_X86_64_PC8:
        return 1;
    default:
        return 0;
    }
}

static int store_relocation(unsigned char *dest, unsigned type, lmm_wide value)
{
    switch (type) {
    case R_X86_64_64: {
        uint64_t result;
        if (value < INT64_MIN || value > UINT64_MAX) return -ERANGE;
        result = (uint64_t)value;
        memcpy(dest, &result, sizeof(result));
        return 0;
    }
    case R_X86_64_PC64: {
        uint64_t result;
        if (value < INT64_MIN || value > INT64_MAX) return -ERANGE;
        result = (uint64_t)value;
        memcpy(dest, &result, sizeof(result));
        return 0;
    }
    case R_X86_64_32: {
        uint32_t result;
        if (value < 0 || value > UINT32_MAX) return -ERANGE;
        result = (uint32_t)value;
        memcpy(dest, &result, sizeof(result));
        return 0;
    }
    case R_X86_64_PC32:
    case R_X86_64_PLT32: {
        uint32_t result;
        if (value < INT32_MIN || value > INT32_MAX) return -ERANGE;
        result = (uint32_t)value;
        memcpy(dest, &result, sizeof(result));
        return 0;
    }
    case R_X86_64_32S: {
        uint64_t full;
        uint64_t sign_extended;
        uint32_t result;

        if (value < INT64_MIN || value > UINT64_MAX) return -ERANGE;
        full = (uint64_t)value;
        result = (uint32_t)full;
        sign_extended = (result & UINT32_C(0x80000000)) ? UINT64_C(0xffffffff00000000) | result : result;
        if (full != sign_extended) return -ERANGE;
        memcpy(dest, &result, sizeof(result));
        return 0;
    }
    case R_X86_64_16: {
        uint16_t result;
        if (value < 0 || value > UINT16_MAX) return -ERANGE;
        result = (uint16_t)value;
        memcpy(dest, &result, sizeof(result));
        return 0;
    }
    case R_X86_64_PC16: {
        uint16_t result;
        if (value < INT16_MIN || value > INT16_MAX) return -ERANGE;
        result = (uint16_t)value;
        memcpy(dest, &result, sizeof(result));
        return 0;
    }
    case R_X86_64_8: {
        uint8_t result;
        if (value < 0 || value > UINT8_MAX) return -ERANGE;
        result = (uint8_t)value;
        memcpy(dest, &result, sizeof(result));
        return 0;
    }
    case R_X86_64_PC8: {
        uint8_t result;
        if (value < INT8_MIN || value > INT8_MAX) return -ERANGE;
        result = (uint8_t)value;
        memcpy(dest, &result, sizeof(result));
        return 0;
    }
    default:
        return -EOPNOTSUPP;
    }
}

static int symbol_value(const struct lmm_elf *image,
                        const struct lmm_stage *stage,
                        const Elf64_Shdr *symtab,
                        const Elf64_Sym *symbol,
                        uint64_t virtual_base,
                        lmm_symbol_resolver resolve,
                        void *resolver_context,
                        uint64_t *value)
{
    const Elf64_Shdr *string_table = &image->sections[symtab->sh_link];
    const char *name;

    if (symbol->st_name >= string_table->sh_size) return -ENOEXEC;
    name = (const char *)(image->bytes + string_table->sh_offset +
                          symbol->st_name);
    if (!memchr(name, 0, (size_t)(string_table->sh_size - symbol->st_name)))
        return -ENOEXEC;

    if (symbol->st_shndx == SHN_ABS) {
        *value = symbol->st_value;
        return 0;
    }

    if (symbol->st_shndx == SHN_UNDEF) {
        if (!symbol->st_name) {
            *value = 0;
            return 0;
        }
        if (!resolve) return -ENOENT;
        return resolve(resolver_context, name, value);
    }

    if (symbol->st_shndx >= image->header->e_shnum ||
        symbol->st_shndx == SHN_COMMON)
        return -ENOENT;

    if (stage->section_offsets[symbol->st_shndx] == SIZE_MAX)
        return -ENOENT;

    if (symbol->st_value >
        image->sections[symbol->st_shndx].sh_size)
        return -ENOEXEC;

    if (stage->section_offsets[symbol->st_shndx] >
        stage->size || symbol->st_value >
        stage->size - stage->section_offsets[symbol->st_shndx])
        return -ENOEXEC;

    *value = virtual_base +
             (uint64_t)stage->section_offsets[symbol->st_shndx] +
             symbol->st_value;
    return 0;
}

int lmm_stage_relocate(const struct lmm_elf *image,
                       struct lmm_stage *stage,
                       uint64_t virtual_base,
                       lmm_symbol_resolver resolve,
                       void *resolver_context,
                       struct lmm_reloc_report *report)
{
    struct lmm_reloc_report local = {0};
    unsigned char *working;

    if (!image || !image->header || !stage || !stage->bytes ||
        !stage->section_offsets ||
        stage->section_count != image->header->e_shnum ||
        !report)
        return -EINVAL;

    memset(report, 0, sizeof(*report));
    if (virtual_base > UINT64_MAX - stage->size)
        return -EOVERFLOW;
    working = malloc(stage->size);
    if (!working) return -ENOMEM;
    memcpy(working, stage->bytes, stage->size);

    for (size_t i = 0; i < image->header->e_shnum; i++) {
        const Elf64_Shdr *rsec = &image->sections[i];
        const Elf64_Shdr *target;
        const Elf64_Shdr *symtab;
        const Elf64_Sym *symbols;
        const Elf64_Rela *relocs;
        size_t count;
        size_t symbol_count;

        if (rsec->sh_type != SHT_RELA && rsec->sh_type != SHT_REL)
            continue;
        if (rsec->sh_info >= stage->section_count ||
            rsec->sh_link >= stage->section_count) {
            free(working);
            return -ENOEXEC;
        }

        target = &image->sections[rsec->sh_info];
        count = (size_t)(rsec->sh_size / rsec->sh_entsize);
        if (!(target->sh_flags & SHF_ALLOC)) {
            local.skipped += count;
            continue;
        }
        local.total += count;
        if (stage->section_offsets[rsec->sh_info] == SIZE_MAX) {
            free(working);
            return -ENOEXEC;
        }

        /* ELF64/x86-64 Linux .ko files use RELA. Do not guess REL addends. */
        if (rsec->sh_type == SHT_REL) {
            local.unsupported += count;
            continue;
        }

        symtab = &image->sections[rsec->sh_link];
        symbols = (const void *)(image->bytes + symtab->sh_offset);
        symbol_count = (size_t)(symtab->sh_size / sizeof(*symbols));
        relocs = (const void *)(image->bytes + rsec->sh_offset);

        for (size_t j = 0; j < count; j++) {
            unsigned type = ELF64_R_TYPE(relocs[j].r_info);
            uint32_t symbol_index = ELF64_R_SYM(relocs[j].r_info);
            size_t width = relocation_width(type);
            size_t place_offset;
            uint64_t place;
            uint64_t value;
            lmm_wide result;
            int err;

            if (relocs[j].r_offset > target->sh_size ||
                symbol_index >= symbol_count) {
                free(working);
                return -ENOEXEC;
            }
            if (type == R_X86_64_NONE) {
                local.skipped++;
                continue;
            }
            if (width == SIZE_MAX) {
                local.unsupported++;
                continue;
            }
            if (width > target->sh_size - relocs[j].r_offset) {
                free(working);
                return -ENOEXEC;
            }
            place_offset = stage->section_offsets[rsec->sh_info] +
                           (size_t)relocs[j].r_offset;
            if (place_offset > stage->size ||
                width > stage->size - place_offset) {
                free(working);
                return -ENOEXEC;
            }

            err = symbol_value(image, stage, symtab,
                               &symbols[symbol_index], virtual_base,
                               resolve, resolver_context, &value);
            if (err == -ENOENT) {
                local.deferred++;
                continue;
            }
            if (err) {
                free(working);
                return err;
            }

            place = virtual_base + (uint64_t)place_offset;
            result = (lmm_wide)value + (lmm_wide)relocs[j].r_addend;
            if (is_pc_relative(type))
                result -= (lmm_wide)place;

            err = store_relocation(working + place_offset, type, result);
            if (err) {
                free(working);
                return err;
            }
            local.applied++;
        }
    }

    memcpy(stage->bytes, working, stage->size);
    free(working);
    *report = local;
    return 0;
}
