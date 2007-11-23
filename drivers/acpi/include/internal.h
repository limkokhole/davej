
/******************************************************************************
 *
 * Name: internal.h - Internal data types used across the ACPI subsystem
 *
 *****************************************************************************/

/*
 *  Copyright (C) 2000 R. Byron Moore
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef _ACPI_INTERNAL_H
#define _ACPI_INTERNAL_H

#include "config.h"


#define WAIT_FOREVER                ((u32) -1)

typedef void*                       ACPI_MUTEX;
typedef u32                         ACPI_MUTEX_HANDLE;


/* Object descriptor types */

#define ACPI_DESC_TYPE_INTERNAL     0xAA
#define ACPI_DESC_TYPE_PARSER       0xBB
#define ACPI_DESC_TYPE_STATE        0xCC
#define ACPI_DESC_TYPE_WALK         0xDD
#define ACPI_DESC_TYPE_NAMED        0xEE


/*****************************************************************************
 *
 * Mutex typedefs and structs
 *
 ****************************************************************************/


/*
 * Predefined handles for the mutex objects used within the subsystem
 * All mutex objects are automatically created by Acpi_cm_mutex_initialize.
 * NOTE: any changes here must be reflected in the Acpi_gbl_Mutex_names table also!
 */

#define ACPI_MTX_HARDWARE           0
#define ACPI_MTX_MEMORY             1
#define ACPI_MTX_CACHES             2
#define ACPI_MTX_TABLES             3
#define ACPI_MTX_PARSER             4
#define ACPI_MTX_DISPATCHER         5
#define ACPI_MTX_INTERPRETER        6
#define ACPI_MTX_EXECUTE            7
#define ACPI_MTX_NAMESPACE          8
#define ACPI_MTX_EVENTS             9
#define ACPI_MTX_OP_REGIONS         10
#define ACPI_MTX_DEBUG_CMD_READY    11
#define ACPI_MTX_DEBUG_CMD_COMPLETE 12

#define MAX_MTX                     12
#define NUM_MTX                     MAX_MTX+1


#ifdef ACPI_DEBUG
#ifdef DEFINE_ACPI_GLOBALS

/* Names for the mutexes used in the subsystem */

static char                 *acpi_gbl_mutex_names[] =
{
	"ACPI_MTX_Hardware",
	"ACPI_MTX_Memory",
	"ACPI_MTX_Caches",
	"ACPI_MTX_Tables",
	"ACPI_MTX_Parser",
	"ACPI_MTX_Dispatcher",
	"ACPI_MTX_Interpreter",
	"ACPI_MTX_Execute",
	"ACPI_MTX_Namespace",
	"ACPI_MTX_Events",
	"ACPI_MTX_Op_regions",
	"ACPI_MTX_Debug_cmd_ready",
	"ACPI_MTX_Debug_cmd_complete"
};

#endif
#endif


/* Table for the global mutexes */

typedef struct acpi_mutex_info
{
	ACPI_MUTEX                  mutex;
	u32                         use_count;
	u8                          locked;

} ACPI_MUTEX_INFO;


/* Lock flag parameter for various interfaces */

#define ACPI_MTX_DO_NOT_LOCK        0
#define ACPI_MTX_LOCK               1


typedef u16                         ACPI_OWNER_ID;
#define OWNER_TYPE_TABLE            0x0
#define OWNER_TYPE_METHOD           0x1
#define FIRST_METHOD_ID             0x0000
#define FIRST_TABLE_ID              0x8000

/* TBD: [Restructure] get rid of the need for this! */

#define TABLE_ID_DSDT               (ACPI_OWNER_ID) 0xD1D1

/*****************************************************************************
 *
 * Namespace typedefs and structs
 *
 ****************************************************************************/


/* Operational modes of the AML interpreter/scanner */

typedef enum
{
	IMODE_LOAD_PASS1 = 0x01,
	IMODE_LOAD_PASS2 = 0x02,
	IMODE_EXECUTE   = 0x0E

} OPERATING_MODE;


