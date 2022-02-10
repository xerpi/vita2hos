#ifndef SCE_ELF_H
#define SCE_ELF_H

#define SCEMAG0     'S'
#define SCEMAG1     'C'
#define SCEMAG2     'E'
#define SCEMAG3     0

/* SCE-specific definitions for e_type: */
#define ET_SCE_EXEC         0xFE00		/* SCE Executable file */
#define ET_SCE_RELEXEC      0xFE04		/* SCE Relocatable file */
#define ET_SCE_STUBLIB      0xFE0C		/* SCE SDK Stubs */
#define ET_SCE_DYNAMIC      0xFE18		/* Unused */
#define ET_SCE_PSPRELEXEC   0xFFA0		/* Unused (PSP ELF only) */
#define ET_SCE_PPURELEXEC   0xFFA4		/* Unused (SPU ELF only) */
#define ET_SCE_UNK          0xFFA5		/* Unknown */

/* SCE-specific definitions for sh_type: */
#define SHT_SCE_RELA       0x60000000	/* SCE Relocations */
#define SHT_SCENID         0x61000001	/* Unused (PSP ELF only) */
#define SHT_SCE_PSPRELA    0x700000A0	/* Unused (PSP ELF only) */
#define SHT_SCE_ARMRELA    0x700000A4	/* Unused (PSP ELF only) */

/* SCE-specific definitions for p_type: */
#define PT_SCE_RELA       0x60000000	/* SCE Relocations */
#define PT_SCE_COMMENT    0x6FFFFF00	/* Unused */
#define PT_SCE_VERSION    0x6FFFFF01	/* Unused */
#define PT_SCE_UNK        0x70000001	/* Unknown */
#define PT_SCE_PSPRELA    0x700000A0	/* Unused (PSP ELF only) */
#define PT_SCE_PPURELA    0x700000A4	/* Unused (SPU ELF only) */

#define NID_MODULE_STOP         0x79F8E492
#define NID_MODULE_EXIT         0x913482A9
#define NID_MODULE_START        0x935CD196
#define NID_MODULE_BOOTSTART    0x5C424D40
#define NID_MODULE_INFO         0x6C2224BA
#define NID_PROCESS_PARAM       0x70FBA1E7
#define NID_MODULE_SDK_VERSION  0x936C8A78

#define PSP2_SDK_VERSION 0x03570011

typedef union {
	Elf32_Word         r_type;
	struct {
		Elf32_Word r_opt1;
		Elf32_Word r_opt2;
	} r_short;
	struct {
		Elf32_Word r_type;
		Elf32_Word r_addend;
		Elf32_Word r_offset;
	} r_long;
	struct {
		Elf32_Word r_word1;
		Elf32_Word r_word2;
		Elf32_Word r_word3;
	} r_raw;
} SCE_Rel;

/* Macros to get SCE reloc values */
#define SCE_REL_SHORT_OFFSET(x) (((x).r_opt1 >> 20) | ((x).r_opt2 & 0x3FF) << 12)
#define SCE_REL_SHORT_ADDEND(x) ((x).r_opt2 >> 10)
#define SCE_REL_LONG_OFFSET(x) ((x).r_offset)
#define SCE_REL_LONG_ADDEND(x) ((x).r_addend)
#define SCE_REL_LONG_CODE2(x) (((x).r_type >> 20) & 0xFF)
#define SCE_REL_LONG_DIST2(x) (((x).r_type >> 28) & 0xF)
#define SCE_REL_IS_SHORT(x) (((x).r_type) & 0xF)
#define SCE_REL_CODE(x) (((x).r_type >> 8) & 0xFF)
#define SCE_REL_SYMSEG(x) (((x).r_type >> 4) & 0xF)
#define SCE_REL_DATSEG(x) (((x).r_type >> 16) & 0xF)

#define SCE_ELF_DEFS_HOST
#include "sce-elf-defs.h"
#undef SCE_ELF_DEFS_HOST

#define SCE_ELF_DEFS_TARGET
#include "sce-elf-defs.h"
#undef SCE_ELF_DEFS_TARGET

#endif
