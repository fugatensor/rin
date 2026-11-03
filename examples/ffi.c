#include "rin.h"
#include <stdio.h>

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s file.rin\n", argv[0]);
        return 2;
    }
    rin_result *result = rin_run_file(argv[1]);
    if (!result) {
        fputs("could not allocate a Rin result\n", stderr);
        return 2;
    }
    fputs(rin_result_output(result), stdout);
    fputs(rin_result_error(result), stderr);
    const int status = rin_result_status(result);
    rin_result_free(result);
    return status;
}
