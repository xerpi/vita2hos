#ifndef VITA_ELF_H
#define VITA_ELF_H

#include <elf.h>
#include <stdint.h>

/** @}*/
/** \name PSVita ELF object types
 *  @{
 */
#define ET_SCE_EXEC     0xFE00
#define ET_SCE_RELEXEC  0xFE04

/** @}*/
/** \name ELF ph section type
 *  @{
 */
#define PT_SCE_RELA 0x60000000

/** \name SCE Relocation
 *  @{
 */
typedef union sce_reloc {
	uint32_t       r_type;
	struct {
		uint32_t   r_opt1;
		uint32_t   r_opt2;
	} r_short;
	struct {
		uint32_t   r_type;
		uint32_t   r_addend;
		uint32_t   r_offset;
	} r_long;
} sce_reloc_t;
/** @}*/

/** \name Macros to get SCE reloc values
 *  @{
 */
#define SCE_RELOC_SHORT_OFFSET(x) (((x).r_opt1 >> 20) | ((x).r_opt2 & 0x3FF) << 12)
#define SCE_RELOC_SHORT_ADDEND(x) ((x).r_opt2 >> 10)
#define SCE_RELOC_LONG_OFFSET(x) ((x).r_offset)
#define SCE_RELOC_LONG_ADDEND(x) ((x).r_addend)
#define SCE_RELOC_LONG_CODE2(x) (((x).r_type >> 20) & 0xFF)
#define SCE_RELOC_LONG_DIST2(x) (((x).r_type >> 28) & 0xF)
#define SCE_RELOC_IS_SHORT(x) (((x).r_type) & 0xF)
#define SCE_RELOC_CODE(x) (((x).r_type >> 8) & 0xFF)
#define SCE_RELOC_SYMSEG(x) (((x).r_type >> 4) & 0xF)
#define SCE_RELOC_DATSEG(x) (((x).r_type >> 16) & 0xF)
/** @}*/

/** @}*/
/** \name SCE identification
 *  @{
 */
#define MAGIC_LEN   4
#define SCEMAG0     'S'
#define SCEMAG1     'C'
#define SCEMAG2     'E'
#define SCEMAG3     0
/** @}*/
#define UVL_SEC_MODINFO        ".sceModuleInfo.rodata" ///< Name of module information section
#define UVL_SEC_MIN_ALIGN      0x100000                ///< Alignment of each section
#define ATTR_MOD_INFO          0x8000                  ///< module_exports_t attribute

/**
 * \brief SCE module information section
 *
 * Can be found in an ELF file or loaded in
 * memory.
 */
typedef struct module_info // thanks roxfan
{
    uint16_t   modattribute;  // ??
    uint16_t   modversion;    // always 1,1?
    char       modname[27];   ///< Name of the module
    uint8_t    type;          // 6 = user-mode prx?
    void      *gp_value;     // always 0 on ARM
    uint32_t   ent_top;       // beginning of the export list (sceModuleExports array)
    uint32_t   ent_end;       // end of same
    uint32_t   stub_top;      // beginning of the import list (sceModuleStubInfo array)
    uint32_t   stub_end;      // end of same
    uint32_t   module_nid;    // ID of the PRX? seems to be unused
    uint32_t   field_38;      // unused in samples
    uint32_t   field_3C;      // I suspect these may contain TLS info
    uint32_t   field_40;      //
    uint32_t   mod_start;     // module start function; can be 0 or -1; also present in exports
    uint32_t   mod_stop;      // module stop function
    uint32_t   exidx_start;   // ARM EABI style exception tables
    uint32_t   exidx_end;     //
    uint32_t   extab_start;   //
    uint32_t   extab_end;     //
} module_info_t;

/**
 * \brief SCE module export table
 *
 * Can be found in an ELF file or loaded in
 * memory.
 */
typedef struct module_exports // thanks roxfan
{
    uint16_t   size;           // size of this structure; 0x20 for Vita 1.x
    uint8_t    lib_version[2]; //
    uint16_t   attribute;      // ?
    uint16_t   num_functions;  // number of exported functions
    uint32_t   num_vars;       // number of exported variables
    uint32_t   num_tls_vars;   // number of exported TLS variables?  <-- pretty sure wrong // yifanlu
    uint32_t   module_nid;     // NID of this specific export list; one PRX can export several names
    char      *lib_name;      // name of the export module
    uint32_t  *nid_table;     // array of 32-bit NIDs for the exports, first functions then vars
    void     **entry_table;  // array of pointers to exported functions and then variables
} module_exports_t;

