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
static const char** filenames;

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

void quit(int code) {
    if(cd) free(cd);
    if(archive) fclose(archive);
    if(filenames) {
        free(filenames);
    }
    exit(code);
}

void cd_update() {
    cd_len = files * sizeof(zip_cdr_t);
    for(int i = 0; i < files; i++) {
        cd_len += cd[i].name_len;
    }
}

void cd_add(const char* filename, size_t lfh_off, size_t size_comp, size_t size, size_t cd_idx, uint32_t crc32) {
    uint16_t time = dos_time();
    uint16_t date = dos_date();

    filenames[cd_idx] = filename;

    printf("Added file %s at %lu\n", filenames[cd_idx], cd_idx);

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

#define CHUNK_SIZE 16384 // 16KB chunks for streaming

void* compress_file(FILE* f, uint32_t* crc32_val, size_t* comp_size, size_t* size) {
    printf("Compressing data...\n");
    fflush(stdout);
    // 1. Initialize sizes and CRC
    *size = 0;
    *comp_size = 0;
    *crc32_val = crc32(0L, Z_NULL, 0);

    // 2. Set up zlib stream
    z_stream strm = {0};
    int ret = deflateInit2(&strm, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -MAX_WBITS, 8, Z_DEFAULT_STRATEGY);
    if (ret != Z_OK) {
        fprintf(stderr, "Failed to init DEFLATE: %d\n", ret);
        return NULL; 
    }

    // 3. Allocate static buffers
    unsigned char* in_buf = malloc(CHUNK_SIZE);
    unsigned char* out_buf = malloc(CHUNK_SIZE);
    
    // Allocate initial output container (grows dynamically)
    size_t out_capacity = CHUNK_SIZE;
    unsigned char* out_data = malloc(out_capacity);

    if (!in_buf || !out_buf || !out_data) {
        fprintf(stderr, "Memory allocation failed\n");
        deflateEnd(&strm);
        free(in_buf); free(out_buf); free(out_data);
        return NULL;
    }

    int flush;
    do {
        // Read a chunk from the file
        size_t read_bytes = fread(in_buf, 1, CHUNK_SIZE, f);
        if (ferror(f)) {
            perror("Error reading file");
            deflateEnd(&strm);
            free(in_buf); free(out_buf); free(out_data);
            return NULL;
        }

        // Track stats
        *size += read_bytes;
        *crc32_val = crc32(*crc32_val, in_buf, (uInt)read_bytes);
        
        // Signal Z_FINISH only when reaching the end of the file
        flush = feof(f) ? Z_FINISH : Z_NO_FLUSH;
        strm.next_in = in_buf;
        strm.avail_in = (uInt)read_bytes;

        // Consume all input for this chunk
        do {
            strm.next_out = out_buf;
            strm.avail_out = CHUNK_SIZE;

            ret = deflate(&strm, flush);
            if (ret == Z_STREAM_ERROR) {
                fprintf(stderr, "Deflate stream error\n");
                deflateEnd(&strm);
                free(in_buf); free(out_buf); free(out_data);
                return NULL;
            }

            size_t write_bytes = CHUNK_SIZE - strm.avail_out;
            if (write_bytes > 0) {
                // Dynamic reallocation to fit compressed output
                if (*comp_size + write_bytes > out_capacity) {
                    out_capacity *= 2;
                    unsigned char* new_out = realloc(out_data, out_capacity);
                    if (!new_out) {
                        fprintf(stderr, "Out of memory during realloc\n");
                        deflateEnd(&strm);
                        free(in_buf); free(out_buf); free(out_data);
                        return NULL;
                    }
                    out_data = new_out;
                }
                memcpy(out_data + *comp_size, out_buf, write_bytes);
                *comp_size += write_bytes;
            }
        } while (strm.avail_out == 0); // Loop if output buffer filled up

    } while (flush != Z_FINISH);

    if (ret != Z_STREAM_END) {
        fprintf(stderr, "Deflate did not end cleanly: %d\n", ret);
        deflateEnd(&strm);
        free(in_buf); free(out_buf); free(out_data);
        return NULL;
    }

    // Clean up
    deflateEnd(&strm);
    free(in_buf);
    free(out_buf);
    fclose(f);

    // Shrink memory down to actual compressed size
    return realloc(out_data, *comp_size);
}

void add_file(const char* filename, size_t cd_idx, size_t shadows) {
    FILE* f = fopen(filename, "rb");
    if(f == NULL) {
        char msg[64];
        snprintf(msg, 64, "Failed to open file %s", filename);
        perror(msg);
        return;
    }
    size_t size, compressed_size;
    uint32_t crc32;
    void* out = compress_file(f, &crc32, &compressed_size, &size);
    
    size_t shadow_size = sizeof(zip_lfh_t) + strlen(filename);
    
    uint16_t time = dos_time();
    uint16_t date = dos_date();

    for(size_t i = shadows; i > 0; i--) {
        char *fn = malloc(64);
        snprintf(fn, 64, "%s%lu", filename, i);
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

        cd_add(fn, ftell(archive), compressed_size, size, cd_idx + i, crc32);

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

        files++;
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

void cd_write() {
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
        printf("Wrote CD entry %s (%d)\n", filenames[i], i);
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
    if(ftruncate(fileno(archive), ftell(archive)) < 0) {
        perror("Error truncating");
        quit(EXIT_FAILURE);
    }
}

int main(int argc, char* argv[]) {
    if(argc != 4) {
        fprintf(stderr, "Usage: %s <outfile> <file> <overlaps>\n", argv[0]);
        return EXIT_FAILURE;
    }
    archive = fopen(argv[1], "wb+");
    if(archive == NULL) {
        fprintf(stderr, "Failed to create file %s\n", argv[1]);
        return EXIT_FAILURE;
    }

    size_t overlaps = strtoul(argv[3], NULL, 10);

    filenames = calloc((overlaps + 2), sizeof(char*));

    files = 1;
    cd = calloc((overlaps + 2), sizeof(zip_cdr_t));

    add_file(argv[2], 0, overlaps);
    cd_write();

    quit(EXIT_SUCCESS);
}