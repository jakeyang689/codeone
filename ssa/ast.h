#ifndef AST_H
#define AST_H

#include "lex.h"
#include <stdbool.h>

/* ==================================================================
 * Source location for debug info
 * ================================================================== */

typedef struct {
    int line;
    int column;
} SourceLoc;

/* ==================================================================
 * AST type kinds (extends LexTypeKind with structural types)
 * ================================================================== */

typedef enum {
    /* Basic types — values align with LexTypeKind for casting */
    AST_TYPE_I1   = 0,
    AST_TYPE_I8   = 1,
    AST_TYPE_I16  = 2,
    AST_TYPE_I32  = 3,
    AST_TYPE_I64  = 4,
    AST_TYPE_F32  = 5,
    AST_TYPE_F64  = 6,
    AST_TYPE_VOID = 7,
    /* Structural types */
    AST_TYPE_PTR,
    AST_TYPE_ARRAY,
    AST_TYPE_STRUCT,
} AstNodeKind;

/* ==================================================================
 * Recursive type tree
 * ================================================================== */

typedef struct AstNode {
    AstNodeKind kind;
    union {
        AstNodeKind basic;          /* basic types */
        struct AstNode *ptr_to;     /* AST_TYPE_PTR */
        struct {                    /* AST_TYPE_ARRAY */
            struct AstNode *elem;
            int64_t size;
        } array;
        char *struct_name;          /* AST_TYPE_STRUCT: name of struct type */
    };
} AstNode;

/* ==================================================================
 * Operand  (value used in instructions)
 * ================================================================== */

typedef enum {
    OPERAND_LOCAL,    /* %name */
    OPERAND_GLOBAL,   /* @name */
    OPERAND_INT,
    OPERAND_FLOAT,
    OPERAND_BOOL,
    OPERAND_NULL,
} OperandKind;

typedef struct {
    OperandKind kind;
    union {
        char *local_name;
        char *global_name;
        int64_t int_val;
        double float_val;
        bool bool_val;
    } as;
} Operand;

/* ==================================================================
 * Phi case:  [%val, @label]
 * ================================================================== */

typedef struct {
    Operand value;
    char *label;    /* label reference (without @) */
} PhiCase;

/* ==================================================================
 * Instruction
 * ================================================================== */

/* Memory ordering for atomic instructions */
typedef enum {
    ORDERING_SEQ_CST,
    ORDERING_ACQUIRE,
    ORDERING_RELEASE,
    ORDERING_ACQ_REL,
    ORDERING_RELAXED,
} MemoryOrdering;

/* Atomic RMW operation kinds for INSN_ATOMICRMW */
typedef enum {
    ATOMICOP_XCHG,
    ATOMICOP_ADD,
    ATOMICOP_SUB,
    ATOMICOP_AND,
    ATOMICOP_OR,
    ATOMICOP_XOR,
} AtomicRmwOp;

typedef enum {
    INSN_LET,

    /* Arithmetic / bitwise / comparison (all binary, same layout) */
    INSN_ADD, INSN_SUB, INSN_MUL, INSN_DIV, INSN_REM,
    INSN_AND, INSN_OR, INSN_XOR, INSN_SHL, INSN_LSHR, INSN_ASHR,
    INSN_EQ, INSN_NE, INSN_SLT, INSN_SLE, INSN_SGT, INSN_SGE,

    /* Memory */
    INSN_LOAD, INSN_STORE, INSN_GEP, INSN_FIELD,
    INSN_LOAD_ATOMIC, INSN_STORE_ATOMIC, INSN_ATOMICRMW, INSN_CMPXCHG,

    /* Control flow */
    INSN_JMP, INSN_BR, INSN_PHI, INSN_RET,

    /* Call */
    INSN_CALL,

    /* Inline assembly */
    INSN_ASM,

    /* Type conversion */
    INSN_ZEXT, INSN_SEXT, INSN_TRUNC,

    /* Unsigned arithmetic */
    INSN_MULHI, INSN_UDIV, INSN_UREM,
} InsnKind;

