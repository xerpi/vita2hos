/* This file gets included multiple times to generate the host-visible and target-visible versions of each struct */

#if defined(SCE_ELF_DEFS_HOST)
# define SCE_TYPE(type) type ## _t
# define SCE_PTR(type) type
#elif defined(SCE_ELF_DEFS_TARGET)
# define SCE_TYPE(type) type ## _raw
# define SCE_PTR(type) uint32_t
#else
# error "Do not include sce-elf-defs.h directly!  Include sce-elf.h!"
#endif

#include <stdint.h>

struct SCE_TYPE(sce_module_exports);
struct SCE_TYPE(sce_module_imports);

typedef struct SCE_TYPE(sce_module_info) {
	uint16_t attributes;
	uint16_t version;			/* Set to 0x0101 */
	char name[27];				/* Name of the library */
	uint8_t type;				/* 0x0 for executable, 0x6 for PRX */
	SCE_PTR(const void *) gp_value;
	SCE_PTR(struct sce_module_exports_t *)
		export_top;			/* Offset to start of export table */
	SCE_PTR(struct sce_module_exports_t *)
		export_end;			/* Offset to end of export table */
	SCE_PTR(struct sce_module_imports_t *)
		import_top;			/* Offset to start of import table */
	SCE_PTR(struct sce_module_imports_t *)
		import_end;			/* Offset to end of import table */
	uint32_t module_nid;			/* NID of this module */
	uint32_t tls_start;
	uint32_t tls_filesz;
	uint32_t tls_memsz;
	SCE_PTR(const void *) module_start;	/* Offset to function to run when library is started, 0 to disable */
	SCE_PTR(const void *) module_stop;	/* Offset to function to run when library is exiting, 0 to disable */
	SCE_PTR(const void *) exidx_top;	/* Offset to start of ARM EXIDX (optional) */
	SCE_PTR(const void *) exidx_end;	/* Offset to end of ARM EXIDX (optional) */
	SCE_PTR(const void *) extab_top;	/* Offset to start of ARM EXTAB (optional) */
	SCE_PTR(const void *) extab_end;	/* Offset to end of ARM EXTAB (optional */

	// Included module_sdk_version export in module_info
	uint32_t module_sdk_version;        /* SDK version */
} SCE_TYPE(sce_module_info);

typedef struct SCE_TYPE(sce_module_exports) {
	uint16_t size;				/* Size of this struct, set to 0x20 */
	uint16_t version;			/* 0x1 for normal export, 0x0 for main module export */
	uint16_t flags;				/* 0x1 for normal export, 0x8000 for main module export */
	uint16_t num_syms_funcs;		/* Number of function exports */
	uint32_t num_syms_vars;			/* Number of variable exports */
	uint32_t num_syms_tls_vars;     /* Number of TLS variable exports */
	uint32_t library_nid;			/* NID of this library */
	SCE_PTR(const char *) library_name;	/* Pointer to name of this library */
	SCE_PTR(uint32_t *) nid_table;		/* Pointer to array of 32-bit NIDs to export */
	SCE_PTR(const void **) entry_table;	/* Pointer to array of data pointers for each NID */
} SCE_TYPE(sce_module_exports);

typedef struct SCE_TYPE(sce_module_imports) {
	uint16_t size;				/* Size of this struct, set to 0x34 */
	uint16_t version;			/* Set to 0x1 */
	uint16_t flags;				/* Set to 0x0 */
	uint16_t num_syms_funcs;		/* Number of function imports */
	uint16_t num_syms_vars;			/* Number of variable imports */
	uint16_t num_syms_tls_vars;     /* Number of TLS variable imports */

	uint32_t reserved1;
	uint32_t library_nid;			/* NID of library to import */
	SCE_PTR(const char *) library_name;	/* Pointer to name of imported library, for debugging */
	uint32_t reserved2;
	SCE_PTR(uint32_t *) func_nid_table;	/* Pointer to array of function NIDs to import */
	SCE_PTR(const void **) func_entry_table;/* Pointer to array of stub functions to fill */
	SCE_PTR(uint32_t *) var_nid_table;	/* Pointer to array of variable NIDs to import */
	SCE_PTR(const void **) var_entry_table;	/* Pointer to array of data pointers to write to */
	SCE_PTR(uint32_t *) tls_var_nid_table; /* Pointer to array of TLS variable NIDs to import */
	SCE_PTR(const void **) tls_var_entry_table; /* Pointer to array of data pointers to write to */
} SCE_TYPE(sce_module_imports);

/* alternative module imports struct with a size of 0x24 */
typedef struct SCE_TYPE(sce_module_imports_short) {
	uint16_t size;				/* Size of this struct, set to 0x24 */
	uint16_t version;			/* Set to 0x1 */
	uint16_t flags;				/* Set to 0x0 */
	uint16_t num_syms_funcs;		/* Number of function imports */
	uint16_t num_syms_vars;			/* Number of variable imports */
	uint16_t num_syms_tls_vars;		/* Number of TLS variable imports */

	uint32_t library_nid;				/* NID of library to import */
	SCE_PTR(const char *) library_name;	/* Pointer to name of imported library, for debugging */
	SCE_PTR(uint32_t *) func_nid_table;	/* Pointer to array of function NIDs to import */
	SCE_PTR(const void **) func_entry_table;	/* Pointer to array of stub functions to fill */
	SCE_PTR(uint32_t *) var_nid_table;			/* Pointer to array of variable NIDs to import */
	SCE_PTR(const void **) var_entry_table;		/* Pointer to array of data pointers to write to */
} SCE_TYPE(sce_module_imports_short);