/*
 * The Acpi_named_object describes a named object that appears in the AML
 * An Acpi_name_table is used to store Acpi_named_objects.
 *
 * Data_type is used to differentiate between internal descriptors, and MUST
 * be the first byte in this structure.
 */

typedef struct acpi_named_object
{
	u8                      data_type;
	u8                      type;           /* Type associated with this name */
	u8                      this_index;     /* Entry number */
	u8                      flags;
	u32                     name;           /* ACPI Name, always 4 chars per ACPI spec */


	void                    *object;        /* Pointer to attached ACPI object (optional) */
	struct acpi_name_table  *child_table;   /* Scope owned by this name (optional) */
	ACPI_OWNER_ID           owner_id;       /* ID of owner - either an ACPI table or a method */
	u16                     reference_count; /* Current count of references and children */

#ifdef _IA64
	u32                     fill1;          /* 64-bit alignment */
#endif

} ACPI_NAMED_OBJECT;


typedef struct acpi_name_table
{
	struct acpi_name_table  *next_table;
	struct acpi_name_table  *parent_table;
	ACPI_NAMED_OBJECT       *parent_entry;
	ACPI_NAMED_OBJECT       entries[1];

} ACPI_NAME_TABLE;


#define ENTRY_NOT_FOUND     NULL


/* NTE flags */

#define NTE_AML_ATTACHMENT  0x1


/*
 * ACPI Table Descriptor.  One per ACPI table
 */
typedef struct acpi_table_desc
{
	struct acpi_table_desc  *prev;
	struct acpi_table_desc  *next;
	struct acpi_table_desc  *installed_desc;
	ACPI_TABLE_HEADER       *pointer;
	void                    *base_pointer;
	u8                      *aml_pointer;
	u32                     aml_length;
	u32                     length;
	u32                     count;
	ACPI_OWNER_ID           table_id;
	u8                      type;
	u8                      allocation;
	u8                      loaded_into_namespace;

} ACPI_TABLE_DESC;


typedef struct
{
	char                    *search_for;
	ACPI_HANDLE             *list;
	s32                     *count;

} FIND_CONTEXT;


typedef struct
{
	ACPI_NAME_TABLE        *name_table;
	u32                     position;
	u8                      table_full;

} NS_SEARCH_DATA;


/*
 * Predefined Namespace items
 */
#define ACPI_MAX_ADDRESS_SPACE      255
#define ACPI_NUM_ADDRESS_SPACES     256


typedef struct
{
	char                    *name;
	ACPI_OBJECT_TYPE        type;
	char                    *val;

} PREDEFINED_NAMES;


/*****************************************************************************
 *
 * Event typedefs and structs
 *
 ****************************************************************************/


/* Status bits. */

#define ACPI_STATUS_PMTIMER                  0x0001
#define ACPI_STATUS_GLOBAL                   0x0020
#define ACPI_STATUS_POWER_BUTTON             0x0100
#define ACPI_STATUS_SLEEP_BUTTON             0x0200
#define ACPI_STATUS_RTC_ALARM                0x0400

/* Enable bits. */

#define ACPI_ENABLE_PMTIMER                  0x0001
#define ACPI_ENABLE_GLOBAL                   0x0020
#define ACPI_ENABLE_POWER_BUTTON             0x0100
#define ACPI_ENABLE_SLEEP_BUTTON             0x0200
#define ACPI_ENABLE_RTC_ALARM                0x0400


/*
 * Entry in the Address_space (AKA Operation Region) table
 */

typedef struct
{
	ADDRESS_SPACE_HANDLER   handler;
	void                    *context;

} ACPI_ADDRESS_SPACE_INFO;


/* Values and addresses of the GPE registers (both banks) */

typedef struct
{
	u8                      status;         /* Current value of status reg */
	u8                      enable;         /* Current value of enable reg */
	u16                     status_addr;    /* Address of status reg */
	u16                     enable_addr;    /* Address of enable reg */
	u8                      gpe_base;       /* Base GPE number */

} ACPI_GPE_REGISTERS;


