#include "zip.h"
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

uint16_t dos_time() {
    time_t now = time(NULL);
    struct tm *t = localtime(&now);

    uint16_t dos_time =
        (t->tm_hour << 11) |
        (t->tm_min  << 5)  |
        (t->tm_sec / 2);

    return dos_time;
}

uint16_t dos_date() {
    time_t now = time(NULL);
    struct tm *t = localtime(&now);

    int year = t->tm_year + 1900;

    uint16_t dos_date =
        ((year - 1980) << 9) |
        ((t->tm_mon + 1) << 5) |
        t->tm_mday;

    return dos_date;
}

uint32_t do_crc32(const void *buf, size_t len) {
    return crc32(0L, (const Bytef*)buf, len);
}

int main(int argc, char* argv[]) {
    if(argc < 2) {
        fprintf(stderr, "Usage: %s <outfile> [files]\n", argv[0]);
        return EXIT_FAILURE;
    }
    FILE* fil = fopen(argv[1], "wb+");
    if(fil == NULL) {
        fprintf(stderr, "Failed to create file %s\n", argv[1]);
        return EXIT_FAILURE;
    }
    uint16_t time = dos_time();
    uint16_t date = dos_date();

    size_t files = argc - 2;
    size_t cd_len = files * sizeof(zip_cdr_t);

    if(files > 0) {

        /* Prepare headers */
        zip_cdr_t* cd = malloc(cd_len);
        for(size_t i = 0; i < files; i++) {
            FILE* f = fopen(argv[2 + i], "rb");
            if(f == NULL) {
                char msg[64];
                snprintf(msg, 64, "Failed to open file %s", argv[2 + i]);
                perror(msg);
                continue;
            }
            if(fseek(f, 0, SEEK_END) < 0) {
                perror("Error seeking");
                fclose(f);
                return EXIT_FAILURE;
            }
            size_t size = ftell(f);
            fseek(f, 0, SEEK_SET);
            void* data = malloc(size);
            if(fread(data, 1, size, f) != size) {
                perror("Error reading");
                fclose(f);
                return EXIT_FAILURE;
            }
            fclose(f);

            uint32_t crc32 = do_crc32(data, size);

            zip_lfh_t lfh = {
                .signature = LFH_SIG,
                .min_version = 20,
                .flags = 0,
                .compression = 0,
                .mtime = time,
                .mdate = date,
                .crc32 = crc32,
                .size_comp = size,
                .size = size,
                .name_len = strlen(argv[2 + i]),
                .extra_len = 0
            };

            cd[i] = (zip_cdr_t){
                .signature = CDR_SIG,
                .version = 20,
                .min_version = 20,
                .flags = 0,
                .mtime = time,
                .mdate = date,
                .crc32 = crc32,
                .size_comp = size,
                .size = size,
                .name_len = strlen(argv[2 + i]),
                .extra_len = 0,
                .comment_len = 0,
                .disk_start = 0,
                .attrs_internal = 0,
                .attrs = 0,
                .lfh_off = ftell(fil)
            };

             /* Write LFH and data */

            if(fwrite(&lfh, 1, sizeof(zip_lfh_t), fil) != sizeof(zip_lfh_t)) {
                perror("Error writing LFH");
                fclose(fil);
                return EXIT_FAILURE;
            }

            if(fwrite(argv[2 + i], 1, strlen(argv[2 + i]), fil) != strlen(argv[2 + i])) {
                perror("Error writing filename");
                fclose(fil);
                return EXIT_FAILURE;
            }

            if(fwrite(data, 1, size, fil) != size) {
                perror("Error writing file data");
                fclose(fil);
                return EXIT_FAILURE;
            }
        }

        /* Write central directory */

        for(int i = 0; i < files; i++) {
            if(fwrite(&cd[i], 1, sizeof(zip_cdr_t), fil) != sizeof(zip_cdr_t)) {
                perror("Error writing central directory");
                fclose(fil);
                return EXIT_FAILURE;
            }
            if(fwrite(argv[2 + i], 1, cd[i].name_len, fil) != cd[i].name_len) {
                perror("Error writing filename");
                fclose(fil);
                return EXIT_FAILURE;
            }
            cd_len += cd[i].name_len;
        }
    }

    /* Assemble and write EOCD */

    zip_eocd_t eocd = {
        .signature = EOCD_SIG,
        .disk_num = 0,
        .cd_disk_num = 0,
        .cd_len_ldisk = files,
        .cd_len = files,
        .cd_size = cd_len,
        .cd_offset = ftell(fil) - cd_len,
        .comment_len = 0
    };

    size_t s;
    if((s = fwrite(&eocd, 1, sizeof(zip_eocd_t), fil)) != sizeof(zip_eocd_t)) {
        printf("%llu", s);
        perror("Error writing EOCD");
        fclose(fil);
        return EXIT_FAILURE;
    }
    fclose(fil);
}