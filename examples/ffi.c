#include "suka.h"
#include <stdio.h>

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s file.suka\n", argv[0]);
        return 2;
    }
    suka_result *result = suka_run_file(argv[1]);
    if (!result) {
        fputs("could not allocate a Suka result\n", stderr);
        return 2;
    }
    fputs(suka_result_output(result), stdout);
    fputs(suka_result_error(result), stderr);
    const int status = suka_result_status(result);
    suka_result_free(result);
    return status;
}