#define ACPI_GPE_LEVEL_TRIGGERED            1
#define ACPI_GPE_EDGE_TRIGGERED             2


/* Information about each particular GPE level */

typedef struct
{
	u8                      type;           /* Level or Edge */

	ACPI_HANDLE             method_handle;  /* Method handle for direct (fast) execution */
	GPE_HANDLER             handler;        /* Address of handler, if any */
	void                    *context;       /* Context to be passed to handler */

} ACPI_GPE_LEVEL_INFO;


/* Information about each particular fixed event */

typedef struct
{
	FIXED_EVENT_HANDLER     handler;        /* Address of handler. */
	void                    *context;       /* Context to be passed to handler */

} ACPI_FIXED_EVENT_INFO;


/* Information used during field processing */

typedef struct
{
	u8                      skip_field;
	u8                      field_flag;
	u32                     pkg_length;

} ACPI_FIELD_INFO;


/*****************************************************************************
 *
 * Parser typedefs and structs
 *
 ****************************************************************************/


#define OP_INFO_TYPE                0x1F
#define OP_INFO_HAS_ARGS            0x20
#define OP_INFO_CHILD_LOCATION      0xC0

/*
 * AML opcode, name, and argument layout
 */
typedef struct acpi_op_info
{
	u16                     opcode;         /* AML opcode */
	u8                      flags;          /* Opcode type, Has_args flag */
	u32                     parse_args;     /* Grammar/Parse time arguments */
	u32                     runtime_args;   /* Interpret time arguments */

	DEBUG_ONLY_MEMBERS (
	char                    *name)          /* op name (debug only) */

} ACPI_OP_INFO;


typedef union acpi_op_value
{
	u32                     integer;        /* integer constant */
	u32                     size;           /* bytelist or field size */
	char                    *string;        /* NULL terminated string */
	u8                      *buffer;        /* buffer or string */
	char                    *name;          /* NULL terminated string */
	struct acpi_generic_op  *arg;           /* arguments and contained ops */
	ACPI_NAMED_OBJECT       *entry;         /* entry in interpreter namespace tbl */

} ACPI_OP_VALUE;


#define ACPI_COMMON_OP \
	u8                      data_type;      /* To differentiate various internal objs */\
	u8                      flags;          /* Type of Op */\
	u16                     opcode;         /* AML opcode */\
	u32                     aml_offset;     /* offset of declaration in AML */\
	struct acpi_generic_op  *parent;        /* parent op */\
	struct acpi_generic_op  *next;          /* next op */\
	DEBUG_ONLY_MEMBERS (\
	char                    op_name[16])    /* op name (debug only) */\
			  /* NON-DEBUG members below: */\
	void                    *acpi_named_object;/* for use by interpreter */\
	ACPI_OP_VALUE           value;          /* Value or args associated with the opcode */\


/*
 * generic operation (eg. If, While, Store)
 */
typedef struct acpi_generic_op
{
	ACPI_COMMON_OP
} ACPI_GENERIC_OP;


/*
 * operation with a name (eg. Scope, Method, Name, Named_field, ...)
 */
typedef struct acpi_named_op
{
	ACPI_COMMON_OP
	u32                     name;           /* 4-byte name or zero if no name */

} ACPI_NAMED_OP;


/*
 * special operation for methods and regions (parsing must be deferred
 * until a first pass parse is completed)
 */
typedef struct acpi_deferred_op
{
	ACPI_COMMON_OP
	u32                     name;           /* 4-byte name or 0 if none */
	u32                     body_length;    /* AML body size */
	u8                      *body;          /* AML body */
	u16                     thread_count;   /* Count of threads currently executing a method */

} ACPI_DEFERRED_OP;


/*
 * special operation for bytelists (Byte_list only)
 */
typedef struct acpi_bytelist_op
{
	ACPI_COMMON_OP
	u8                      *data;          /* bytelist data */

} ACPI_BYTELIST_OP;


/*
 * Parse state - one state per parser invocation and each control
 * method.
 */

