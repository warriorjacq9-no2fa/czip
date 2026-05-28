#include "zip.h"
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <zlib.h>

static zip_cdr_t* cd;
static size_t cd_len;
static size_t cd_offset;

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

void cd_update() {
    cd_len = files * sizeof(zip_cdr_t);
    for(int i = 0; i < files; i++) {
        cd_len += cd[i].name_len;
    }
}

void insert_before_file(size_t file_idx, void* data, size_t len) {
    size_t insert_pos = cd[file_idx].lfh_off;
    size_t tail_len = cd_offset - insert_pos;

    void* tail = malloc(tail_len);
    if(!tail) quit(EXIT_FAILURE);

    fseek(archive, insert_pos, SEEK_SET);
    fread(tail, 1, tail_len, archive);

    fseek(archive, insert_pos, SEEK_SET);
    fwrite(data, 1, len, archive);
    fwrite(tail, 1, tail_len, archive);
    free(tail);

    cd_offset += len;

    /* Fix up LFH offsets for every file that moved */
    for(size_t i = 0; i < files; i++) {
        if(cd[i].lfh_off >= insert_pos)
            cd[i].lfh_off += len;
    }
}

void cd_add(char* filename, size_t lfh_off, size_t size_comp, size_t size, size_t cd_idx, uint32_t crc32) {
    uint16_t time = dos_time();
    uint16_t date = dos_date();

    cd[cd_idx] = (zip_cdr_t){
        .signature = CDR_SIG,
        .version = 20,
        .min_version = 20,
        .flags = 0,
        .compression = 8,
        .mtime = time,
        .mdate = date,
        .crc32 = crc32,
        .size_comp = size_comp,
        .size = size,
        .name_len = strlen(filename),
        .extra_len = 0,
        .comment_len = 0,
        .disk_start = 0,
        .attrs_internal = 0,
        .attrs = 0,
        .lfh_off = lfh_off
    };
}

void add_file(char* filename, size_t cd_idx, size_t shadows) {
    FILE* f = fopen(filename, "rb");
    if(f == NULL) {
        char msg[64];
        snprintf(msg, 64, "Failed to open file %s", filename);
        perror(msg);
        return;
    }
    if(fseek(archive, cd_offset, SEEK_SET) < 0) {
        perror("Error seeking");
        quit(EXIT_FAILURE);
    }

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

    size_t shadow_size = sizeof(zip_lfh_t) + strlen(filename);

    for(size_t i = shadows; i > 0; i--) {
        char fn[64];
        snprintf(fn, 64, "%s%llu", filename, i);
        printf("Adding shadow %llu\n", i);
        fflush(stdout);
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
            .name_len = strlen(fn),
            .extra_len = shadow_size * i + 4
        };

        cd_add(fn, ftell(archive), compressed_size, size, cd_idx, crc32);

        /* Write LFH and data */

        uint16_t extra_hdr[] = {
            0xFFFF,
            shadow_size * i
        };

        if(fwrite(&lfh, 1, sizeof(zip_lfh_t), archive) != sizeof(zip_lfh_t)) {
            perror("Error writing LFH");
            quit(EXIT_FAILURE);
        }

        if(fwrite(fn, 1, strlen(fn), archive) != strlen(fn)) {
            perror("Error writing filename");
            quit(EXIT_FAILURE);
        }

        if(fwrite(extra_hdr, 1, 4, archive) != 4) {
            perror("Error writing file data");
            quit(EXIT_FAILURE);
        }
    }

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

    cd_add(filename, ftell(archive), compressed_size, size, cd_idx, crc32);

    /* Write LFH and data */

    if(fwrite(&lfh, 1, sizeof(zip_lfh_t), archive) != sizeof(zip_lfh_t)) {
        perror("Error writing LFH");
        quit(EXIT_FAILURE);
    }

    if(fwrite(filename, 1, strlen(filename), archive) != strlen(filename)) {
        perror("Error writing filename");
        quit(EXIT_FAILURE);
    }

    if(fwrite(out, 1, compressed_size, archive) != compressed_size) {
        perror("Error writing file data");
        quit(EXIT_FAILURE);
    }

    size_t file_end = ftell(archive);
    if(file_end < 0) {
        perror("ftell error");
        quit(EXIT_FAILURE);
    }
    cd_offset = ftell(archive);
}

void cd_write(char** filenames) {
    if(fseek(archive, cd_offset, SEEK_SET) < 0) {
        perror("Error seeking");
        quit(EXIT_FAILURE);
    }

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
    }

    cd_update();

    /* Assemble and write EOCD */

    zip_eocd_t eocd = {
        .signature = EOCD_SIG,
        .disk_num = 0,
        .cd_disk_num = 0,
        .cd_len_ldisk = files,
        .cd_len = files,
        .cd_size = cd_len,
        .cd_offset = cd_offset,
        .comment_len = 0
    };

    if(fwrite(&eocd, 1, sizeof(zip_eocd_t), archive) != sizeof(zip_eocd_t)) {
        perror("Error writing EOCD");
        quit(EXIT_FAILURE);
    }

    fflush(archive);
    ftruncate(fileno(archive), ftell(archive));
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

    files = 0;
    cd = malloc((argc - 2) * (sizeof(zip_cdr_t) + 256));

    if(argc > 2) {
        for(size_t i = 0; i < argc - 2; i++) {
            files++;
            add_file(argv[2 + i], i, 0);
            cd_write(&argv[2]);
        }
    }
    add_file(argv[2], argc - 2, 5);

    cd_write(&argv[2]);

    quit(EXIT_SUCCESS);
}