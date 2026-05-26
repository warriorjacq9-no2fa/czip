#include "zip.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char** argv) {
    if(argc != 2) {
        fprintf(stderr, "Usage: %s <outfile>\n", argv[0]);
        return EXIT_FAILURE;
    }
    FILE* fil = fopen(argv[1], "wb+");
    if(fil == NULL) {
        fprintf(stderr, "Failed to create file %s\n", argv[1]);
        return EXIT_FAILURE;
    }

    zip_eocd_t eocd = {
        .signature = EOCD_SIG,
        .disk_num = 0,
        .cd_disk_num = 0,
        .disk_entries = 0,
        .cd_len = 0,
        .cd_size = 0,
        .cd_offset = 0,
        .comment_len = 0
    };

    fwrite(&eocd, sizeof(eocd), 1, fil);
    fclose(fil);
}