typedef struct acpi_parse_state
{
	u8                      *aml_start;     /* first AML byte */
	u8                      *aml;           /* next AML byte */
	u8                      *aml_end;       /* (last + 1) AML byte */
	u8                      *pkg_end;       /* current package end */
	ACPI_GENERIC_OP         *start_op;      /* root of parse tree */
	struct acpi_parse_scope *scope;         /* current scope */
	struct acpi_parse_scope *scope_avail;   /* unused (extra) scope structs */
	struct acpi_parse_state *next;

} ACPI_PARSE_STATE;


/*
 * Parse scope - one per ACPI scope
 */

typedef struct acpi_parse_scope
{
	ACPI_GENERIC_OP         *op;            /* current op being parsed */
	u8                      *arg_end;       /* current argument end */
	u8                      *pkg_end;       /* current package end */
	struct acpi_parse_scope *parent;        /* parent scope */
	u32                     arg_list;       /* next argument to parse */
	u32                     arg_count;      /* Number of fixed arguments */

} ACPI_PARSE_SCOPE;


/*****************************************************************************
 *
 * Generic "state" object for stacks
 *
 ****************************************************************************/


#define CONTROL_NORMAL                        0xC0
#define CONTROL_CONDITIONAL_EXECUTING         0xC1
#define CONTROL_PREDICATE_EXECUTING           0xC2
#define CONTROL_PREDICATE_FALSE               0xC3
#define CONTROL_PREDICATE_TRUE                0xC4


#define ACPI_STATE_COMMON                  /* Two 32-bit fields and a pointer */\
	u8                      data_type;          /* To differentiate various internal objs */\
	u8                      flags; \
	u16                     value; \
	u16                     state; \
	u16                     acpi_eval; \
	void                    *next; \

typedef struct acpi_common_state
{
	ACPI_STATE_COMMON
} ACPI_COMMON_STATE;


/*
 * Update state - used to traverse complex objects such as packages
 */
typedef struct acpi_update_state
{
	ACPI_STATE_COMMON
	union acpi_obj_internal *object;

} ACPI_UPDATE_STATE;

/*
 * Control state - one per if/else and while constructs.
 * Allows nesting of these constructs
 */
typedef struct acpi_control_state
{
	ACPI_STATE_COMMON
	ACPI_GENERIC_OP         *predicate_op;  /* Start of if/while predicate */

} ACPI_CONTROL_STATE;


/*
 * Scope state - current scope during namespace lookups
 */

typedef struct acpi_scope_state
{
	ACPI_STATE_COMMON
	ACPI_NAME_TABLE         *name_table;

} ACPI_SCOPE_STATE;


typedef union acpi_gen_state
{
	ACPI_COMMON_STATE       common;
	ACPI_CONTROL_STATE      control;
	ACPI_UPDATE_STATE       update;
	ACPI_SCOPE_STATE        scope;

} ACPI_GENERIC_STATE;


/*****************************************************************************
 *
 * Tree walking typedefs and structs
 *
 ****************************************************************************/


/*
 * Walk state - current state of a parse tree walk.  Used for both a leisurely stroll through
 * the tree (for whatever reason), and for control method execution.
 */

#define NEXT_OP_DOWNWARD    1
#define NEXT_OP_UPWARD      2

typedef struct acpi_walk_state
{
	u8                      data_type;                          /* To differentiate various internal objs */\
	ACPI_OWNER_ID           owner_id;                           /* Owner of objects created during the walk */
	u8                      last_predicate;                     /* Result of last predicate */
	u8                      next_op_info;                       /* Info about Next_op */
	u8                      num_operands;                       /* Stack pointer for Operands[] array */
	u8                      num_results;                        /* Stack pointer for Results[] array */
	u8                      current_result;                     /* */

	struct acpi_walk_state  *next;                              /* Next Walk_state in list */
	ACPI_GENERIC_OP         *origin;                            /* Start of walk */
	ACPI_GENERIC_OP         *prev_op;                           /* Last op that was processed */
	ACPI_GENERIC_OP         *next_op;                           /* next op to be processed */
	ACPI_GENERIC_STATE      *control_state;                     /* List of control states (nested IFs) */
	ACPI_GENERIC_STATE      *scope_info;                        /* Stack of nested scopes */
	union acpi_obj_internal *return_desc;                       /* Return object, if any */
	union acpi_obj_internal *method_desc;                       /* Method descriptor if running a method */
	ACPI_GENERIC_OP         *method_call_op;                    /* Method_call Op if running a method */
	union acpi_obj_internal *operands[OBJ_NUM_OPERANDS];        /* Operands passed to the interpreter */
	union acpi_obj_internal *results[OBJ_NUM_OPERANDS];         /* Accumulated results */
	struct acpi_named_object arguments[MTH_NUM_ARGS];           /* Control method arguments */
	struct acpi_named_object local_variables[MTH_NUM_LOCALS];   /* Control method locals */


} ACPI_WALK_STATE;


