#include "compiler_compat.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <miniz/miniz.h>
#include <switch.h>
#include <psp2/kernel/error.h>
#include "arm-encode.h"
#include "load.h"
#include "log.h"
#include "module.h"
#include "sce-elf.h"
#include "self.h"
#include "util.h"

#define IMP_GET_NEXT(imp) ((sce_module_imports_u_t *)((char *)imp + imp->size))
#define IMP_GET_FUNC_COUNT(imp) (imp->size == sizeof(sce_module_imports_short_raw) ? imp->imports_short.num_syms_funcs : imp->imports.num_syms_funcs)
#define IMP_GET_VARS_COUNT(imp) (imp->size == sizeof(sce_module_imports_short_raw) ? imp->imports_short.num_syms_vars : imp->imports.num_syms_vars)
#define IMP_GET_NID(imp) (imp->size == sizeof(sce_module_imports_short_raw) ? imp->imports_short.library_nid : imp->imports.library_nid)
#define IMP_GET_NAME(imp) (imp->size == sizeof(sce_module_imports_short_raw) ? imp->imports_short.library_name : imp->imports.library_name)
#define IMP_GET_FUNC_TABLE(imp) (imp->size == sizeof(sce_module_imports_short_raw) ? imp->imports_short.func_nid_table : imp->imports.func_nid_table)
#define IMP_GET_FUNC_ENTRIES(imp) (imp->size == sizeof(sce_module_imports_short_raw) ? imp->imports_short.func_entry_table : imp->imports.func_entry_table)
#define IMP_GET_VARS_TABLE(imp) (imp->size == sizeof(sce_module_imports_short_raw) ? imp->imports_short.var_nid_table : imp->imports.var_nid_table)
#define IMP_GET_VARS_ENTRIES(imp) (imp->size == sizeof(sce_module_imports_short_raw) ? imp->imports_short.var_entry_table : imp->imports.var_entry_table)

#define CODE_RX_TO_RW_ADDR(rx_base, rw_base, rx_addr) \
	((((uintptr_t)rx_addr - (uintptr_t)rx_base)) + (uintptr_t)rw_base)

typedef struct {
	void *src_data;
	uintptr_t rw_addr;
	uintptr_t rx_addr; /* Only used for executable segments */
	Elf32_Word p_type;
	Elf32_Word p_filesz;
	Elf32_Word p_memsz;
	Elf32_Word p_flags;
	Elf32_Word p_align;
	bool needs_free;
} segment_info_t;

static int load_vpk(Jit *jit, const void *data, uint32_t size, void **entry);
static int load_executable(Jit *jit, const uint8_t *data, void **entry);
static int load_elf(Jit *jit, const void *data, void **entry);
static int load_self(Jit *jit, const void *data, void **entry);
static int load_segments(Jit *jit, void **entry, Elf32_Addr e_entry, segment_info_t *segments, int num_segments);
static int resolve_imports(uintptr_t rx_base, uintptr_t rw_base, sce_module_imports_u_t *import);
static int relocate(const void *reloc, uint32_t size, segment_info_t *segs);

int elf_check_vita_header(const Elf32_Ehdr *hdr)
{
	if (!(hdr->e_ident[EI_MAG0] == ELFMAG0 && hdr->e_ident[EI_MAG1] == ELFMAG1 &&
	      hdr->e_ident[EI_MAG2] == ELFMAG2 && hdr->e_ident[EI_MAG3] == ELFMAG3)) {
		LOG("Invalid ELF magic number.");
		return -1;
	}

	if (hdr->e_ident[EI_CLASS] != ELFCLASS32) {
		LOG("Not a 32bit executable.");
		return -1;
	}

	if (hdr->e_ident[EI_DATA] != ELFDATA2LSB) {
		LOG("Not a valid ARM executable.");
		return -1;
	}

	if (hdr->e_ident[EI_VERSION] != EV_CURRENT) {
		LOG("Unsupported ELF version.");
		return -1;
	}

	if (hdr->e_type != ET_SCE_RELEXEC) {
		LOG("Only ET_SCE_RELEXEC files are supported.");
		return -1;
	}

	if (hdr->e_machine != EM_ARM) {
		LOG("Not an ARM executable.");
		return -1;
	}

	if (hdr->e_version != EV_CURRENT) {
		LOG("Unsupported ELF version.");
		return -1;
	}

	return 0;
}

