#define _GNU_SOURCE
#include "lmm_symbols.h"

#include <stdbool.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int lmm_kallsyms_resolve(void *context, const char *name, uint64_t *value) {
    const char *path = context ? context : "/proc/kallsyms";
    char *line = NULL;
    size_t capacity = 0;
    uint64_t found = 0;
    bool matched = false;
    int result = -ENOENT;
    FILE *stream;

    if (!name || !*name || !value) return -EINVAL;
    stream = fopen(path, "re");
    if (!stream) return -errno;
    while (getline(&line, &capacity, stream) >= 0) {
        unsigned long long address;
        char type;
        int offset = 0;
        char *symbol;
        size_t length;

        if (sscanf(line, "%llx %c %n", &address, &type, &offset) != 2 || !offset) continue;
        symbol = line + offset;
        length = strcspn(symbol, " \t\r\n");
        if (strlen(name) != length || memcmp(symbol, name, length)) continue;
        if (!address) {
            result = -EACCES;
            break;
        }
        if (matched) {
            result = -ENOTUNIQ;
            break;
        }
        found = address;
        matched = true;
        result = 0;
    }
    if (ferror(stream)) result = -EIO;
    free(line);
    fclose(stream);
    if (!result) *value = found;
    return result;
}
