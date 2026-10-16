#ifndef LMM_SYMBOLS_H
#define LMM_SYMBOLS_H

#include <stdint.h>

int lmm_kallsyms_resolve(void *path, const char *name, uint64_t *value);

#endif
