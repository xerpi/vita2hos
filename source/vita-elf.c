#include <stdlib.h>
#include <string.h>
#include <switch.h>
#include <psp2/kernel/error.h>
#include "arm-encode.h"
#include "log.h"
#include "vita-elf.h"
#include "utils.h"

#define CODE_RX_TO_RW_ADDR(rx_base, rw_base, rx_addr) \
	((((uintptr_t)rx_addr - (uintptr_t)rx_base)) + (uintptr_t)rw_base)

int resolve_imports(uintptr_t rx_base, uintptr_t rw_base, module_imports_t *import);
int relocate(void *reloc, uint32_t size, Elf32_Phdr *segs);

extern void sceKernelAllocMemBlock();
extern void sceKernelGetMemBlockBase();
extern void sceDisplaySetFrameBuf();
extern void sceCtrlPeekBufferPositive();

static const struct {
	uint32_t nid;
	void *func;
} stub_map[] = {
	{0xB9D5EBDE, sceKernelAllocMemBlock},
	{0xB8EF5818, sceKernelGetMemBlockBase},
	{0x7A410B64, sceDisplaySetFrameBuf},
	{0xA9C3CED6, sceCtrlPeekBufferPositive},
};

static inline const void *get_stub_func(uint32_t nid)
{
	for (int i = 0; i < ARRAY_SIZE(stub_map); i++) {
		if (stub_map[i].nid == nid)
			return stub_map[i].func;
	}
	return NULL;
}

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

static int elf_get_sce_module_info(const Elf32_Ehdr *elf_hdr, const Elf32_Phdr *elf_phdrs, module_info_t **mod_info)
{
	uint32_t offset;
	uint32_t index;

	index = ((uint32_t)elf_hdr->e_entry & 0xC0000000) >> 30;
	offset = (uint32_t)elf_hdr->e_entry & 0x3FFFFFFF;

	if (elf_phdrs[index].p_vaddr == 0) {
		LOG("Invalid segment index %ld\n", index);
		return -1;
	}

	*mod_info = (module_info_t *)((char *)elf_phdrs[index].p_vaddr + offset);

	return index;
}

int load_exe(Jit *jit, const char *filename, void **entry)
{
	void *data;
	uint32_t size;
	uint8_t *magic;
	uint32_t offset;

	LOG("Opening %s for reading.", filename);
	if (utils_load_file(filename, &data, &size) != 0) {
		LOG("Cannot load file.");
		return -1;
	}

	magic = (uint8_t *)data;

	if (magic[0] == ELFMAG0) {
		if (magic[1] == ELFMAG1 && magic[2] == ELFMAG2 && magic[3] == ELFMAG3) {
			LOG("Found an ELF, loading.");
			if (load_elf(jit, data, entry) < 0) {
				LOG("Cannot load ELF.");
				return -1;
			}
		}
	} else if (magic[0] == SCEMAG0) {
		if (magic[1] == SCEMAG1 && magic[2] == SCEMAG2 && magic[3] == SCEMAG3) {
			offset = ((uint32_t *)data)[4];
			LOG("Loading FSELF. ELF offset at 0x%08lX", offset);
			if (load_elf(jit, (void *)((uintptr_t)data + offset), entry) < 0) {
				LOG("Cannot load FSELF.");
				return -1;
			}
		}
	} else {
		LOG("Invalid magic.");
		return -1;
	}

	free(data);
	return 0;
}