/**
 * \brief SCE module import table (< 3.0 format)
 *
 * Can be found in an ELF file or loaded in
 * memory.
 */
typedef struct module_imports_2x // thanks roxfan
{
    uint16_t   size;               // size of this structure; 0x34 for Vita 1.x
    uint16_t   lib_version;        //
    uint16_t   attribute;          //
    uint16_t   num_functions;      // number of imported functions
    uint16_t   num_vars;           // number of imported variables
    uint16_t   num_tls_vars;       // number of imported TLS variables
    uint32_t   reserved1;          // ?
    uint32_t   module_nid;         // NID of the module to link to
    char      *lib_name;          // name of module
    uint32_t   reserved2;          // ?
    uint32_t  *func_nid_table;    // array of function NIDs (numFuncs)
    void     **func_entry_table; // parallel array of pointers to stubs; they're patched by the loader to jump to the final code
    uint32_t  *var_nid_table;     // NIDs of the imported variables (numVars)
    void     **var_entry_table;  // array of pointers to "ref tables" for each variable
    uint32_t  *tls_nid_table;     // NIDs of the imported TLS variables (numTlsVars)
    void     **tls_entry_table;  // array of pointers to ???
} module_imports_2x_t;

/**
 * \brief SCE module import table (>= 3.x format)
 *
 * Can be found in an ELF file or loaded in
 * memory.
 */
typedef struct module_imports_3x
{
    uint16_t   size;               // size of this structure; 0x24 for Vita 3.x
    uint16_t   lib_version;        //
    uint16_t   attribute;          //
    uint16_t   num_functions;      // number of imported functions
    uint16_t   num_vars;           // number of imported variables
    uint16_t   unknown1;
    uint32_t   module_nid;         // NID of the module to link to
    char      *lib_name;          // name of module
    uint32_t  *func_nid_table;    // array of function NIDs (numFuncs)
    void     **func_entry_table; // parallel array of pointers to stubs; they're patched by the loader to jump to the final code
    uint32_t  *var_nid_table;     // NIDs of the imported variables (numVars)
    void     **var_entry_table;  // array of pointers to "ref tables" for each variable
} module_imports_3x_t;

/**
 * \brief SCE module import table
 */
typedef union module_imports
{
    uint16_t size;
    module_imports_2x_t old_version;
    module_imports_3x_t new_version;
} module_imports_t;

#define IMP_GET_NEXT(imp) ((module_imports_t *)((char *)imp + imp->size))
#define IMP_GET_FUNC_COUNT(imp) (imp->size == sizeof (module_imports_3x_t) ? imp->new_version.num_functions : imp->old_version.num_functions)
#define IMP_GET_VARS_COUNT(imp) (imp->size == sizeof (module_imports_3x_t) ? imp->new_version.num_vars : imp->old_version.num_vars)
#define IMP_GET_NID(imp) (imp->size == sizeof (module_imports_3x_t) ? imp->new_version.module_nid : imp->old_version.module_nid)
#define IMP_GET_NAME(imp) (imp->size == sizeof (module_imports_3x_t) ? imp->new_version.lib_name : imp->old_version.lib_name)
#define IMP_GET_FUNC_TABLE(imp) (imp->size == sizeof (module_imports_3x_t) ? imp->new_version.func_nid_table : imp->old_version.func_nid_table)
#define IMP_GET_FUNC_ENTRIES(imp) (imp->size == sizeof (module_imports_3x_t) ? imp->new_version.func_entry_table : imp->old_version.func_entry_table)
#define IMP_GET_VARS_TABLE(imp) (imp->size == sizeof (module_imports_3x_t) ? imp->new_version.var_nid_table : imp->old_version.var_nid_table)
#define IMP_GET_VARS_ENTRIES(imp) (imp->size == sizeof (module_imports_3x_t) ? imp->new_version.var_entry_table : imp->old_version.var_entry_table)


/** @}*/
/** \name Functions
 *  @{
 */
int load_exe(Jit *jit, const char *filename, void **entry);
int load_elf(Jit *jit, const void *data, void **entry);


#endif