typedef union SCE_TYPE(sce_module_imports_u) {
	uint16_t				size;
	SCE_TYPE(sce_module_imports) 		imports;
	SCE_TYPE(sce_module_imports_short) 	imports_short;
} SCE_TYPE(sce_module_imports_u);

typedef struct SCE_TYPE(sce_process_param) {
	uint32_t size;                          /* 0x34 */
	uint32_t magic;                         /* PSP2 */
	uint32_t version;                       /* Unknown, but it could be 6 */
	uint32_t fw_version;                    /* SDK vsersion */
	SCE_PTR(const char *) main_thread_name; /* Thread name pointer*/
	int32_t main_thread_priority;           /* Thread initPriority */
	uint32_t main_thread_stacksize;         /* Thread stacksize*/
	uint32_t main_thread_attribute;         /* Unknown */
	SCE_PTR(const char *) process_name;     /* Process name pointer */
	uint32_t process_preload_disabled;      /* Module load inhibit */
	uint32_t main_thread_cpu_affinity_mask; /* Unknown */
	SCE_PTR(const void *) sce_libc_param;   /* SceLibc param pointer */
	uint32_t unk;                           /* Unknown */
} SCE_TYPE(sce_process_param);

typedef struct SCE_TYPE(sce_libc_param) {
	struct {
		uint32_t       size;                /* 0x34 */
		uint32_t       unk_0x4;             /* Unknown, set to 1 */
		SCE_PTR(void *) malloc_init;        /* Initialize malloc heap */
		SCE_PTR(void *) malloc_term;        /* Terminate malloc heap */
		SCE_PTR(void *) malloc;             /* malloc replacement */
		SCE_PTR(void *) free;               /* free replacement */
		SCE_PTR(void *) calloc;             /* calloc replacement */
		SCE_PTR(void *) realloc;            /* realloc replacement */
		SCE_PTR(void *) memalign;           /* memalign replacement */
		SCE_PTR(void *) reallocalign;       /* reallocalign replacement */
		SCE_PTR(void *) malloc_stats;       /* malloc_stats replacement */
		SCE_PTR(void *) malloc_stats_fast;  /* malloc_stats_fast replacement */
		SCE_PTR(void *) malloc_usable_size; /* malloc_usable_size replacement */
	} _malloc_replace;

	struct {
		uint32_t size;                               /* 0x28 */
		uint32_t unk_0x4;                            /* Unknown, set to 1 */
		SCE_PTR(void *) operator_new;                /* new operator replacement */
		SCE_PTR(void *) operator_new_nothrow;        /* new (nothrow) operator replacement */
		SCE_PTR(void *) operator_new_arr;            /* new[] operator replacement */
		SCE_PTR(void *) operator_new_arr_nothrow;    /* new[] (nothrow) operator replacement */
		SCE_PTR(void *) operator_delete;             /* delete operator replacement */
		SCE_PTR(void *) operator_delete_nothrow;     /* delete (nothrow) operator replacement */
		SCE_PTR(void *) operator_delete_arr;         /* delete[] operator replacement */
		SCE_PTR(void *) operator_delete_arr_nothrow; /* delete[] (nothrow) operator replacement */
	} _new_replace;

	struct {
		uint32_t size;                       /* 0x18 */
		uint32_t unk_0x4;                    /* Unknown, set to 1 */
		SCE_PTR(void *) malloc_init_for_tls; /* Initialise tls malloc heap */
		SCE_PTR(void *) malloc_term_for_tls; /* Terminate tls malloc heap */
		SCE_PTR(void *) malloc_for_tls;      /* malloc_for_tls replacement */
		SCE_PTR(void *) free_for_tls;        /* free_for_tls replacement */
	} _malloc_for_tls_replace;

	uint32_t size;                                /* 0x38 */
	uint32_t unk_0x04;                            /* Unknown */
	SCE_PTR(uint32_t *) heap_size;                /* Heap size variable */
	SCE_PTR(uint32_t *) default_heap_size;        /* Default heap size variable */
	SCE_PTR(uint32_t *) heap_extended_alloc;      /* Dynamically extend heap size */
	SCE_PTR(uint32_t *) heap_delayed_alloc;       /* Allocate heap on first call to malloc */
	uint32_t fw_version;                          /* SDK version */
	uint32_t unk_0x1C;                            /* Unknown, set to 9 */
	SCE_PTR(const void *) malloc_replace;         /* malloc replacement functions */
	SCE_PTR(const void *) new_replace;            /* new replacement functions */
	SCE_PTR(uint32_t *) heap_initial_size;        /* Dynamically allocated heap initial size */
	SCE_PTR(uint32_t *) heap_unit_1mb;            /* Change alloc unit size from 64k to 1M */
	SCE_PTR(uint32_t *) heap_detect_overrun;      /* Detect heap buffer overruns */
	SCE_PTR(const void *) malloc_for_tls_replace; /* malloc_for_tls replacement functions */

	uint32_t _default_heap_size;                  /* Default SceLibc heap size - 0x40000 (256KiB) */
} SCE_TYPE(sce_libc_param);

#undef SCE_TYPE
#undef SCE_PTR
