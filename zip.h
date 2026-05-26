#ifndef ZIP_H
#define ZIP_H

#include <stdint.h>

#define LFH_SIG     0x04034b50

typedef struct local_file_header {
    uint32_t    signature;
    uint16_t    min_version;
    uint16_t    flags;
    uint16_t    compression;
    uint16_t    mtime;
    uint16_t    mdate;
    uint32_t    crc32;
    uint32_t    size_comp;
    uint32_t    size;
    uint16_t    name_len;
    uint16_t    extra_len;
} __attribute__((packed)) zip_lfh_t;

#define CDR_SIG     0x02014b50

typedef struct central_directory_record {
    uint32_t    signature;
    uint16_t    version;
    uint16_t    min_version;
    uint16_t    flags;
    uint16_t    compression;
    uint16_t    mtime;
    uint16_t    mdate;
    uint32_t    crc32;
    uint32_t    size_comp;
    uint32_t    size;
    uint16_t    name_len;
    uint16_t    extra_len;
    uint16_t    comment_len;
    uint16_t    disk_start;
    uint16_t    attrs_internal;
    uint32_t    attrs;
    uint32_t    lfh_off;
} __attribute__((packed)) zip_cdr_t;

#define EOCD_SIG    0x06054b50

typedef struct end_central_directory_record {
    uint32_t    signature;
    uint16_t    disk_num;
    uint16_t    cd_disk_num;
    uint16_t    cd_len_ldisk; // Number of CD entries on this disk
    uint16_t    cd_len;
    uint32_t    cd_size;
    uint32_t    cd_offset;
    uint16_t    comment_len;
} __attribute__((packed)) zip_eocd_t;

#endif