typedef struct Insn {
    SourceLoc loc;          /* source location for debug */
    InsnKind kind;
    char *result;           /* %result name, NULL if no result */
    AstNode *type;          /* primary type of the instruction */
    union {
        /* INSN_LET */
        struct { int align; } let;
        /* INSN_ADD … INSN_SGE */
        struct { Operand lhs, rhs; } binary;
        /* INSN_LOAD */
        struct { Operand ptr; } load;
        /* INSN_STORE */
        struct { Operand value, ptr; } store;
        /* INSN_GEP */
        struct { Operand base, index; } gep;
        /* INSN_FIELD */
        struct { AstNode *struct_type; Operand base; Operand field_index; } field;
        /* INSN_LOAD_ATOMIC */
        struct { Operand ptr; MemoryOrdering ordering; } load_atomic;
        /* INSN_STORE_ATOMIC */
        struct { Operand value, ptr; MemoryOrdering ordering; } store_atomic;
        /* INSN_ATOMICRMW */
        struct { int atomic_op; Operand ptr; Operand value; MemoryOrdering ordering; } atomicrmw;
        /* INSN_CMPXCHG */
        struct { Operand ptr, cmp, new_val; MemoryOrdering succ_ordering; MemoryOrdering fail_ordering; } cmpxchg;
        /* INSN_JMP */
        struct { char *target; } jmp;
        /* INSN_BR */
        struct { Operand cond; char *true_target, *false_target; } br;
        /* INSN_PHI */
        struct { PhiCase *cases; int count; } phi;
        /* INSN_RET */
        struct { bool is_void; Operand value; } ret;
        /* INSN_CALL */
        struct { char *func; Operand *args; int argc; } call;
        /* INSN_ASM */
        struct {
            bool intel_dialect;         /* false = ATT, true = Intel */
            bool align_stack;           /* asm alignstack */
            char *text;                 /* assembly template ($0, $1, ...) */
            int text_len;
            char *constraints;          /* LLVM constraint string, verbatim */
            int constraints_len;
            Operand *operands;
            AstNode **operand_types;
            int operand_count;
        } asm_insn;
        /* INSN_ZEXT, INSN_SEXT, INSN_TRUNC */
        struct { AstNode *from_type; Operand value; AstNode *to_type; } convert;
    } data;
} Insn;

/* ==================================================================
 * Basic block
 * ================================================================== */

typedef struct {
    SourceLoc loc;          /* source location of label */
    char *label;
    Insn *insns;
    int insn_count;
    int insn_capacity;
} BasicBlock;

/* ==================================================================
 * Function parameter
 * ================================================================== */

typedef struct {
    char *name;
    AstNode *type;
} FuncParam;

/* ==================================================================
 * Top-level declarations
 * ================================================================== */

typedef struct {
    SourceLoc loc;          /* source location */
    char *name;
    AstNode *type;
    Operand init_value;
    bool has_init;
    bool is_const;          /* true if declared with 'const' */
    bool is_internal;       /* true if declared with 'internal' keyword */
    /* Array initializer (for const arrays like const @hex: [i8, 16] = [...] ) */
    int64_t *array_init;    /* flat integer array of initial values */
    int array_count;        /* number of elements */
} GlobalDecl;

typedef struct {
    SourceLoc loc;          /* source location of 'fun' keyword */
    char *name;
    FuncParam *params;
    int param_count;
    AstNode *return_type;
    BasicBlock *blocks;
    int block_count;
    int block_capacity;
} FuncDecl;

/* External function declaration (e.g. libc functions like printf, malloc).
 * Has no body — just a signature for the linker to resolve. */
typedef struct {
    SourceLoc loc;
    char *name;
    FuncParam *params;
    int param_count;
    AstNode *return_type;
    bool is_variadic;
} ExternDecl;

/* Structure type declaration (e.g. struct @Point { x: i32, y: i32 }) */
typedef struct {
    SourceLoc loc;
    char *name;           /* struct name, without @ */
    FuncParam *fields;    /* field definitions: {name, type} pairs */
    int field_count;
} StructDecl;

/* ==================================================================
 * Program (root AST node)
 * ================================================================== */

typedef struct {
    const char *filename;   /* source file path, not owned */
    StructDecl *structs;
    int struct_count;
    int struct_capacity;
    GlobalDecl *globals;
    int global_count;
    int global_capacity;
    ExternDecl *externs;
    int extern_count;
    int extern_capacity;
    FuncDecl *functions;
    int func_count;
    int func_capacity;
} Program;

/* ==================================================================
 * Parser
 * ================================================================== */

typedef struct {
    Lexer *lexer;
    Token current;
    bool has_error;
    char error_msg[256];
    /* Struct name registry — registered when struct declarations are parsed;
       used by parse_type to recognise @StructName as a type reference. */
    char **struct_names;
    int struct_name_count;
    int struct_name_capacity;
    /* For anonymous string constants in call arguments */
    Program *prog;          /* parent program, for adding anon globals */
    int anon_counter;       /* counter for unique anon constant names */
} Parser;

/* ==================================================================
 * Parser API
 * ================================================================== */

/* Initialise parser with an already-initialised lexer. */
void ast_init(Parser *parser, Lexer *lexer);

/* Free parser-internal resources (does NOT free the lexer). */
void ast_free(Parser *parser);

/* Parse the full program. Returns NULL on error. */
Program *ast_parse(Parser *parser);

/* Check whether an error occurred. */
bool ast_has_error(const Parser *parser);

/* Get the error message. */
const char *ast_get_error(const Parser *parser);

/* ==================================================================
 * AST utility functions
 * ================================================================== */

/* Convert AstNodeKind to human-readable name. */
const char *ast_node_kind_name(AstNodeKind kind);

/* Convert InsnKind to human-readable name. */
const char *insn_kind_name(InsnKind kind);

/* Format a SourceLoc into a static buffer. */
const char *source_loc_str(const SourceLoc *loc);

/* Deep-free a complete Program tree. */
void program_free(Program *prog);

/* Deep-free an Operand (frees owned strings). */
void operand_free(Operand *op);

/* Deep-free a single Insn. */
void insn_free(Insn *insn);

/* Print a human-readable dump of the AST to stdout. */
void ast_print(const Program *prog);

#endif /* AST_H */