static int elf_get_sce_module_info(Elf32_Addr e_entry, const segment_info_t *segments, sce_module_info_raw **mod_info)
{
	uint32_t offset;
	uint32_t index;

	index = (e_entry & 0xC0000000) >> 30;
	offset = e_entry & 0x3FFFFFFF;

	if (segments[index].src_data == NULL) {
		LOG("Invalid segment index %" PRId32, index);
		return -1;
	}

	*mod_info = (sce_module_info_raw *)((char *)segments[index].src_data + offset);

	return index;
}

int load_file(Jit *jit, const char *filename, void **entry)
{
	void *data;
	uint32_t size;
	int ret = 0;

	LOG("Opening '%s' for reading.", filename);
	if (util_load_file(filename, &data, &size) != 0) {
		LOG("Cannot load file.");
		return -1;
	}

	if (load_executable(jit, data, entry) < 0) {
		/* Fallback to vpk (zip file) */
		if (load_vpk(jit, data, size, entry) < 0) {
			LOG("Unsupported file.");
			ret = -1;
		}
	}

	free(data);
	return ret;
}

static int load_vpk(Jit *jit, const void *data, uint32_t size, void **entry)
{
	mz_bool status;
	mz_zip_archive zip_archive;
	size_t uncompressed_size;
	void *eboot_bin;
	int ret;

	mz_zip_zero_struct(&zip_archive);

	status = mz_zip_reader_init_mem(&zip_archive, data, size, 0);
	if (status == MZ_FALSE)
		return -1;

	LOG("Found a ZIP file (VPK), extracting eboot.bin.");

	eboot_bin = mz_zip_reader_extract_file_to_heap(&zip_archive, "eboot.bin", &uncompressed_size, 0);
	if (!eboot_bin) {
		mz_zip_reader_end(&zip_archive);
		return -1;
	}

	ret = load_executable(jit, eboot_bin, entry);

	mz_free(eboot_bin);
	mz_zip_reader_end(&zip_archive);

	return ret;
}


static int load_executable(Jit *jit, const uint8_t *data, void **entry)
{
	if (data[0] == ELFMAG0) {
		if (data[1] == ELFMAG1 && data[2] == ELFMAG2 && data[3] == ELFMAG3) {
			LOG("Found an ELF, loading.");
			if (load_elf(jit, data, entry) < 0) {
				LOG("Cannot load ELF.");
				return -1;
			}
		}
	} else if (data[0] == SCEMAG0) {
		if (data[1] == SCEMAG1 && data[2] == SCEMAG2 && data[3] == SCEMAG3) {
			LOG("Found a SELF, loading.");
			if (load_self(jit, data, entry) < 0) {
				LOG("Cannot load SELF.");
				return -1;
			}
		}
	} else {
		/* Unsupported executable */
		return -1;
	}

	return 0;
}