int load_elf(Jit *jit, const void *data, void **entry)
{
	const Elf32_Ehdr *elf_hdr = data;
	Elf32_Phdr *prog_hdrs;
	uint32_t code_size = 0, data_size = 0;
	uint32_t code_offset = 0, data_offset = 0;
	uintptr_t data_addr, code_rw_addr, code_rx_addr;
	uintptr_t addr_rw, addr_rx;
	uint32_t length;
	module_info_t *mod_info;
	const char *imp_name;
	int mod_info_idx;
	Result res;

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
	IF_VERBOSE LOG("Reading program headers.");
	prog_hdrs = (void *)((uintptr_t)data + elf_hdr->e_phoff);

	/* Calculate total code and data sizes */
	for (Elf32_Half i = 0; i < elf_hdr->e_phnum; i++) {
		if (prog_hdrs[i].p_type == PT_LOAD) {
			length = prog_hdrs[i].p_memsz + prog_hdrs[i].p_align;
			if ((prog_hdrs[i].p_flags & PF_X) == PF_X)
				code_size += length;
			else
				data_size += length;
		}
	}

	LOG("Total needed code size: 0x%lx", code_size);
	LOG("Total needed data size: 0x%lx", data_size);

	res = jitCreate(jit, code_size);
	if (R_FAILED(res)) {
		LOG("jitCreate failed: 0x%lx", res);
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
		LOG("Could not transition JIT to writable: 0x%lx", res);
		goto err_free_data;
	}

	for (Elf32_Half i = 0; i < elf_hdr->e_phnum; i++) {
		if (prog_hdrs[i].p_type == PT_LOAD) {
			LOG("Found loadable segment (%u)", i);
			LOG("  p_offset: 0x%lx", prog_hdrs[i].p_offset);
			LOG("  p_filesz: 0x%lx", prog_hdrs[i].p_filesz);
			LOG("  p_memsz:  0x%lx", prog_hdrs[i].p_align);
			LOG("  p_align:  0x%lx", prog_hdrs[i].p_align);

			if ((prog_hdrs[i].p_flags & PF_X) == PF_X) {
				length = ALIGN(code_rx_addr, prog_hdrs[i].p_align) - code_rx_addr;
				addr_rx = code_rx_addr + code_offset;
				addr_rw = code_rw_addr + code_offset;
				code_offset += prog_hdrs[i].p_memsz;
			} else {
				length = ALIGN(data_addr, prog_hdrs[i].p_align) - data_addr;
				addr_rw = addr_rx = data_addr + data_offset;
				data_offset += prog_hdrs[i].p_memsz;
			}

			/* If it's a code segment, store the RX JIT area address to p_paddr */
			prog_hdrs[i].p_paddr = addr_rx;
			prog_hdrs[i].p_vaddr = addr_rw;

			if ((prog_hdrs[i].p_flags & PF_X) == PF_X) {
				LOG("Code segment loaded at %p for RW, %p for RX",
				    (void *)addr_rw, (void *)addr_rx);
			} else {
				LOG("Data segment loaded at %p ", (void *)addr_rw);
			}

			memcpy((void *)prog_hdrs[i].p_vaddr, (char *)data + prog_hdrs[i].p_offset, prog_hdrs[i].p_filesz);
			memset((char *)prog_hdrs[i].p_vaddr + prog_hdrs[i].p_filesz, 0, prog_hdrs[i].p_memsz - prog_hdrs[i].p_filesz);
		} else if (prog_hdrs[i].p_type == PT_SCE_RELA) {
			LOG("Found relocations segment (%u)", i);
			relocate((char *)data + prog_hdrs[i].p_offset, prog_hdrs[i].p_filesz, prog_hdrs);
		} else {
			LOG("Segment %u is not loadable. Skipping.", i);
			continue;
		}
	}

	/* Get module info */
	LOG("Getting module info.");
	if ((mod_info_idx = elf_get_sce_module_info(elf_hdr, prog_hdrs, &mod_info)) < 0) {
		LOG("Cannot find module info section.");
		goto err_free_data;
	}
	LOG("Module name: %s, export table offset: 0x%08lX, import table offset: 0x%08lX",
	    mod_info->modname, mod_info->ent_top, mod_info->stub_top);

	/* Resolve NIDs */
	module_imports_t *import = (void *)(prog_hdrs[mod_info_idx].p_vaddr + mod_info->stub_top);
	void *end = (void *)(prog_hdrs[mod_info_idx].p_vaddr + mod_info->stub_end);

	for (; (void *)import < end; import = IMP_GET_NEXT(import)) {
		imp_name = (void *)CODE_RX_TO_RW_ADDR(code_rx_addr, code_rw_addr, IMP_GET_NAME(import));
		LOG("Resolving imports for %s", imp_name);
		if (resolve_imports(code_rx_addr, code_rw_addr, import) < 0) {
			LOG("Failed to resolve imports for %s", imp_name);
			goto err_free_data;
		}
	}

	/* Find the entry point (address belonging to the RX JIT area) */
	*entry = (char *)prog_hdrs[mod_info_idx].p_paddr + mod_info->mod_start;
	if (*entry == NULL) {
		LOG("Invalid module entry function.\n");
		goto err_free_data;
	}

	res = jitTransitionToExecutable(jit);
	if (R_FAILED(res)) {
		LOG("Could not transition JIT to executable: 0x%lx", res);
		goto err_free_data;
	}

	return 0;

err_free_data:
	free((void *)data_addr);
err_jit_close:
	jitClose(jit);

	return -1;
}