/*
 * Walk list - head of a tree of walk states.  Multiple walk states are created when there
 * are nested control methods executing.
 */
typedef struct acpi_walk_list
{

	ACPI_WALK_STATE         *walk_state;

} ACPI_WALK_LIST;


typedef
ACPI_STATUS (*INTERPRETER_CALLBACK) (
	ACPI_WALK_STATE         *state,
	ACPI_GENERIC_OP         *op);


/* Info used by Acpi_ps_init_objects */

typedef struct init_walk_info
{
	u32                     method_count;
	u32                     op_region_count;
	ACPI_TABLE_DESC         *table_desc;

} INIT_WALK_INFO;


/* TBD: [Restructure] Merge with struct above */

typedef struct acpi_walk_info
{
	u32                     debug_level;
	u32                     owner_id;

} ACPI_WALK_INFO;


/*****************************************************************************
 *
 * Hardware and PNP
 *
 ****************************************************************************/


/* Sleep states */

#define SLWA_DEBUG_LEVEL    4
#define GTS_CALL            0
#define GTS_WAKE            1

/* Cx States */

#define MAX_CX_STATE_LATENCY 0xFFFFFFFF
#define MAX_CX_STATES       4

/*
 * The #define's and enum below establish an abstract way of identifying what
 * register block and register is to be accessed.  Do not change any of the
 * values as they are used in switch statements and offset calculations.
 */

#define REGISTER_BLOCK_MASK     0xFF00
#define BIT_IN_REGISTER_MASK    0x00FF
#define PM1_EVT                 0x0100
#define PM1_CONTROL             0x0200
#define PM2_CONTROL             0x0300
#define PM_TIMER                0x0400
#define PROCESSOR_BLOCK         0x0500
#define GPE0_STS_BLOCK          0x0600
#define GPE0_EN_BLOCK           0x0700
#define GPE1_STS_BLOCK          0x0800
#define GPE1_EN_BLOCK           0x0900

enum
{
	/* PM1 status register ids */

	TMR_STS =   (PM1_EVT        | 0x01),
	BM_STS,
	GBL_STS,
	PWRBTN_STS,
	SLPBTN_STS,
	RTC_STS,
	WAK_STS,

	/* PM1 enable register ids */

	TMR_EN,
	/* need to skip 1 enable number since there's no bus master enable register */
	GBL_EN =    (PM1_EVT        | 0x0A),
	PWRBTN_EN,
	SLPBTN_EN,
	RTC_EN,

	/* PM1 control register ids */

	SCI_EN =    (PM1_CONTROL    | 0x01),
	BM_RLD,
	GBL_RLS,
	SLP_TYPE_A,
	SLP_TYPE_B,
	SLP_EN,

	/* PM2 control register ids */

	ARB_DIS =   (PM2_CONTROL    | 0x01),

	/* PM Timer register ids */

	TMR_VAL =   (PM_TIMER       | 0x01),

	GPE0_STS =  (GPE0_STS_BLOCK | 0x01),
	GPE0_EN =   (GPE0_EN_BLOCK  | 0x01),

	GPE1_STS =  (GPE1_STS_BLOCK | 0x01),
	GPE1_EN =   (GPE0_EN_BLOCK  | 0x01),

