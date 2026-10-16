#include "lmm_live_command.h"

#include <stdio.h>

int main(int argc, char **argv) {
    int result = lmm_live_command(argc, argv);

    if (result == 2) fprintf(stderr, "Usage: %s <object> <entry> <arg1> <arg2> [--release]\n", argv[0]);
    return result;
}