int resolve_imports(uintptr_t rx_base, uintptr_t rw_base, module_imports_t *import)
{
	uint32_t nid;
	uint32_t *stub;

	/* The module imports struct contains pointers to entries to the RX JIT area because
	 * it has already been relocated, so we have to convert them to RW addresses */

	for (uint32_t i = 0; i < IMP_GET_FUNC_COUNT(import); i++) {
		nid = ((uint32_t *)CODE_RX_TO_RW_ADDR(rx_base, rw_base, IMP_GET_FUNC_TABLE(import)))[i];
		stub = (void *)CODE_RX_TO_RW_ADDR(rx_base, rw_base, IMP_GET_FUNC_ENTRIES(import)[i]);

		IF_VERBOSE LOG("  Trying to resolve function NID 0x%08lX", nid);
		IF_VERBOSE LOG("    Stub located at: %p", stub);

		uintptr_t export_addr = (uintptr_t)get_stub_func(nid);
		if (export_addr) {
			stub[0] = arm_encode_movw(12, (uint16_t)export_addr);
			stub[1] = arm_encode_movt(12, (uint16_t)(export_addr >> 16));
			stub[2] = arm_encode_bx(12);
			LOG("    NID resolved successfully!");
		} else {
			stub[0] = arm_encode_movw(0, (uint16_t)SCE_KERNEL_ERROR_MODULEMGR_NO_FUNC_NID);
			stub[1] = arm_encode_movt(0, (uint16_t)(SCE_KERNEL_ERROR_MODULEMGR_NO_FUNC_NID >> 16));
			stub[2] = arm_encode_ret();
			LOG("    Could not resolve NID, export not found!");
		}
	}

	for (uint32_t i = 0; i < IMP_GET_VARS_COUNT(import); i++) {
		nid = ((uint32_t *)CODE_RX_TO_RW_ADDR(rx_base, rw_base, IMP_GET_VARS_TABLE(import)))[i];
		IF_VERBOSE LOG("  Trying to resolve variable NID 0x%08lX", nid);
		/* TODO */
		LOG("    Variable NID resolving currently not implemented!");
	}

	return 0;
}

int relocate(void *reloc, uint32_t size, Elf32_Phdr *segs)
{
	sce_reloc_t *entry;
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
		entry = (sce_reloc_t *)((char *)reloc + pos);
		if (SCE_RELOC_IS_SHORT(*entry)) {
			r_offset = SCE_RELOC_SHORT_OFFSET(entry->r_short);
			r_addend = SCE_RELOC_SHORT_ADDEND(entry->r_short);
			pos += 8;
		} else {
			r_offset = SCE_RELOC_LONG_OFFSET(entry->r_long);
			r_addend = SCE_RELOC_LONG_ADDEND(entry->r_long);
			if (SCE_RELOC_LONG_CODE2(entry->r_long))
				IF_VERBOSE LOG("Code2 ignored for relocation at %lx.", pos);
			pos += 12;
		}

		// get values
		r_symseg = SCE_RELOC_SYMSEG(*entry);
		r_datseg = SCE_RELOC_DATSEG(*entry);
		/* For the code segment, p_paddr contains the RX JIT area address */
		symval = r_symseg == 15 ? 0 : (uint32_t)segs[r_symseg].p_paddr;
		loc_rw = (uint32_t)segs[r_datseg].p_vaddr + r_offset;
		loc_rx = (uint32_t)segs[r_datseg].p_paddr + r_offset;

		// perform relocation
		// taken from linux/arch/arm/kernel/module.c of Linux Kernel 4.0
		switch (SCE_RELOC_CODE(*entry)) {
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
				LOG("reloc %lx out of range: 0x%08lX", pos, symval);
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
				LOG("reloc %lx out of range: 0x%08lX", pos, symval);
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
			if (SCE_RELOC_CODE(*entry) == R_ARM_MOVT_ABS)
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

			if (SCE_RELOC_CODE(*entry) == R_ARM_THM_MOVT_ABS)
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
			LOG("Unknown relocation code %u at %lx", r_code, pos);
		}
		case R_ARM_NONE:
			continue;
		}

		memcpy((char *)segs[r_datseg].p_vaddr + r_offset, &value, sizeof(value));
	}

	return 0;
}