static int load_self(Jit *jit, const void *data, void **entry)
{
	const SCE_header *self_header = data;
	const Elf32_Ehdr *elf_hdr = (void *)((char *)data + self_header->elf_offset);
	const Elf32_Phdr *prog_hdrs = (void *)((char *)data + self_header->phdr_offset);
	const segment_info *seg_infos = (void *)((char *)data + self_header->section_info_offset);
	segment_info_t *segments;
	const Elf32_Phdr *seg_header;
	uint8_t *seg_bytes;
	void *uncompressed;
	mz_ulong dest_bytes;
	int ret;

	if (self_header->magic != 0x00454353) {
		LOG("SELF is corrupt or encrypted. Decryption is not yet supported.");
		return -1;
	}

	if (self_header->version != 3) {
		LOG("SELF version 0x%" PRIx32 " is not supported.", self_header->version);
		return -1;
	}

	if (self_header->header_type != 1) {
		LOG("SELF header type 0x%x is not supported.", self_header->header_type);
		return -1;
	}

	LOG("Loading SELF: ELF type: 0x%x, header_type: 0x%x, self_filesize: 0x%llx, self_offset: 0x%llx",
		elf_hdr->e_type, self_header->header_type, self_header->self_filesize, self_header->self_offset);

	/* Make sure it contains a PSVita ELF */
	if (elf_check_vita_header(elf_hdr) < 0) {
		LOG("Check header failed.");
		return -1;
	}

	segments = calloc(elf_hdr->e_phnum, sizeof(segment_info_t));
	if (!segments)
		return -1;

	for (Elf32_Half i = 0; i < elf_hdr->e_phnum; i++) {
		seg_header = &prog_hdrs[i];
		seg_bytes = (uint8_t *)data + self_header->header_len + seg_header->p_offset;

		LOG("Segment %i: encryption: %lld, compression: %lld", i,
		    seg_infos[i].encryption, seg_infos[i].compression);

		segments[i].p_type = seg_header->p_type;
		segments[i].p_filesz = seg_header->p_filesz;
		segments[i].p_memsz = seg_header->p_memsz;
		segments[i].p_flags = seg_header->p_flags;
		segments[i].p_align = seg_header->p_align;

		if (seg_header->p_type == PT_LOAD) {
			if (seg_header->p_memsz != 0) {
				if (seg_infos[i].compression == 2) {
					dest_bytes = seg_header->p_filesz;
					uncompressed = malloc(seg_header->p_memsz);
					if (!uncompressed) {
						ret = -1;
						goto done;
					}

					ret = mz_uncompress(uncompressed, &dest_bytes,
							    (uint8_t *)data + seg_infos[i].offset,
							    seg_infos[i].length);
					if (ret != MZ_OK) {
						ret = -1;
						goto done;
					}

					segments[i].src_data = uncompressed;
					segments[i].needs_free = true;
				} else {
					segments[i].src_data = seg_bytes;

				}
			}
		} else if (seg_header->p_type == PT_LOOS) {
			if (seg_infos[i].compression == 2) {
				dest_bytes = seg_header->p_filesz;
				uncompressed = malloc(seg_header->p_filesz);
				if (!uncompressed) {
					ret = -1;
					goto done;
				}

				ret = mz_uncompress(uncompressed, &dest_bytes,
						    (uint8_t *)data + seg_infos[i].offset,
						    seg_infos[i].length);
				if (ret != MZ_OK) {
					ret = -1;
					goto done;
				}

				segments[i].src_data = uncompressed;
				segments[i].needs_free = true;
			} else {
				segments[i].src_data = seg_bytes;
			}
		} else {
			LOG("Unknown segment type 0x%" PRIx32, seg_header->p_type);
		}
	}

	ret = load_segments(jit, entry, elf_hdr->e_entry, segments, elf_hdr->e_phnum);

done:
	for (Elf32_Half i = 0; i < elf_hdr->e_phnum; i++) {
		if (segments[i].needs_free)
			free(segments[i].src_data);
	}

	free(segments);

	return ret;
}

static int load_elf(Jit *jit, const void *data, void **entry)
{
	const Elf32_Ehdr *elf_hdr = data;
	Elf32_Phdr *prog_hdrs;
	int ret;

	/* Make sure it's a PSVita ELF */
	if (elf_check_vita_header(elf_hdr) < 0) {
		LOG("Check header failed.");
		return -1;
	}

	LOG("Found %u program segments.", elf_hdr->e_phnum);
	if (elf_hdr->e_phnum < 1) {
		LOG("No program sections to load!");
		return -1;
	}

	/* Read ELF program headers */
	LOG("Reading program headers.");
	prog_hdrs = (void *)((uintptr_t)data + elf_hdr->e_phoff);

	segment_info_t *segments = calloc(elf_hdr->e_phnum, sizeof(segment_info_t));

	for (Elf32_Half i = 0; i < elf_hdr->e_phnum; i++) {
		segments[i].src_data = (char *)data + prog_hdrs[i].p_offset;
		segments[i].p_type = prog_hdrs[i].p_type;
		segments[i].p_filesz = prog_hdrs[i].p_filesz;
		segments[i].p_memsz = prog_hdrs[i].p_memsz;
		segments[i].p_flags = prog_hdrs[i].p_flags;
		segments[i].p_align = prog_hdrs[i].p_align;
	}

	ret = load_segments(jit, entry, elf_hdr->e_entry, segments, elf_hdr->e_phnum);

	free(segments);

	return ret;
}

