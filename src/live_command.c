#include "lmm_live_command.h"
#include "lmm_live.h"
#include "lmm_symbols.h"

#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int parse_argument(const char *text, uint64_t *value) {
    char *end;
    unsigned long long parsed;

    if (!text || !*text || text[0] == '-') return -EINVAL;
    errno = 0;
    parsed = strtoull(text, &end, 0);
    if (errno || *end) return -EINVAL;
    *value = parsed;
    return 0;
}

int lmm_live_command(int argc, char **argv) {
    struct lmm_elf image = {0};
    struct lmm_map_backend backend = {0};
    struct lmm_reloc_report report = {0};
    struct lmm_live *live = NULL;
    uint64_t first;
    uint64_t second;
    uint64_t address = 0;
    int64_t answer = 0;
    bool release;
    int result;
    int close_result;

    if (argc != 5 && argc != 6) return 2;
    release = argc == 6 && !strcmp(argv[5], "--release");
    if (argc == 6 && !release) return 2;
    if (parse_argument(argv[3], &first) || parse_argument(argv[4], &second)) {
        fprintf(stderr, "Map arguments must be unsigned integers in decimal or 0x hexadecimal.\n");
        return 2;
    }
    result = lmm_elf_open(argv[1], &image);
    if (result) {
        fprintf(stderr, "ELF validation failed: %s\n", strerror(-result));
        return 1;
    }
    result = lmm_live_open(&live, &backend);
    if (result) {
        fprintf(stderr, "Live backend unavailable: %s\n", strerror(-result));
        if (result == -ENOENT) fprintf(stderr, "Check that 'make live-layout' completed for the running kernel and /dev/helper is present.\n");
        lmm_elf_close(&image);
        return 1;
    }
    result = lmm_map_image(&image, argv[2], first, second, &backend, lmm_kallsyms_resolve, NULL, &address, &answer, &report);
    if (result) fprintf(stderr, "Manual map failed: %s (relocations: %zu applied, %zu deferred, %zu unsupported)\n", strerror(-result), report.applied, report.deferred, report.unsupported);
    if (address) printf("Mapped image at 0x%016" PRIx64 "; entry result=%" PRId64 " (0x%" PRIx64 ")\n", address, answer, (uint64_t)answer);
    if (!result && release && address) {
        int release_result = lmm_live_release(live);
        if (release_result) {
            fprintf(stderr, "Explicit release failed: %s\n", strerror(-release_result));
            result = release_result;
        } else {
            printf("Explicitly released the mapped image at 0x%016" PRIx64 ".\n", address);
        }
    }
    if (address && (!release || result)) printf("Mapped image remains allocated at 0x%016" PRIx64 ".\n", address);
    close_result = lmm_live_close(live);
    if (close_result) {
        fprintf(stderr, "CRITICAL: bootstrap restoration failed: %s; keep the RW source available and inspect kernel state.\n", strerror(-close_result));
        result = close_result;
    }
    lmm_elf_close(&image);
    return result ? 1 : 0;
}
