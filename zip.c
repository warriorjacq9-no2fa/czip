#include "zip.h"
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

static zip_cdr_t* cd;
static size_t cd_len;
static FILE* archive;
static size_t files;

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

void quit(int code) {
    if(cd) free(cd);
    if(archive) fclose(archive);
    exit(code);
}

void add_file(FILE* f, char* filename, size_t cd_idx) {
    uint16_t time = dos_time();
    uint16_t date = dos_date();

    if(fseek(f, 0, SEEK_END) < 0) {
        perror("Error seeking");
        quit(EXIT_FAILURE);
    }
    size_t size = ftell(f);
    fseek(f, 0, SEEK_SET);
    void* data = malloc(size);
    if(fread(data, 1, size, f) != size) {
        perror("Error reading");
        quit(EXIT_FAILURE);
    }
    fclose(f);

    void* out = malloc(compressBound(size));

    z_stream strm = {0};

    strm.next_in = (Bytef *)data;
    strm.avail_in = size;

    strm.next_out = out;
    strm.avail_out = compressBound(size);

    // Negative windowBits => raw DEFLATE
    if (deflateInit2(&strm,
                     Z_DEFAULT_COMPRESSION,
                     Z_DEFLATED,
                     -MAX_WBITS,
                     8,
                     Z_DEFAULT_STRATEGY) != Z_OK) {
        quit(EXIT_FAILURE);
    }

    int ret = deflate(&strm, Z_FINISH);

    if (ret != Z_STREAM_END) {
        deflateEnd(&strm);
        quit(EXIT_FAILURE);
    }

    size_t compressed_size = strm.total_out;
    deflateEnd(&strm);

    uint32_t crc32 = do_crc32(data, size);

    zip_lfh_t lfh = {
        .signature = LFH_SIG,
        .min_version = 20,
        .flags = 0,
        .compression = 8,
        .mtime = time,
        .mdate = date,
        .crc32 = crc32,
        .size_comp = compressed_size,
        .size = size,
        .name_len = strlen(filename),
        .extra_len = 0
    };

    cd[cd_idx] = (zip_cdr_t){
        .signature = CDR_SIG,
        .version = 20,
        .min_version = 20,
        .flags = 0,
        .mtime = time,
        .mdate = date,
        .crc32 = crc32,
        .size_comp = compressed_size,
        .size = size,
        .name_len = strlen(filename),
        .extra_len = 0,
        .comment_len = 0,
        .disk_start = 0,
        .attrs_internal = 0,
        .attrs = 0,
        .lfh_off = ftell(archive)
    };

    /* Write LFH and data */

    if(fwrite(&lfh, 1, sizeof(zip_lfh_t), archive) != sizeof(zip_lfh_t)) {
        perror("Error writing LFH");
        quit(EXIT_FAILURE);
    }

    if(fwrite(filename, 1, strlen(filename), archive) != strlen(filename)) {
        perror("Error writing filename");
        quit(EXIT_FAILURE);
    }

    if(fwrite(out, 1, size, archive) != size) {
        perror("Error writing file data");
        quit(EXIT_FAILURE);
    }
}

void archive_write(char** filenames) {
    /* Write central directory */

    for(int i = 0; i < files; i++) {
        if(fwrite(&cd[i], 1, sizeof(zip_cdr_t), archive) != sizeof(zip_cdr_t)) {
            perror("Error writing central directory");
            quit(EXIT_FAILURE);
        }
        if(fwrite(filenames[i], 1, cd[i].name_len, archive) != cd[i].name_len) {
            perror("Error writing filename");
            quit(EXIT_FAILURE);
        }
        cd_len += cd[i].name_len;
    }

    /* Assemble and write EOCD */

    zip_eocd_t eocd = {
        .signature = EOCD_SIG,
        .disk_num = 0,
        .cd_disk_num = 0,
        .cd_len_ldisk = files,
        .cd_len = files,
        .cd_size = cd_len,
        .cd_offset = ftell(archive) - cd_len,
        .comment_len = 0
    };

    if(fwrite(&eocd, 1, sizeof(zip_eocd_t), archive) != sizeof(zip_eocd_t)) {
        perror("Error writing EOCD");
        quit(EXIT_FAILURE);
    }
}

int main(int argc, char* argv[]) {
    if(argc < 2) {
        fprintf(stderr, "Usage: %s <outfile> [files]\n", argv[0]);
        return EXIT_FAILURE;
    }
    archive = fopen(argv[1], "wb+");
    if(archive == NULL) {
        fprintf(stderr, "Failed to create file %s\n", argv[1]);
        return EXIT_FAILURE;
    }

    files = argc - 2;
    cd_len = files * sizeof(zip_cdr_t);
    cd = malloc(cd_len);

    if(files > 0) {
        /* Prepare headers */
        for(size_t i = 0; i < files; i++) {
            FILE* f = fopen(argv[2 + i], "rb");
            if(f == NULL) {
                char msg[64];
                snprintf(msg, 64, "Failed to open file %s", argv[2 + i]);
                perror(msg);
                continue;
            }
            add_file(f, argv[2 + i], i);
        }
    }

    archive_write(&argv[2]);

    quit(EXIT_SUCCESS);
}