static int load_segments(Jit *jit, void **entry, Elf32_Addr e_entry, segment_info_t *segments, int num_segments)
{
	uint32_t code_size = 0, data_size = 0;
	uint32_t code_offset = 0, data_offset = 0;
	uintptr_t data_addr, code_rw_addr, code_rx_addr;
	uintptr_t addr_rw, addr_rx;
	uint32_t length;
	sce_module_info_raw *mod_info;
	const char *imp_name;
	int mod_info_idx;
	Result res;

	/* Calculate total code and data sizes */
	for (int i = 0; i < num_segments; i++) {
		if (segments[i].p_type == PT_LOAD) {
			length = segments[i].p_memsz + segments[i].p_align;
			if ((segments[i].p_flags & PF_X) == PF_X)
				code_size += length;
			else
				data_size += length;
		}
	}

	LOG("Total needed code size: 0x%" PRIx32, code_size);
	LOG("Total needed data size: 0x%" PRIx32, data_size);

	res = jitCreate(jit, code_size);
	if (R_FAILED(res)) {
		LOG("jitCreate failed: 0x%" PRIx32, res);
		return -1;
	}

	data_addr = (uintptr_t)malloc(data_size);
	if (!data_addr) {
		LOG("Could not allocate buffer for the data segment");
		goto err_jit_close;
	}

	code_rw_addr = (uintptr_t)jitGetRwAddr(jit);
	code_rx_addr = (uintptr_t)jitGetRxAddr(jit);

	LOG("JIT code RW addr: %p", (void *)code_rw_addr);
	LOG("JIT code RX addr: %p", (void *)code_rx_addr);
	LOG("Data        addr: %p", (void *)data_addr);

	res = jitTransitionToWritable(jit);
	if (R_FAILED(res)) {
		LOG("Could not transition JIT to writable: 0x%" PRIx32, res);
		goto err_free_data;
	}

	for (int i = 0; i < num_segments; i++) {
		if (segments[i].p_type == PT_LOAD) {
			LOG("Found loadable segment (%u)", i);
			LOG("  p_filesz: 0x%" PRIx32, segments[i].p_filesz);
			LOG("  p_memsz:  0x%" PRIx32, segments[i].p_align);
			LOG("  p_align:  0x%" PRIx32, segments[i].p_align);

			if ((segments[i].p_flags & PF_X) == PF_X) {
				length = ALIGN(code_rx_addr, segments[i].p_align) - code_rx_addr;
				addr_rx = code_rx_addr + code_offset;
				addr_rw = code_rw_addr + code_offset;
				code_offset += segments[i].p_memsz;
			} else {
				length = ALIGN(data_addr, segments[i].p_align) - data_addr;
				addr_rw = addr_rx = data_addr + data_offset;
				data_offset += segments[i].p_memsz;
			}

			segments[i].rx_addr = addr_rx;
			segments[i].rw_addr = addr_rw;

			if ((segments[i].p_flags & PF_X) == PF_X) {
				LOG("Code segment loaded at %p for RW, %p for RX",
				    (void *)addr_rw, (void *)addr_rx);
			} else {
				LOG("Data segment loaded at %p ", (void *)addr_rw);
			}

			memcpy((void *)segments[i].rw_addr, segments[i].src_data, segments[i].p_filesz);
			memset((char *)segments[i].rw_addr + segments[i].p_filesz, 0, segments[i].p_memsz - segments[i].p_filesz);
		} else if (segments[i].p_type == PT_SCE_RELA) {
			LOG("Found relocations segment (%u)", i);
			relocate(segments[i].src_data, segments[i].p_filesz, segments);
		} else {
			LOG("Segment %u is not loadable. Skipping.", i);
			continue;
		}
	}

	/* Get module info */
	LOG("Getting module info.");
	if ((mod_info_idx = elf_get_sce_module_info(e_entry, segments, &mod_info)) < 0) {
		LOG("Cannot find module info section.");
		goto err_free_data;
	}
	LOG("Module name: %s", mod_info->name);
	LOG("  export table offset: 0x%" PRIx32, mod_info->export_top);
	LOG("  import table offset: 0x%" PRIx32, mod_info->import_top);
	LOG("  tls start: 0x%" PRIx32, mod_info->tls_start);
	LOG("  tls filesz: 0x%" PRIx32, mod_info->tls_filesz);
	LOG("  tls memsz: 0x%" PRIx32, mod_info->tls_memsz);

	/* Resolve NIDs */
	sce_module_imports_u_t *import = (void *)(segments[mod_info_idx].rw_addr + mod_info->import_top);
	void *end = (void *)(segments[mod_info_idx].rw_addr + mod_info->import_end);

	for (; (void *)import < end; import = IMP_GET_NEXT(import)) {
		imp_name = (void *)CODE_RX_TO_RW_ADDR(code_rx_addr, code_rw_addr, IMP_GET_NAME(import));
		LOG("Resolving imports for %s (NID: 0x%08" PRIx32 ")", imp_name, IMP_GET_NID(import));
		if (resolve_imports(code_rx_addr, code_rw_addr, import) < 0) {
			LOG("Failed to resolve imports for %s", imp_name);
			goto err_free_data;
		}
	}

	/* Find the entry point (address belonging to the RX JIT area) */
	*entry = (char *)segments[mod_info_idx].rx_addr + mod_info->module_start;
	if (*entry == NULL) {
		LOG("Invalid module entry function.");
		goto err_free_data;
	}

	res = jitTransitionToExecutable(jit);
	if (R_FAILED(res)) {
		LOG("Could not transition JIT to executable: 0x%" PRIx32, res);
		goto err_free_data;
	}

	return 0;

err_free_data:
	free((void *)data_addr);
err_jit_close:
	jitClose(jit);

	return -1;
}