	/* Last register value is one less than LAST_REG */

	LAST_REG
};


#define TMR_STS_MASK        0x0001
#define BM_STS_MASK         0x0010
#define GBL_STS_MASK        0x0020
#define PWRBTN_STS_MASK     0x0100
#define SLPBTN_STS_MASK     0x0200
#define RTC_STS_MASK        0x0400
#define WAK_STS_MASK        0x8000

#define ALL_FIXED_STS_BITS  (TMR_STS_MASK   | BM_STS_MASK  | GBL_STS_MASK | PWRBTN_STS_MASK |  \
			 SLPBTN_STS_MASK | RTC_STS_MASK | WAK_STS_MASK)

#define TMR_EN_MASK         0x0001
#define GBL_EN_MASK         0x0020
#define PWRBTN_EN_MASK      0x0100
#define SLPBTN_EN_MASK      0x0200
#define RTC_EN_MASK         0x0400

#define SCI_EN_MASK         0x0001
#define BM_RLD_MASK         0x0002
#define GBL_RLS_MASK        0x0004
#define SLP_TYPE_X_MASK     0x1C00
#define SLP_EN_MASK         0x2000

#define ARB_DIS_MASK        0x0001

#define GPE0_STS_MASK
#define GPE0_EN_MASK

#define GPE1_STS_MASK
#define GPE1_EN_MASK


#define ACPI_READ           1
#define ACPI_WRITE          2

#define LOW_BYTE            0x00FF
#define ONE_BYTE            0x08

#ifndef SET
	#define SET             1
#endif
#ifndef CLEAR
	#define CLEAR           0
#endif


/* Plug and play */

/* Pnp and ACPI data */

#define VERSION_NO                      0x01
#define LOGICAL_DEVICE_ID               0x02
#define COMPATIBLE_DEVICE_ID            0x03
#define IRQ_FORMAT                      0x04
#define DMA_FORMAT                      0x05
#define START_DEPENDENT_TAG             0x06
#define END_DEPENDENT_TAG               0x07
#define IO_PORT_DESCRIPTOR              0x08
#define FIXED_LOCATION_IO_DESCRIPTOR    0x09
#define RESERVED_TYPE0                  0x0A
#define RESERVED_TYPE1                  0x0B
#define RESERVED_TYPE2                  0x0C
#define RESERVED_TYPE3                  0x0D
#define SMALL_VENDOR_DEFINED            0x0E
#define END_TAG                         0x0F

/* Pnp and ACPI data */

#define MEMORY_RANGE_24                 0x81
#define ISA_MEMORY_RANGE                0x81
#define LARGE_VENDOR_DEFINED            0x84
#define EISA_MEMORY_RANGE               0x85
#define MEMORY_RANGE_32                 0x85
#define FIXED_EISA_MEMORY_RANGE         0x86
#define FIXED_MEMORY_RANGE_32           0x86

/* ACPI only data */

#define DWORD_ADDRESS_SPACE             0x87
#define WORD_ADDRESS_SPACE              0x88
#define EXTENDED_IRQ                    0x89

/* MUST HAVES */


typedef enum
{
	DWORD_DEVICE_ID,
	STRING_PTR_DEVICE_ID,
	STRING_DEVICE_ID

}   DEVICE_ID_TYPE;

typedef struct
{
	DEVICE_ID_TYPE      type;
	union
	{
		u32                 number;
		char                *string_ptr;
		char                buffer[9];
	} data;

} DEVICE_ID;


/*****************************************************************************
 *
 * Debug
 *
 ****************************************************************************/


/* Entry for a memory allocation (debug only) */

#ifdef ACPI_DEBUG

#define MEM_MALLOC          0
#define MEM_CALLOC          1
#define MAX_MODULE_NAME     16

typedef struct allocation_info
{
	struct allocation_info  *previous;
	struct allocation_info  *next;
	void                    *address;
	u32                     size;
	u32                     component;
	u32                     line;
	char                    module[MAX_MODULE_NAME];
	u8                      alloc_type;

} ALLOCATION_INFO;

#endif

#endif
