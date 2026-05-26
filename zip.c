#include "zip.h"
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <nmmintrin.h>

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

uint32_t do_crc32(void* buf, size_t len) {
    uint8_t* data = (uint8_t*)buf;
    uint32_t crc;
    while(len >= 8) {
        crc = (uint32_t)_mm_crc32_u64(crc, *(uint64_t*)data);
        data += 8;
        len -= 8;
    }

    while(len > 0) {
        crc = (uint32_t)_mm_crc32_u8(crc, *data);
        data++;
        len--;
    }

    return crc ^ 0xFFFFFFFF;
}

int main(int argc, char* argv[]) {
    if(argc != 2) {
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

    if(files > 0) {

        /* Prepare headers */
        zip_cdr_t* cd = malloc(files * sizeof(zip_cdr_t));
        zip_lfh_t* lfh = malloc(files * sizeof(zip_lfh_t));
        for(size_t i = 0; i < files; i++) {
            FILE* f = fopen(argv[2 + i], "rb");
            if(f == NULL) {
                char msg[64];
                snprintf(msg, 64, "Failed to open file %s", argv[2 + i]);
                perror(msg);
                continue;
            }
            int res;
            if((res = fseek(f, 0, SEEK_END)) < 0) {
                perror("Error seeking");
                return EXIT_FAILURE;
            }
            size_t size = ftell(f);
            void* data;
            if(fread(data, 1, size, f) != size) {
                perror("Error reading");
                return EXIT_FAILURE;
            }
            fclose(f);

            uint32_t crc32 = do_crc32(data, size);

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
                .lfh_off = 0
            };

            lfh[i] = (zip_lfh_t){
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
        }
        
        /* Write headers and data */
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