static int resolve_imports(uintptr_t rx_base, uintptr_t rw_base, sce_module_imports_u_t *import)
{
	const void *addr;
	uint32_t nid;
	uint32_t *stub;
	uint32_t lib_nid = IMP_GET_NID(import);

	/* The module imports struct contains pointers to entries to the RX JIT area because
	 * it has already been relocated, so we have to convert them to RW addresses */

	for (uint32_t i = 0; i < IMP_GET_FUNC_COUNT(import); i++) {
		nid = ((uint32_t *)CODE_RX_TO_RW_ADDR(rx_base, rw_base, IMP_GET_FUNC_TABLE(import)))[i];
		stub = (void *)CODE_RX_TO_RW_ADDR(rx_base, rw_base, IMP_GET_FUNC_ENTRIES(import)[i]);
		addr = module_get_func_export(lib_nid, nid);

		if (addr) {
			stub[0] = arm_encode_movw(12, (uint16_t)(uintptr_t)addr);
			stub[1] = arm_encode_movt(12, (uint16_t)(((uintptr_t)addr) >> 16));
			stub[2] = arm_encode_bx(12);
		} else {
			stub[0] = arm_encode_movw(0, (uint16_t)SCE_KERNEL_ERROR_MODULEMGR_NO_FUNC_NID);
			stub[1] = arm_encode_movt(0, (uint16_t)(SCE_KERNEL_ERROR_MODULEMGR_NO_FUNC_NID >> 16));
			stub[2] = arm_encode_ret();
			LOG("  Could not resolve NID 0x%08" PRIx32 ", export not found!", nid);
		}
	}

	for (uint32_t i = 0; i < IMP_GET_VARS_COUNT(import); i++) {
		nid = ((uint32_t *)CODE_RX_TO_RW_ADDR(rx_base, rw_base, IMP_GET_VARS_TABLE(import)))[i];
		IF_VERBOSE LOG("  Trying to resolve variable NID 0x%08" PRIx32, nid);
		/* TODO */
		LOG("    Variable NID resolving currently not implemented!");
	}

	return 0;
}

