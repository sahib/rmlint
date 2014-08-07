#ifndef FILEMAP_H_INCLUDED
#define FILEMAP_H_INCLUDED

#include <linux/fs.h>
#include <linux/fiemap.h>
#include <errno.h>
#include <stdio.h>

#include <stdlib.h>
#include <stdint.h>

#include <glib.h>


#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <inttypes.h>


#define FILEFRAG_FIEMAP_FLAGS_COMPAT (FIEMAP_FLAG_SYNC | FIEMAP_FLAG_XATTR)

typedef struct OffsetEntry {
    uint64_t logical;
    uint64_t physical;
} OffsetEntry;

typedef enum RmFileOffsetType {
    RM_OFFSET_START = 0,
    RM_OFFSET_RELATIVE,  /*relative to current file fseek() position*/
    RM_OFFSET_ABSOLUTE,
    RM_OFFSET_END        /*last byte of file */
} RmFileOffsetType;


uint64_t get_disk_offset(GSequence *offset_list, uint64_t file_offset);
GSequence *get_fiemap_extents(char *path);

#endif // FILEMAP_H_INCLUDED
