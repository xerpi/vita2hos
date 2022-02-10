#pragma once

#include <inttypes.h>

// some info taken from the wiki, see http://vitadevwiki.com/index.php?title=SELF_File_Format

#pragma pack(push, 1)
typedef struct {
	uint32_t magic;                 /* 53434500 = SCE\0 */
	uint32_t version;               /* header version 3*/
	uint16_t sdk_type;              /* */
	uint16_t header_type;           /* 1 self, 2 unknown, 3 pkg */
	uint32_t metadata_offset;       /* metadata offset */
	uint64_t header_len;            /* self header length */
	uint64_t elf_filesize;          /* ELF file length */
	uint64_t self_filesize;         /* SELF file length */
	uint64_t unknown;               /* UNKNOWN */
	uint64_t self_offset;           /* SELF offset */
	uint64_t appinfo_offset;        /* app info offset */
	uint64_t elf_offset;            /* ELF #1 offset */
	uint64_t phdr_offset;           /* program header offset */
	uint64_t shdr_offset;           /* section header offset */
	uint64_t section_info_offset;   /* section info offset */
	uint64_t sceversion_offset;     /* version offset */
	uint64_t controlinfo_offset;    /* control info offset */
	uint64_t controlinfo_size;      /* control info size */
	uint64_t padding;
} SCE_header;

typedef struct {
	uint64_t authid;                /* auth id */
	uint32_t vendor_id;             /* vendor id */
	uint32_t self_type;             /* app type */
	uint64_t version;               /* app version */
	uint64_t padding;               /* UNKNOWN */
} SCE_appinfo;

typedef struct {
	uint32_t unk1;
	uint32_t unk2;
	uint32_t unk3;
	uint32_t unk4;
} SCE_version;

typedef struct {
	uint32_t type;
	uint32_t size;
	uint32_t unk;
	uint32_t pad;
} SCE_controlinfo;

typedef struct {
	SCE_controlinfo common;
	char unk[0x100];
} SCE_controlinfo_5;

typedef struct {
	SCE_controlinfo common;
	uint32_t is_used;               /* always set to 1 */
	uint32_t attr;                  /* controls several app settings */
	uint32_t phycont_memsize;       /* physically contiguous memory budget */
	uint32_t total_memsize;         /* total memory budget (user + phycont) */
	uint32_t filehandles_limit;     /* max number of opened filehandles simultaneously */
	uint32_t dir_max_level;         /* max depth for directories support */
	uint32_t encrypt_mount_max;     /* UNKNOWN */
	uint32_t redirect_mount_max;    /* UNKNOWN */
	char unk[0xE0];
} SCE_controlinfo_6;

typedef struct {
	SCE_controlinfo common;
	char unk[0x40];
} SCE_controlinfo_7;

typedef struct {
	uint64_t offset;
	uint64_t length;
	uint64_t compression; // 1 = uncompressed, 2 = compressed
	uint64_t encryption; // 1 = encrypted, 2 = plain
} segment_info;
#pragma pack(pop)

enum {
	HEADER_LEN = 0x1000
};