static int relocate(const void *reloc, uint32_t size, segment_info_t *segs)
{
	SCE_Rel *entry;
	uint32_t pos;
	uint16_t r_code = 0;
	uint32_t r_offset;
	uint32_t r_addend;
	uint8_t r_symseg;
	uint8_t r_datseg;
	int32_t offset;
	uint32_t symval, loc_rw, loc_rx;
	uint32_t upper, lower, sign, j1, j2;
	uint32_t value = 0;

	pos = 0;
	while (pos < size) {
		// get entry
		entry = (SCE_Rel *)((char *)reloc + pos);
		if (SCE_REL_IS_SHORT(*entry)) {
			r_offset = SCE_REL_SHORT_OFFSET(entry->r_short);
			r_addend = SCE_REL_SHORT_ADDEND(entry->r_short);
			pos += 8;
		} else {
			r_offset = SCE_REL_LONG_OFFSET(entry->r_long);
			r_addend = SCE_REL_LONG_ADDEND(entry->r_long);
			if (SCE_REL_LONG_CODE2(entry->r_long))
				IF_VERBOSE LOG("Code2 ignored for relocation at 0x%" PRIx32, pos);
			pos += 12;
		}

		// get values
		r_symseg = SCE_REL_SYMSEG(*entry);
		r_datseg = SCE_REL_DATSEG(*entry);
		symval = r_symseg == 15 ? 0 : (uint32_t)segs[r_symseg].rx_addr;
		loc_rw = (uint32_t)segs[r_datseg].rw_addr + r_offset;
		loc_rx = (uint32_t)segs[r_datseg].rx_addr + r_offset;

		// perform relocation
		// taken from linux/arch/arm/kernel/module.c of Linux Kernel 4.0
		switch (SCE_REL_CODE(*entry)) {
		case R_ARM_V4BX: {
			/* Preserve Rm and the condition code. Alter
			 * other bits to re-code instruction as
			 * MOV PC,Rm.
			 */
			value = (*(uint32_t *)loc_rw & 0xf000000f) | 0x01a0f000;
		}
		break;
		case R_ARM_ABS32:
		case R_ARM_TARGET1: {
			value = r_addend + symval;
		}
		break;
		case R_ARM_REL32:
		case R_ARM_TARGET2: {
			value = r_addend + symval - loc_rx;
		}
		break;
		case R_ARM_THM_PC22: {
			upper = *(uint16_t *)loc_rw;
			lower = *(uint16_t *)(loc_rw + 2);

			/*
			 * 25 bit signed address range (Thumb-2 BL and B.W
			 * instructions):
			 *   S:I1:I2:imm10:imm11:0
			 * where:
			 *   S     = upper[10]   = offset[24]
			 *   I1    = ~(J1 ^ S)   = offset[23]
			 *   I2    = ~(J2 ^ S)   = offset[22]
			 *   imm10 = upper[9:0]  = offset[21:12]
			 *   imm11 = lower[10:0] = offset[11:1]
			 *   J1    = lower[13]
			 *   J2    = lower[11]
			 */
			sign = (upper >> 10) & 1;
			j1 = (lower >> 13) & 1;
			j2 = (lower >> 11) & 1;
			offset = r_addend + symval - loc_rx;

			if (offset <= (int32_t)0xff000000 ||
			    offset >= (int32_t)0x01000000) {
				LOG("reloc 0x%" PRIx32 " out of range: 0x%08" PRIx32, pos, symval);
				break;
			}

			sign = (offset >> 24) & 1;
			j1 = sign ^ (~(offset >> 23) & 1);
			j2 = sign ^ (~(offset >> 22) & 1);
			upper = (uint16_t)((upper & 0xf800) | (sign << 10) |
					   ((offset >> 12) & 0x03ff));
			lower = (uint16_t)((lower & 0xd000) |
					   (j1 << 13) | (j2 << 11) |
					   ((offset >> 1) & 0x07ff));

			value = ((uint32_t)lower << 16) | upper;
		}
		break;
		case R_ARM_CALL:
		case R_ARM_JUMP24: {
			offset = r_addend + symval - loc_rx;
			if (offset <= (int32_t)0xfe000000 ||
			    offset >= (int32_t)0x02000000) {
				LOG("reloc 0x%" PRIx32 " out of range: 0x%08" PRIx32, pos, symval);
				break;
			}

			offset >>= 2;
			offset &= 0x00ffffff;

			value = (*(uint32_t *)loc_rw & 0xff000000) | offset;
		}
		break;
		case R_ARM_PREL31: {
			offset = r_addend + symval - loc_rx;
			value = offset & 0x7fffffff;
		}
		break;
		case R_ARM_MOVW_ABS_NC:
		case R_ARM_MOVT_ABS: {
			offset = symval + r_addend;
			if (SCE_REL_CODE(*entry) == R_ARM_MOVT_ABS)
				offset >>= 16;

			value = *(uint32_t *)loc_rw;
			value &= 0xfff0f000;
			value |= ((offset & 0xf000) << 4) |
				 (offset & 0x0fff);
		}
		break;
		case R_ARM_THM_MOVW_ABS_NC:
		case R_ARM_THM_MOVT_ABS: {
			upper = *(uint16_t *)loc_rw;
			lower = *(uint16_t *)(loc_rw + 2);

			/*
			 * MOVT/MOVW instructions encoding in Thumb-2:
			 *
			 * i    = upper[10]
			 * imm4 = upper[3:0]
			 * imm3 = lower[14:12]
			 * imm8 = lower[7:0]
			 *
			 * imm16 = imm4:i:imm3:imm8
			 */
			offset = r_addend + symval;

			if (SCE_REL_CODE(*entry) == R_ARM_THM_MOVT_ABS)
				offset >>= 16;

			upper = (uint16_t)((upper & 0xfbf0) |
					   ((offset & 0xf000) >> 12) |
					   ((offset & 0x0800) >> 1));
			lower = (uint16_t)((lower & 0x8f00) |
					   ((offset & 0x0700) << 4) |
					   (offset & 0x00ff));

			value = ((uint32_t)lower << 16) | upper;
		}
		break;
		default: {
			LOG("Unknown relocation code %u at 0x%" PRIx32, r_code, pos);
		}
		case R_ARM_NONE:
			continue;
		}

		memcpy((char *)segs[r_datseg].rw_addr + r_offset, &value, sizeof(value));
	}

	return 0;
}
