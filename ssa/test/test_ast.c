#include "test.h"
#include "../lex.h"
#include "../ast.h"

/* Helper: parse source string and return the Program (or NULL on error). */
static Program *parse_str(const char *source) {
    Lexer lex;
    lexer_init(&lex, source);
    Parser parser;
    ast_init(&parser, &lex);
    Program *prog = ast_parse(&parser);
    if (ast_has_error(&parser)) {
        fprintf(stderr, "  PARSE ERROR: %s\n", ast_get_error(&parser));
    }
    ast_free(&parser);
    lexer_free(&lex);
    return prog;
}

/* ==================================================================
 * Global declaration tests
 * ================================================================== */

TEST(parse_empty) {
    Program *p = parse_str("");
    ASSERT_NOT_NULL(p);
    ASSERT_EQ(p->global_count, 0);
    ASSERT_EQ(p->func_count, 0);
    program_free(p);
}

TEST(parse_global_no_init) {
    Program *p = parse_str("global @x: i32");
    ASSERT_NOT_NULL(p);
    ASSERT_EQ(p->global_count, 1);
    ASSERT_EQ(p->func_count, 0);
    ASSERT_STR_EQ(p->globals[0].name, "x");
    ASSERT_EQ(p->globals[0].type->kind, AST_TYPE_I32);
    ASSERT(!p->globals[0].has_init);
    program_free(p);
}

TEST(parse_global_with_init) {
    Program *p = parse_str("global @count: i32 = 42");
    ASSERT_NOT_NULL(p);
    ASSERT_EQ(p->global_count, 1);
    ASSERT_STR_EQ(p->globals[0].name, "count");
    ASSERT_EQ(p->globals[0].type->kind, AST_TYPE_I32);
    ASSERT(p->globals[0].has_init);
    ASSERT_EQ(p->globals[0].init_value.kind, OPERAND_INT);
    ASSERT_EQ(p->globals[0].init_value.as.int_val, 42);
    program_free(p);
}

TEST(parse_global_void) {
    Program *p = parse_str("global @debug: i1 = true");
    ASSERT_NOT_NULL(p);
    ASSERT_EQ(p->globals[0].type->kind, AST_TYPE_I1);
    ASSERT(p->globals[0].has_init);
    ASSERT_EQ(p->globals[0].init_value.kind, OPERAND_BOOL);
    ASSERT(p->globals[0].init_value.as.bool_val);
    program_free(p);
}

TEST(parse_global_float_init) {
    Program *p = parse_str("global @pi: f64 = 3.14");
    ASSERT_NOT_NULL(p);
    ASSERT_EQ(p->globals[0].type->kind, AST_TYPE_F64);
    ASSERT(p->globals[0].has_init);
    ASSERT_EQ(p->globals[0].init_value.kind, OPERAND_FLOAT);
    program_free(p);
}

TEST(parse_global_str_init) {
    Program *p = parse_str("const @msg: [i8, 6] = \"hello\"");
    ASSERT_NOT_NULL(p);
    ASSERT_EQ(p->global_count, 1);
    ASSERT(p->globals[0].has_init);
    ASSERT_EQ(p->globals[0].array_count, 6);
    ASSERT_EQ(p->globals[0].array_init[0], 104); /* 'h' */
    ASSERT_EQ(p->globals[0].array_init[4], 111); /* 'o' */
    ASSERT_EQ(p->globals[0].array_init[5], 0);   /* null */
    program_free(p);
}

TEST(parse_global_str_init_escape) {
    Program *p = parse_str("const @x: [i8, 4] = \"a\\nb\"");
    ASSERT_NOT_NULL(p);
    ASSERT_EQ(p->global_count, 1);
    ASSERT(p->globals[0].has_init);
    ASSERT_EQ(p->globals[0].array_count, 4);
    ASSERT_EQ(p->globals[0].array_init[0], 97);  /* 'a' */
    ASSERT_EQ(p->globals[0].array_init[1], 10);  /* '\n' */
    ASSERT_EQ(p->globals[0].array_init[2], 98);  /* 'b' */
    ASSERT_EQ(p->globals[0].array_init[3], 0);   /* null */
    program_free(p);
}

TEST(parse_global_str_init_crlf) {
    Program *p = parse_str("const @x: [i8, 4] = \"a\\r\\n\"");
    ASSERT_NOT_NULL(p);
    ASSERT_EQ(p->global_count, 1);
    ASSERT_EQ(p->globals[0].array_count, 4);
    ASSERT_EQ(p->globals[0].array_init[0], 97);  /* 'a' */
    ASSERT_EQ(p->globals[0].array_init[1], 13);  /* '\r' */
    ASSERT_EQ(p->globals[0].array_init[2], 10);  /* '\n' */
    ASSERT_EQ(p->globals[0].array_init[3], 0);   /* null */
    program_free(p);
}

/* ==================================================================
 * Extern declaration tests
 * ================================================================== */

TEST(parse_extern_simple) {
    Program *p = parse_str("extern @putchar(c: i32) -> i32");
    ASSERT_NOT_NULL(p);
    ASSERT_EQ(p->extern_count, 1);
    ASSERT_STR_EQ(p->externs[0].name, "putchar");
    ASSERT_EQ(p->externs[0].param_count, 1);
    ASSERT_STR_EQ(p->externs[0].params[0].name, "c");
    ASSERT_EQ(p->externs[0].params[0].type->kind, AST_TYPE_I32);
    ASSERT_EQ(p->externs[0].return_type->kind, AST_TYPE_I32);
    program_free(p);
}

TEST(parse_extern_void) {
    Program *p = parse_str("extern @exit(code: i32) -> void");
    ASSERT_NOT_NULL(p);
    ASSERT_EQ(p->extern_count, 1);
    ASSERT_STR_EQ(p->externs[0].name, "exit");
    ASSERT_EQ(p->externs[0].return_type->kind, AST_TYPE_VOID);
    program_free(p);
}

TEST(parse_extern_multi_param) {
    Program *p = parse_str("extern @write(fd: i32, buf: *i8, len: i64) -> i64");
    ASSERT_NOT_NULL(p);
    ASSERT_EQ(p->extern_count, 1);
    ASSERT_EQ(p->externs[0].param_count, 3);
    ASSERT_STR_EQ(p->externs[0].params[1].name, "buf");
    ASSERT_EQ(p->externs[0].params[1].type->kind, AST_TYPE_PTR);
    program_free(p);
}

TEST(parse_extern_with_func) {
    Program *p = parse_str(
        "extern @putchar(c: i32) -> i32\n"
        "fun @main() -> i32 {\n"
        "  @entry:\n"
        "    ret i32 0\n"
        "}"
    );
    ASSERT_NOT_NULL(p);
    ASSERT_EQ(p->extern_count, 1);
    ASSERT_EQ(p->func_count, 1);
    ASSERT_STR_EQ(p->externs[0].name, "putchar");
    ASSERT_STR_EQ(p->functions[0].name, "main");
    program_free(p);
}

TEST(parse_extern_variadic) {
    Program *p = parse_str("extern @printf(fmt: *i8, ...) -> i32");
    ASSERT_NOT_NULL(p);
    ASSERT_EQ(p->extern_count, 1);
    ASSERT_STR_EQ(p->externs[0].name, "printf");
    ASSERT(p->externs[0].is_variadic);
    ASSERT_EQ(p->externs[0].param_count, 1);
    program_free(p);
}

/* ==================================================================
 * Function declaration tests
 * ================================================================== */

TEST(parse_func_no_params) {
    Program *p = parse_str(
        "fun @main() -> i32 {\n"
        "  @entry:\n"
        "    ret i32 0\n"
        "}"
    );
    ASSERT_NOT_NULL(p);
    ASSERT_EQ(p->func_count, 1);
    ASSERT_STR_EQ(p->functions[0].name, "main");
    ASSERT_EQ(p->functions[0].param_count, 0);
    ASSERT_EQ(p->functions[0].return_type->kind, AST_TYPE_I32);
    ASSERT_EQ(p->functions[0].block_count, 1);
    ASSERT_STR_EQ(p->functions[0].blocks[0].label, "entry");
    ASSERT_EQ(p->functions[0].blocks[0].insn_count, 1);
    program_free(p);
}

TEST(parse_func_void_return) {
    Program *p = parse_str(
        "fun @nop() -> void {\n"
        "  @entry:\n"
        "    ret void\n"
        "}"
    );
    ASSERT_NOT_NULL(p);
    ASSERT_EQ(p->functions[0].return_type->kind, AST_TYPE_VOID);
    ASSERT_EQ(p->functions[0].blocks[0].insns[0].kind, INSN_RET);
    ASSERT(p->functions[0].blocks[0].insns[0].data.ret.is_void);
    program_free(p);
}

TEST(parse_func_with_params) {
    Program *p = parse_str(
        "fun @add(a: i32, b: i64) -> i64 {\n"
        "  @entry:\n"
        "    ret i64 0\n"
        "}"
    );
    ASSERT_NOT_NULL(p);
    ASSERT_EQ(p->functions[0].param_count, 2);
    ASSERT_STR_EQ(p->functions[0].params[0].name, "a");
    ASSERT_EQ(p->functions[0].params[0].type->kind, AST_TYPE_I32);
    ASSERT_STR_EQ(p->functions[0].params[1].name, "b");
    ASSERT_EQ(p->functions[0].params[1].type->kind, AST_TYPE_I64);
    program_free(p);
}

TEST(parse_func_multiple_blocks) {
    Program *p = parse_str(
        "fun @test() -> i32 {\n"
        "  @b1:\n"
        "    jmp @b2\n"
        "  @b2:\n"
        "    ret i32 0\n"
        "}"
    );
    ASSERT_NOT_NULL(p);
    ASSERT_EQ(p->functions[0].block_count, 2);
    ASSERT_STR_EQ(p->functions[0].blocks[0].label, "b1");
    ASSERT_STR_EQ(p->functions[0].blocks[1].label, "b2");
    ASSERT_EQ(p->functions[0].blocks[0].insns[0].kind, INSN_JMP);
    ASSERT_STR_EQ(p->functions[0].blocks[0].insns[0].data.jmp.target, "b2");
    program_free(p);
}

/* ==================================================================
 * Instruction tests
 * ================================================================== */

TEST(insn_let) {
    Program *p = parse_str(
        "fun @f() -> i32 {\n"
        "  @entry:\n"
        "    %p = let i32\n"
        "    ret i32 0\n"
        "}"
    );
    ASSERT_NOT_NULL(p);
    Insn *insn = &p->functions[0].blocks[0].insns[0];
    ASSERT_EQ(insn->kind, INSN_LET);
    ASSERT_STR_EQ(insn->result, "p");
    ASSERT_EQ(insn->type->kind, AST_TYPE_I32);
    program_free(p);
}

TEST(insn_let_align) {
    Program *p = parse_str(
        "fun @f() -> i32 {\n"
        "  @entry:\n"
        "    %b = let i8, 16\n"
        "    ret i32 0\n"
        "}"
    );
    ASSERT_NOT_NULL(p);
    Insn *insn = &p->functions[0].blocks[0].insns[0];
    ASSERT_EQ(insn->kind, INSN_LET);
    ASSERT_EQ(insn->data.let.align, 16);
    program_free(p);
}

TEST(insn_add) {
    Program *p = parse_str(
        "fun @f() -> i32 {\n"
        "  @entry:\n"
        "    %1 = add i32 2, 1\n"
        "    ret i32 %1\n"
        "}"
    );
    ASSERT_NOT_NULL(p);
    Insn *insn = &p->functions[0].blocks[0].insns[0];
    ASSERT_EQ(insn->kind, INSN_ADD);
    ASSERT_STR_EQ(insn->result, "1");
    ASSERT_EQ(insn->type->kind, AST_TYPE_I32);
    ASSERT_EQ(insn->data.binary.lhs.kind, OPERAND_INT);
    ASSERT_EQ(insn->data.binary.lhs.as.int_val, 2);
    ASSERT_EQ(insn->data.binary.rhs.kind, OPERAND_INT);
    ASSERT_EQ(insn->data.binary.rhs.as.int_val, 1);
    program_free(p);
}

TEST(insn_sub) {
    Program *p = parse_str(
        "fun @f(a: i32) -> i32 {\n"
        "  @entry:\n"
        "    %1 = sub i32 %a, 5\n"
        "    ret i32 %1\n"
        "}"
    );
    ASSERT_NOT_NULL(p);
    Insn *insn = &p->functions[0].blocks[0].insns[0];
    ASSERT_EQ(insn->kind, INSN_SUB);
    ASSERT_STR_EQ(insn->result, "1");
    program_free(p);
}

TEST(insn_mul) {
    Program *p = parse_str(
        "fun @f() -> i32 {\n"
        "  @entry:\n"
        "    %r = mul i32 3, 7\n"
        "    ret i32 %r\n"
        "}"
    );
    ASSERT_NOT_NULL(p);
    ASSERT_EQ(p->functions[0].blocks[0].insns[0].kind, INSN_MUL);
    program_free(p);
}

TEST(insn_div) {
    Program *p = parse_str(
        "fun @f() -> i32 {\n"
        "  @entry:\n"
        "    %r = div i32 10, 3\n"
        "    ret i32 %r\n"
        "}"
    );
    ASSERT_NOT_NULL(p);
    ASSERT_EQ(p->functions[0].blocks[0].insns[0].kind, INSN_DIV);
    program_free(p);
}

TEST(insn_rem) {
    Program *p = parse_str(
        "fun @f() -> i32 {\n"
        "  @entry:\n"
        "    %r = rem i32 10, 3\n"
        "    ret i32 %r\n"
        "}"
    );
    ASSERT_NOT_NULL(p);
    ASSERT_EQ(p->functions[0].blocks[0].insns[0].kind, INSN_REM);
    program_free(p);
}

TEST(insn_and_or_xor) {
    Program *p = parse_str(
        "fun @f() -> i32 {\n"
        "  @entry:\n"
        "    %1 = and i32 %x, 1\n"
        "    %2 = or  i32 %x, 2\n"
        "    %3 = xor i32 %x, 3\n"
        "    ret i32 %3\n"
        "}"
    );
    ASSERT_NOT_NULL(p);
    ASSERT_EQ(p->functions[0].blocks[0].insns[0].kind, INSN_AND);
    ASSERT_EQ(p->functions[0].blocks[0].insns[1].kind, INSN_OR);
    ASSERT_EQ(p->functions[0].blocks[0].insns[2].kind, INSN_XOR);
    program_free(p);
}

TEST(insn_shl_lshr) {
    Program *p = parse_str(
        "fun @f() -> i32 {\n"
        "  @entry:\n"
        "    %1 = shl i32 1, 4\n"
        "    %2 = lshr i32 %1, 2\n"
        "    ret i32 %2\n"
        "}"
    );
    ASSERT_NOT_NULL(p);
    ASSERT_EQ(p->functions[0].blocks[0].insns[0].kind, INSN_SHL);
    ASSERT_EQ(p->functions[0].blocks[0].insns[1].kind, INSN_LSHR);
    program_free(p);
}

TEST(insn_ashr) {
    Program *p = parse_str(
        "fun @f() -> i32 {\n"
        "  @entry:\n"
        "    %1 = ashr i32 -1, 2\n"
        "    ret i32 %1\n"
        "}"
    );
    ASSERT_NOT_NULL(p);
    ASSERT_EQ(p->functions[0].blocks[0].insns[0].kind, INSN_ASHR);
    program_free(p);
}

TEST(insn_comparisons) {
    Program *p = parse_str(
        "fun @f() -> i1 {\n"
        "  @entry:\n"
        "    %e = eq i32 1, 2\n"
        "    %n = ne i32 1, 2\n"
        "    %l = slt i32 1, 2\n"
        "    %le= sle i32 1, 2\n"
        "    %g = sgt i32 1, 2\n"
        "    %ge= sge i32 1, 2\n"
        "    ret i1 %e\n"
        "}"
    );
    ASSERT_NOT_NULL(p);
    ASSERT_EQ(p->functions[0].blocks[0].insns[0].kind, INSN_EQ);
    ASSERT_EQ(p->functions[0].blocks[0].insns[1].kind, INSN_NE);
    ASSERT_EQ(p->functions[0].blocks[0].insns[2].kind, INSN_SLT);
    ASSERT_EQ(p->functions[0].blocks[0].insns[3].kind, INSN_SLE);
    ASSERT_EQ(p->functions[0].blocks[0].insns[4].kind, INSN_SGT);
    ASSERT_EQ(p->functions[0].blocks[0].insns[5].kind, INSN_SGE);
    program_free(p);
}

TEST(insn_null) {
    Program *p = parse_str(
        "fun @f() -> i32 {\n"
        "  @entry:\n"
        "    %r = eq *i8 null, null\n"
        "    ret i32 0\n"
        "}"
    );
    ASSERT_NOT_NULL(p);
    ASSERT_EQ(p->functions[0].blocks[0].insns[0].kind, INSN_EQ);
    ASSERT_EQ(p->functions[0].blocks[0].insns[0].data.binary.lhs.kind, OPERAND_NULL);
    ASSERT_EQ(p->functions[0].blocks[0].insns[0].data.binary.rhs.kind, OPERAND_NULL);
    program_free(p);
}

TEST(insn_load) {
    Program *p = parse_str(
        "fun @f() -> i32 {\n"
        "  @entry:\n"
        "    %v = load i32, *%p\n"
        "    ret i32 %v\n"
        "}"
    );
    ASSERT_NOT_NULL(p);
    Insn *insn = &p->functions[0].blocks[0].insns[0];
    ASSERT_EQ(insn->kind, INSN_LOAD);
    ASSERT_STR_EQ(insn->result, "v");
    ASSERT_EQ(insn->data.load.ptr.kind, OPERAND_LOCAL);
    ASSERT_STR_EQ(insn->data.load.ptr.as.local_name, "p");
    program_free(p);
}

TEST(insn_store) {
    Program *p = parse_str(
        "fun @f() -> void {\n"
        "  @entry:\n"
        "    store i32 42, *%p\n"
        "    ret void\n"
        "}"
    );
    ASSERT_NOT_NULL(p);
    Insn *insn = &p->functions[0].blocks[0].insns[0];
    ASSERT_EQ(insn->kind, INSN_STORE);
    ASSERT_EQ(insn->type->kind, AST_TYPE_I32);
    ASSERT_EQ(insn->data.store.value.kind, OPERAND_INT);
    ASSERT_EQ(insn->data.store.value.as.int_val, 42);
    ASSERT_EQ(insn->data.store.ptr.kind, OPERAND_LOCAL);
    program_free(p);
}

TEST(insn_gep) {
    Program *p = parse_str(
        "fun @f() -> void {\n"
        "  @entry:\n"
        "    %p = gep i32, *%arr, %i\n"
        "    ret void\n"
        "}"
    );
    ASSERT_NOT_NULL(p);
    Insn *insn = &p->functions[0].blocks[0].insns[0];
    ASSERT_EQ(insn->kind, INSN_GEP);
    ASSERT_STR_EQ(insn->result, "p");
    ASSERT_EQ(insn->data.gep.base.kind, OPERAND_LOCAL);
    ASSERT_STR_EQ(insn->data.gep.base.as.local_name, "arr");
    ASSERT_EQ(insn->data.gep.index.kind, OPERAND_LOCAL);
    ASSERT_STR_EQ(insn->data.gep.index.as.local_name, "i");
    program_free(p);
}

TEST(insn_jmp) {
    Program *p = parse_str(
        "fun @f() -> i32 {\n"
        "  @entry:\n"
        "    jmp @done\n"
        "  @done:\n"
        "    ret i32 0\n"
        "}"
    );
    ASSERT_NOT_NULL(p);
    ASSERT_EQ(p->functions[0].blocks[0].insns[0].kind, INSN_JMP);
    ASSERT_STR_EQ(p->functions[0].blocks[0].insns[0].data.jmp.target, "done");
    program_free(p);
}

TEST(insn_br) {
    Program *p = parse_str(
        "fun @f() -> i32 {\n"
        "  @entry:\n"
        "    br %c, @t, @f\n"
        "  @t:\n"
        "    ret i32 1\n"
        "  @f:\n"
        "    ret i32 0\n"
        "}"
    );
    ASSERT_NOT_NULL(p);
    Insn *insn = &p->functions[0].blocks[0].insns[0];
    ASSERT_EQ(insn->kind, INSN_BR);
    ASSERT_EQ(insn->data.br.cond.kind, OPERAND_LOCAL);
    ASSERT_STR_EQ(insn->data.br.cond.as.local_name, "c");
    ASSERT_STR_EQ(insn->data.br.true_target, "t");
    ASSERT_STR_EQ(insn->data.br.false_target, "f");
    program_free(p);
}

TEST(insn_phi) {
    Program *p = parse_str(
        "fun @f() -> i32 {\n"
        "  @entry:\n"
        "    jmp @loop\n"
        "  @loop:\n"
        "    %x = phi [%0, @entry], [%n, @body]\n"
        "    ret i32 %x\n"
        "  @body:\n"
        "    jmp @loop\n"
        "}"
    );
    ASSERT_NOT_NULL(p);
    BasicBlock *loop = &p->functions[0].blocks[1];
    ASSERT_STR_EQ(loop->label, "loop");
    Insn *phi = &loop->insns[0];
    ASSERT_EQ(phi->kind, INSN_PHI);
    ASSERT_STR_EQ(phi->result, "x");
    ASSERT_EQ(phi->data.phi.count, 2);
    ASSERT_EQ(phi->data.phi.cases[0].value.kind, OPERAND_LOCAL);
    ASSERT_STR_EQ(phi->data.phi.cases[0].value.as.local_name, "0");
    ASSERT_STR_EQ(phi->data.phi.cases[0].label, "entry");
    ASSERT_EQ(phi->data.phi.cases[1].value.kind, OPERAND_LOCAL);
    ASSERT_STR_EQ(phi->data.phi.cases[1].value.as.local_name, "n");
    ASSERT_STR_EQ(phi->data.phi.cases[1].label, "body");
    program_free(p);
}

TEST(insn_ret_value) {
    Program *p = parse_str(
        "fun @f() -> i32 {\n"
        "  @entry:\n"
        "    ret i32 42\n"
        "}"
    );
    ASSERT_NOT_NULL(p);
    Insn *insn = &p->functions[0].blocks[0].insns[0];
    ASSERT_EQ(insn->kind, INSN_RET);
    ASSERT(!insn->data.ret.is_void);
    ASSERT_EQ(insn->data.ret.value.kind, OPERAND_INT);
    ASSERT_EQ(insn->data.ret.value.as.int_val, 42);
    program_free(p);
}

TEST(insn_ret_void) {
    Program *p = parse_str(
        "fun @f() -> void {\n"
        "  @entry:\n"
        "    ret void\n"
        "}"
    );
    ASSERT_NOT_NULL(p);
    Insn *insn = &p->functions[0].blocks[0].insns[0];
    ASSERT(insn->data.ret.is_void);
    program_free(p);
}

TEST(insn_call_no_result) {
    Program *p = parse_str(
        "fun @f() -> void {\n"
        "  @entry:\n"
        "    call @print(42)\n"
        "    ret void\n"
        "}"
    );
    ASSERT_NOT_NULL(p);
    Insn *insn = &p->functions[0].blocks[0].insns[0];
    ASSERT_EQ(insn->kind, INSN_CALL);
    ASSERT_NULL(insn->result);
    ASSERT_STR_EQ(insn->data.call.func, "print");
    ASSERT_EQ(insn->data.call.argc, 1);
    ASSERT_EQ(insn->data.call.args[0].kind, OPERAND_INT);
    ASSERT_EQ(insn->data.call.args[0].as.int_val, 42);
    program_free(p);
}

TEST(insn_call_with_result) {
    Program *p = parse_str(
        "fun @f() -> i32 {\n"
        "  @entry:\n"
        "    %r = call @foo(%a, %b)\n"
        "    ret i32 %r\n"
        "}"
    );
    ASSERT_NOT_NULL(p);
    Insn *insn = &p->functions[0].blocks[0].insns[0];
    ASSERT_EQ(insn->kind, INSN_CALL);
    ASSERT_STR_EQ(insn->result, "r");
    ASSERT_STR_EQ(insn->data.call.func, "foo");
    ASSERT_EQ(insn->data.call.argc, 2);
    ASSERT_EQ(insn->data.call.args[0].kind, OPERAND_LOCAL);
    ASSERT_STR_EQ(insn->data.call.args[0].as.local_name, "a");
    ASSERT_EQ(insn->data.call.args[1].kind, OPERAND_LOCAL);
    ASSERT_STR_EQ(insn->data.call.args[1].as.local_name, "b");
    program_free(p);
}

TEST(insn_call_empty_string_arg) {
    Program *p = parse_str(
        "fun @f() -> void {\n"
        "  @entry:\n"
        "    call @print(\"\")\n"
        "    ret void\n"
        "}"
    );
    ASSERT_NOT_NULL(p);
    ASSERT_EQ(p->global_count, 1);
    ASSERT_EQ(p->globals[0].type->kind, AST_TYPE_ARRAY);
    ASSERT_EQ(p->globals[0].type->array.size, 1);
    ASSERT_EQ(p->globals[0].array_count, 1);
    ASSERT_EQ(p->globals[0].array_init[0], 0);
    Insn *insn = &p->functions[0].blocks[0].insns[0];
    ASSERT_EQ(insn->data.call.args[0].kind, OPERAND_GLOBAL);
    program_free(p);
}

TEST(insn_asm_with_result) {
    Program *p = parse_str(
        "fun @f() -> i64 {\n"
        "  @entry:\n"
        "    %r = asm att i64, \"add $0, $1, $1\", \"=r,r\", i64 %a\n"
        "    ret i64 %r\n"
        "}"
    );
    ASSERT_NOT_NULL(p);
    Insn *insn = &p->functions[0].blocks[0].insns[0];
    ASSERT_EQ(insn->kind, INSN_ASM);
    ASSERT_STR_EQ(insn->result, "r");
    ASSERT(!insn->data.asm_insn.intel_dialect);
    ASSERT_STR_EQ(insn->data.asm_insn.text, "add $0, $1, $1");
    ASSERT_EQ(insn->data.asm_insn.text_len, 14);
    ASSERT_STR_EQ(insn->data.asm_insn.constraints, "=r,r");
    ASSERT_EQ(insn->data.asm_insn.operand_count, 1);
    ASSERT_EQ(insn->data.asm_insn.operand_types[0]->kind, AST_TYPE_I64);
    ASSERT_EQ(insn->data.asm_insn.operands[0].kind, OPERAND_LOCAL);
    ASSERT_STR_EQ(insn->data.asm_insn.operands[0].as.local_name, "a");
    program_free(p);
}

TEST(insn_asm_void_intel_clobber) {
    Program *p = parse_str(
        "fun @f() -> void {\n"
        "  @entry:\n"
        "    asm intel void, \"mfence\", \"~{memory}\"\n"
        "    ret void\n"
        "}"
    );
    ASSERT_NOT_NULL(p);
    Insn *insn = &p->functions[0].blocks[0].insns[0];
    ASSERT_EQ(insn->kind, INSN_ASM);
    ASSERT_NULL(insn->result);
    ASSERT(insn->data.asm_insn.intel_dialect);
    ASSERT_EQ(insn->type->kind, AST_TYPE_VOID);
    ASSERT_STR_EQ(insn->data.asm_insn.constraints, "~{memory}");
    ASSERT_EQ(insn->data.asm_insn.operand_count, 0);
    program_free(p);
}

TEST(insn_asm_escaped_newline) {
    Program *p = parse_str(
        "fun @f() -> void {\n"
        "  @entry:\n"
        "    asm att void, \"nop\\n nop\", \"\"\n"
        "    ret void\n"
        "}"
    );
    ASSERT_NOT_NULL(p);
    Insn *insn = &p->functions[0].blocks[0].insns[0];
    ASSERT_EQ(insn->data.asm_insn.text_len, 8);
    ASSERT_EQ(insn->data.asm_insn.text[3], '\n');
    program_free(p);
}

TEST(insn_asm_alignstack) {
    Program *p = parse_str(
        "fun @f() -> void {\n"
        "  @entry:\n"
        "    asm alignstack att void, \"nop\", \"\"\n"
        "    ret void\n"
        "}"
    );
    ASSERT_NOT_NULL(p);
    Insn *insn = &p->functions[0].blocks[0].insns[0];
    ASSERT_EQ(insn->kind, INSN_ASM);
    ASSERT(insn->data.asm_insn.align_stack);
    ASSERT_STR_EQ(insn->data.asm_insn.text, "nop");
    program_free(p);
}

TEST(insn_asm_named_operands) {
    Program *p = parse_str(
        "fun @f(a: i64) -> i64 {\n"
        "  @entry:\n"
        "    %r = asm att i64, \"add $0, ${a}, ${a:c}\\n// $$x\", "
        "\"=r,r\", i64 %a\n"
        "    ret i64 %r\n"
        "}"
    );
    ASSERT_NOT_NULL(p);
    Insn *insn = &p->functions[0].blocks[0].insns[0];
    ASSERT_EQ(insn->kind, INSN_ASM);
    ASSERT_STR_EQ(insn->data.asm_insn.text, "add $0, ${1}, ${1:c}\n// $$x");
    program_free(p);
}

TEST(insn_asm_struct_result) {
    Program *p = parse_str(
        "struct @Pair { @lo: i64, @hi: i64 }\n"
        "fun @f() -> void {\n"
        "  @entry:\n"
        "    %s = asm att @Pair, \"nop\", \"=r,=r\"\n"
        "    ret void\n"
        "}"
    );
    ASSERT_NOT_NULL(p);
    Insn *insn = &p->functions[0].blocks[0].insns[0];
    ASSERT_EQ(insn->kind, INSN_ASM);
    ASSERT_EQ(insn->type->kind, AST_TYPE_STRUCT);
    ASSERT_STR_EQ(insn->type->struct_name, "Pair");
    program_free(p);
}

TEST(parse_error_asm_unknown_named_operand) {
    Lexer lex;
    lexer_init(&lex,
        "fun @f() -> i64 {\n"
        "  @entry:\n"
        "    %r = asm att i64, \"add $0, ${x}\", \"=r,r\", i64 1\n"
        "    ret i64 %r\n"
        "}");
    Parser parser;
    ast_init(&parser, &lex);
    Program *p = ast_parse(&parser);
    ASSERT_NULL(p);
    ASSERT(ast_has_error(&parser));
    ASSERT(strstr(ast_get_error(&parser), "line 3") != NULL);
    ASSERT(strstr(ast_get_error(&parser), "unknown asm operand name") != NULL);
    ast_free(&parser);
    lexer_free(&lex);
}

TEST(parse_error_asm_ambiguous_operand_name) {
    Lexer lex;
    lexer_init(&lex,
        "fun @f(a: i64) -> i64 {\n"
        "  @entry:\n"
        "    %r = asm att i64, \"add $0, ${a}, ${a}\", \"=r,r,r\", "
        "i64 %a, i64 %a\n"
        "    ret i64 %r\n"
        "}");
    Parser parser;
    ast_init(&parser, &lex);
    Program *p = ast_parse(&parser);
    ASSERT_NULL(p);
    ASSERT(ast_has_error(&parser));
    ASSERT(strstr(ast_get_error(&parser), "ambiguous") != NULL);
    ast_free(&parser);
    lexer_free(&lex);
}

TEST(parse_error_asm_struct_no_output) {
    Lexer lex;
    lexer_init(&lex,
        "struct @Pair { @lo: i64, @hi: i64 }\n"
        "fun @f() -> void {\n"
        "  @entry:\n"
        "    %s = asm att @Pair, \"nop\", \"\"\n"
        "    ret void\n"
        "}");
    Parser parser;
    ast_init(&parser, &lex);
    Program *p = ast_parse(&parser);
    ASSERT_NULL(p);
    ASSERT(ast_has_error(&parser));
    ASSERT(strstr(ast_get_error(&parser), "at least one output") != NULL);
    ast_free(&parser);
    lexer_free(&lex);
}

TEST(parse_error_asm_missing_dialect) {
    Lexer lex;
    lexer_init(&lex,
        "fun @f() -> void {\n"
        "  @entry:\n"
        "    asm void, \"nop\", \"\"\n"
        "    ret void\n"
        "}");
    Parser parser;
    ast_init(&parser, &lex);
    Program *p = ast_parse(&parser);
    ASSERT_NULL(p);
    ASSERT(ast_has_error(&parser));
    ASSERT(strstr(ast_get_error(&parser), "line 3") != NULL);
    ast_free(&parser);
    lexer_free(&lex);
}

TEST(parse_error_asm_constraint_mismatch) {
    Lexer lex;
    lexer_init(&lex,
        "fun @f() -> i64 {\n"
        "  @entry:\n"
        "    %r = asm att i64, \"mov $0, $1\", \"=r\", i64 1\n"
        "    ret i64 %r\n"
        "}");
    Parser parser;
    ast_init(&parser, &lex);
    Program *p = ast_parse(&parser);
    ASSERT_NULL(p);
    ASSERT(ast_has_error(&parser));
    ASSERT(strstr(ast_get_error(&parser), "line 3") != NULL);
    ast_free(&parser);
    lexer_free(&lex);
}

TEST(parse_error_asm_label_constraint) {
    Lexer lex;
    lexer_init(&lex,
        "fun @f() -> void {\n"
        "  @entry:\n"
        "    asm att void, \"b $0\", \"!i\"\n"
        "    ret void\n"
        "}");
    Parser parser;
    ast_init(&parser, &lex);
    Program *p = ast_parse(&parser);
    ASSERT_NULL(p);
    ASSERT(ast_has_error(&parser));
    ASSERT(strstr(ast_get_error(&parser), "not supported") != NULL);
    ast_free(&parser);
    lexer_free(&lex);
}

TEST(parse_error_asm_raw_newline) {
    Lexer lex;
    lexer_init(&lex,
        "fun @f() -> void {\n"
        "  @entry:\n"
        "    asm att void, \"nop\nnop\", \"\"\n"
        "    ret void\n"
        "}");
    Parser parser;
    ast_init(&parser, &lex);
    Program *p = ast_parse(&parser);
    ASSERT_NULL(p);
    ASSERT(ast_has_error(&parser));
    ast_free(&parser);
    lexer_free(&lex);
}

TEST(insn_convert) {
    Program *p = parse_str(
        "fun @f() -> i64 {\n"
        "  @entry:\n"
        "    %1 = zext i32 %v to i64\n"
        "    %2 = sext i32 %v to i64\n"
        "    %3 = trunc i64 %v2 to i32\n"
        "    ret i64 %1\n"
        "}"
    );
    ASSERT_NOT_NULL(p);
    Insn *z = &p->functions[0].blocks[0].insns[0];
    ASSERT_EQ(z->kind, INSN_ZEXT);
    ASSERT_STR_EQ(z->result, "1");
    ASSERT_EQ(z->data.convert.from_type->kind, AST_TYPE_I32);
    ASSERT_EQ(z->data.convert.value.kind, OPERAND_LOCAL);
    ASSERT_STR_EQ(z->data.convert.value.as.local_name, "v");
    ASSERT_EQ(z->data.convert.to_type->kind, AST_TYPE_I64);

    Insn *s = &p->functions[0].blocks[0].insns[1];
    ASSERT_EQ(s->kind, INSN_SEXT);

    Insn *t = &p->functions[0].blocks[0].insns[2];
    ASSERT_EQ(t->kind, INSN_TRUNC);
    ASSERT_EQ(t->data.convert.from_type->kind, AST_TYPE_I64);
    ASSERT_EQ(t->data.convert.to_type->kind, AST_TYPE_I32);
    program_free(p);
}

/* ==================================================================
 * Pointer and array type tests
 * ================================================================== */

TEST(type_pointer) {
    Program *p = parse_str(
        "fun @f(p: *i32) -> void {\n"
        "  @entry:\n"
        "    ret void\n"
        "}"
    );
    ASSERT_NOT_NULL(p);
    AstNode *t = p->functions[0].params[0].type;
    ASSERT_EQ(t->kind, AST_TYPE_PTR);
    ASSERT_EQ(t->ptr_to->kind, AST_TYPE_I32);
    program_free(p);
}

TEST(type_array) {
    Program *p = parse_str(
        "global @buf: [i8, 256]"
    );
    ASSERT_NOT_NULL(p);
    AstNode *t = p->globals[0].type;
    ASSERT_EQ(t->kind, AST_TYPE_ARRAY);
    ASSERT_EQ(t->array.elem->kind, AST_TYPE_I8);
    ASSERT_EQ(t->array.size, 256);
    program_free(p);
}

TEST(type_ptr_to_array) {
    Program *p = parse_str(
        "global @p: *[i32, 10]"
    );
    ASSERT_NOT_NULL(p);
    AstNode *t = p->globals[0].type;
    ASSERT_EQ(t->kind, AST_TYPE_PTR);
    ASSERT_EQ(t->ptr_to->kind, AST_TYPE_ARRAY);
    ASSERT_EQ(t->ptr_to->array.elem->kind, AST_TYPE_I32);
    ASSERT_EQ(t->ptr_to->array.size, 10);
    program_free(p);
}

/* ==================================================================
 * Struct declaration tests
 * ================================================================== */

TEST(parse_struct_decl) {
    Program *p = parse_str(
        "struct @Point { @x: i32, @y: i32 }"
    );
    ASSERT_NOT_NULL(p);
    ASSERT_EQ(p->struct_count, 1);
    ASSERT_STR_EQ(p->structs[0].name, "Point");
    ASSERT_EQ(p->structs[0].field_count, 2);
    ASSERT_STR_EQ(p->structs[0].fields[0].name, "x");
    ASSERT_EQ(p->structs[0].fields[0].type->kind, AST_TYPE_I32);
    ASSERT_STR_EQ(p->structs[0].fields[1].name, "y");
    ASSERT_EQ(p->structs[0].fields[1].type->kind, AST_TYPE_I32);
    program_free(p);
}

TEST(parse_struct_type_usage) {
    Program *p = parse_str(
        "struct @Point { @x: i32, @y: i32 }\n"
        "global @origin: @Point\n"
        "fun @test() -> void {\n"
        "  @b:\n"
        "    %pt = let @Point\n"
        "    ret void\n"
        "}"
    );
    ASSERT_NOT_NULL(p);
    ASSERT_EQ(p->struct_count, 1);
    /* Global of struct type */
    ASSERT_EQ(p->globals[0].type->kind, AST_TYPE_STRUCT);
    ASSERT_STR_EQ(p->globals[0].type->struct_name, "Point");
    /* Let of struct type */
    ASSERT_EQ(p->functions[0].blocks[0].insns[0].type->kind, AST_TYPE_STRUCT);
    ASSERT_STR_EQ(p->functions[0].blocks[0].insns[0].type->struct_name, "Point");
    program_free(p);
}

/* ==================================================================
 * Enum declaration tests
 * ================================================================== */

TEST(parse_enum_decl) {
    Program *p = parse_str(
        "enum @Color: i32 {\n"
        "  @Red = 0,\n"
        "  @Green = 1,\n"
        "  @Blue = 0x10\n"
        "}"
    );
    ASSERT_NOT_NULL(p);
    ASSERT_EQ(p->global_count, 3);
    ASSERT_STR_EQ(p->globals[0].name, "Color.Red");
    ASSERT_STR_EQ(p->globals[1].name, "Color.Green");
    ASSERT_STR_EQ(p->globals[2].name, "Color.Blue");
    ASSERT_EQ(p->globals[0].type->kind, AST_TYPE_I32);
    ASSERT_EQ(p->globals[2].type->kind, AST_TYPE_I32);
    ASSERT(p->globals[0].has_init);
    ASSERT_EQ(p->globals[0].init_value.kind, OPERAND_INT);
    ASSERT_EQ(p->globals[0].init_value.as.int_val, 0);
    ASSERT_EQ(p->globals[1].init_value.as.int_val, 1);
    ASSERT_EQ(p->globals[2].init_value.as.int_val, 16);
    ASSERT(p->globals[0].is_const);
    ASSERT(p->globals[0].is_internal);
    ASSERT_EQ(p->struct_count, 0);
    program_free(p);
}

TEST(parse_enum_negative) {
    Program *p = parse_str("enum @Sign: i64 { @Neg = -1, @Pos = 1 }");
    ASSERT_NOT_NULL(p);
    ASSERT_EQ(p->global_count, 2);
    ASSERT_EQ(p->globals[0].type->kind, AST_TYPE_I64);
    ASSERT_EQ(p->globals[0].init_value.as.int_val, -1);
    ASSERT_EQ(p->globals[1].init_value.as.int_val, 1);
    program_free(p);
}

TEST(parse_enum_member_usage) {
    Program *p = parse_str(
        "enum @Color: i32 { @Red = 1 }\n"
        "fun @test() -> i32 {\n"
        "  @b:\n"
        "    %x = add i32 @Color.Red, 1\n"
        "    ret i32 %x\n"
        "}"
    );
    ASSERT_NOT_NULL(p);
    ASSERT_EQ(p->global_count, 1);
    Insn *add = &p->functions[0].blocks[0].insns[0];
    ASSERT_EQ(add->data.binary.lhs.kind, OPERAND_GLOBAL);
    ASSERT_STR_EQ(add->data.binary.lhs.as.global_name, "Color.Red");
    program_free(p);
}

TEST(parse_enum_float_type_rejected) {
    Lexer lex;
    lexer_init(&lex, "enum @E: f64 { @A = 0 }");
    Parser parser;
    ast_init(&parser, &lex);
    Program *p = ast_parse(&parser);
    ASSERT_NULL(p);
    ASSERT(ast_has_error(&parser));
    ast_free(&parser);
    lexer_free(&lex);
}

TEST(parse_enum_non_int_value_rejected) {
    Lexer lex;
    lexer_init(&lex, "enum @E: i32 { @A = @B }");
    Parser parser;
    ast_init(&parser, &lex);
    Program *p = ast_parse(&parser);
    ASSERT_NULL(p);
    ASSERT(ast_has_error(&parser));
    ast_free(&parser);
    lexer_free(&lex);
}

TEST(parse_enum_duplicate_member_rejected) {
    Lexer lex;
    lexer_init(&lex, "enum @E: i32 { @A = 0, @A = 1 }");
    Parser parser;
    ast_init(&parser, &lex);
    Program *p = ast_parse(&parser);
    ASSERT_NULL(p);
    ASSERT(ast_has_error(&parser));
    ast_free(&parser);
    lexer_free(&lex);
}

TEST(parse_enum_internal_rejected) {
    Lexer lex;
    lexer_init(&lex, "internal enum @E: i32 { @A = 0 }");
    Parser parser;
    ast_init(&parser, &lex);
    Program *p = ast_parse(&parser);
    ASSERT_NULL(p);
    ASSERT(ast_has_error(&parser));
    ast_free(&parser);
    lexer_free(&lex);
}

TEST(insn_field) {
    Program *p = parse_str(
        "struct @Point { @x: i32, @y: i32 }\n"
        "fun @test() -> void {\n"
        "  @b:\n"
        "    %pt = let @Point\n"
        "    %xp = field @Point, *%pt, 0\n"
        "    ret void\n"
        "}"
    );
    ASSERT_NOT_NULL(p);
    Insn *field_insn = &p->functions[0].blocks[0].insns[1];
    ASSERT_EQ(field_insn->kind, INSN_FIELD);
    ASSERT_STR_EQ(field_insn->result, "xp");
    ASSERT_EQ(field_insn->data.field.struct_type->kind, AST_TYPE_STRUCT);
    ASSERT_STR_EQ(field_insn->data.field.struct_type->struct_name, "Point");
    ASSERT_EQ(field_insn->data.field.base.kind, OPERAND_LOCAL);
    ASSERT_STR_EQ(field_insn->data.field.base.as.local_name, "pt");
    ASSERT_EQ(field_insn->data.field.field_index.kind, OPERAND_INT);
    ASSERT_EQ(field_insn->data.field.field_index.as.int_val, 0);
    program_free(p);
}

/* ==================================================================
 * Complete program test
 * ================================================================== */

TEST(parse_complete_program) {
    Program *p = parse_str(
        "global @count: i32 = 0\n"
        "global @pi: f64 = 3.14\n"
        "\n"
        "fun @gcd(a: i32, b: i32) -> i32 {\n"
        "  @loop:\n"
        "    %c = eq i32 %a, %b\n"
        "    br %c, @done, @body\n"
        "  @body:\n"
        "    %cmp = slt i32 %a, %b\n"
        "    br %cmp, @swap, @sub_ab\n"
        "  @sub_ab:\n"
        "    %r = sub i32 %a, %b\n"
        "    ret i32 %r\n"
        "  @swap:\n"
        "    %r2 = sub i32 %b, %a\n"
        "    ret i32 %r2\n"
        "  @done:\n"
        "    ret i32 %a\n"
        "}\n"
    );
    ASSERT_NOT_NULL(p);
    ASSERT_EQ(p->global_count, 2);
    ASSERT_EQ(p->func_count, 1);
    ASSERT_STR_EQ(p->globals[0].name, "count");
    ASSERT_STR_EQ(p->globals[1].name, "pi");
    ASSERT_STR_EQ(p->functions[0].name, "gcd");
    ASSERT_EQ(p->functions[0].param_count, 2);
    ASSERT_EQ(p->functions[0].block_count, 5);
    /* Verify specific instructions inside blocks */
    BasicBlock *loop = &p->functions[0].blocks[0];
    ASSERT_STR_EQ(loop->label, "loop");
    ASSERT_EQ(loop->insns[0].kind, INSN_EQ);
    ASSERT_EQ(loop->insns[1].kind, INSN_BR);
    BasicBlock *body = &p->functions[0].blocks[1];
    ASSERT_STR_EQ(body->label, "body");
    ASSERT_EQ(body->insns[0].kind, INSN_SLT);
    program_free(p);
}

/* ==================================================================
 * Error handling tests
 * ================================================================== */

TEST(parse_error_empty_global) {
    Lexer lex;
    lexer_init(&lex, "global");
    Parser parser;
    ast_init(&parser, &lex);
    Program *p = ast_parse(&parser);
    ASSERT_NULL(p);
    ASSERT(ast_has_error(&parser));
    ast_free(&parser);
    lexer_free(&lex);
}

TEST(parse_error_bad_token) {
    Lexer lex;
    lexer_init(&lex, "global @x = @");
    Parser parser;
    ast_init(&parser, &lex);
    Program *p = ast_parse(&parser);
    ASSERT_NULL(p);
    ASSERT(ast_has_error(&parser));
    ast_free(&parser);
    lexer_free(&lex);
}

TEST(parse_error_no_brace) {
    Lexer lex;
    lexer_init(&lex, "fun @f() -> i32 {");
    Parser parser;
    ast_init(&parser, &lex);
    Program *p = ast_parse(&parser);
    ASSERT_NULL(p);
    ASSERT(ast_has_error(&parser));
    ast_free(&parser);
    lexer_free(&lex);
}

TEST(parse_error_duplicate_local) {
    Lexer lex;
    lexer_init(&lex,
        "fun @f() -> i32 {\n"
        "  @entry:\n"
        "    %x = let i32\n"
        "    %x = add i32 1, 2\n"
        "    ret i32 %x\n"
        "}");
    Parser parser;
    ast_init(&parser, &lex);
    Program *p = ast_parse(&parser);
    ASSERT_NULL(p);
    ASSERT(ast_has_error(&parser));
    ASSERT(strstr(ast_get_error(&parser), "line 4") != NULL);
    ASSERT(strstr(ast_get_error(&parser), "%x") != NULL);
    ast_free(&parser);
    lexer_free(&lex);
}

TEST(parse_error_duplicate_param_local) {
    Lexer lex;
    lexer_init(&lex,
        "fun @f(x: i32) -> i32 {\n"
        "  @entry:\n"
        "    %x = add i32 1, 2\n"
        "    ret i32 %x\n"
        "}");
    Parser parser;
    ast_init(&parser, &lex);
    Program *p = ast_parse(&parser);
    ASSERT_NULL(p);
    ASSERT(ast_has_error(&parser));
    ast_free(&parser);
    lexer_free(&lex);
}

TEST(parse_error_duplicate_label) {
    Lexer lex;
    lexer_init(&lex,
        "fun @f() -> i32 {\n"
        "  @entry:\n"
        "    jmp @entry\n"
        "  @entry:\n"
        "    ret i32 0\n"
        "}");
    Parser parser;
    ast_init(&parser, &lex);
    Program *p = ast_parse(&parser);
    ASSERT_NULL(p);
    ASSERT(ast_has_error(&parser));
    ast_free(&parser);
    lexer_free(&lex);
}

TEST(parse_duplicate_local_ok_across_functions) {
    Program *p = parse_str(
        "fun @a() -> i32 {\n"
        "  @entry:\n"
        "    %x = add i32 1, 2\n"
        "    ret i32 %x\n"
        "}\n"
        "fun @b() -> i32 {\n"
        "  @entry:\n"
        "    %x = add i32 3, 4\n"
        "    ret i32 %x\n"
        "}");
    ASSERT_NOT_NULL(p);
    ASSERT_EQ(p->func_count, 2);
    program_free(p);
}






TEST(parse_insn_unsigned_ops) {
    Program *p = parse_str(
        "fun @f(a: i64, b: i64) -> i64 {\n"
        "  @entry:\n"
        "    %h = mulhi i64 %a, %b\n"
        "    %q = udiv i64 %h, %b\n"
        "    %r = urem i64 %q, %a\n"
        "    ret i64 %r\n"
        "}");
    ASSERT_NOT_NULL(p);
    BasicBlock *bb = &p->functions[0].blocks[0];
    ASSERT_EQ(bb->insns[0].kind, INSN_MULHI);
    ASSERT_EQ(bb->insns[1].kind, INSN_UDIV);
    ASSERT_EQ(bb->insns[2].kind, INSN_UREM);
    ASSERT_EQ(bb->insns[0].type->kind, AST_TYPE_I64);
    program_free(p);
}

TEST(parse_struct_forward_ref) {
    Program *p = parse_str(
        "fun @f(p: *@Later) -> i32 {\n"
        "  @entry:\n"
        "    ret i32 0\n"
        "}\n"
        "struct @Later {\n"
        "  @x: i32\n"
        "}");
    ASSERT_NOT_NULL(p);
    ASSERT_EQ(p->func_count, 1);
    ASSERT_EQ(p->functions[0].params[0].type->kind, AST_TYPE_PTR);
    ASSERT_EQ(p->functions[0].params[0].type->ptr_to->kind, AST_TYPE_STRUCT);
    program_free(p);
}

TEST(parse_const_str_inferred) {
    Program *p = parse_str("const @g = \"hello\"");
    ASSERT_NOT_NULL(p);
    ASSERT_EQ(p->global_count, 1);
    ASSERT_EQ(p->globals[0].type->kind, AST_TYPE_ARRAY);
    ASSERT_EQ(p->globals[0].type->array.size, 6);
    ASSERT_EQ(p->globals[0].array_count, 6);
    ASSERT_EQ(p->globals[0].array_init[0], 104);  /* 'h' */
    ASSERT_EQ(p->globals[0].array_init[5], 0);    /* null */
    program_free(p);
}

/* ==================================================================
 * Run all AST / parser tests
 * ================================================================== */

void run_ast_tests(void) {
    printf("\n=== AST / Parser Tests ===\n\n");

    /* AES / v128 */
    RUN_TEST(parse_insn_unsigned_ops);

    /* Global decl */
    RUN_TEST(parse_empty);
    RUN_TEST(parse_global_no_init);
    RUN_TEST(parse_global_with_init);
    RUN_TEST(parse_global_void);
    RUN_TEST(parse_global_float_init);
    RUN_TEST(parse_global_str_init);
    RUN_TEST(parse_global_str_init_escape);
    RUN_TEST(parse_global_str_init_crlf);
    RUN_TEST(parse_struct_forward_ref);
    RUN_TEST(parse_const_str_inferred);

    /* Extern decl */
    RUN_TEST(parse_extern_simple);
    RUN_TEST(parse_extern_void);
    RUN_TEST(parse_extern_multi_param);
    RUN_TEST(parse_extern_with_func);
    RUN_TEST(parse_extern_variadic);

    /* Function decl */
    RUN_TEST(parse_func_no_params);
    RUN_TEST(parse_func_void_return);
    RUN_TEST(parse_func_with_params);
    RUN_TEST(parse_func_multiple_blocks);

    /* Instructions */
    RUN_TEST(insn_let);
    RUN_TEST(insn_let_align);
    RUN_TEST(insn_add);
    RUN_TEST(insn_sub);
    RUN_TEST(insn_mul);
    RUN_TEST(insn_div);
    RUN_TEST(insn_rem);
    RUN_TEST(insn_and_or_xor);
    RUN_TEST(insn_shl_lshr);
    RUN_TEST(insn_ashr);
    RUN_TEST(insn_comparisons);
    RUN_TEST(insn_null);
    RUN_TEST(insn_load);
    RUN_TEST(insn_store);
    RUN_TEST(insn_gep);
    RUN_TEST(insn_jmp);
    RUN_TEST(insn_br);
    RUN_TEST(insn_phi);
    RUN_TEST(insn_ret_value);
    RUN_TEST(insn_ret_void);
    RUN_TEST(insn_call_no_result);
    RUN_TEST(insn_call_with_result);
    RUN_TEST(insn_call_empty_string_arg);
    RUN_TEST(insn_asm_with_result);
    RUN_TEST(insn_asm_void_intel_clobber);
    RUN_TEST(insn_asm_escaped_newline);
    RUN_TEST(insn_asm_alignstack);
    RUN_TEST(insn_asm_named_operands);
    RUN_TEST(insn_asm_struct_result);
    RUN_TEST(insn_convert);

    /* Types */
    RUN_TEST(type_pointer);
    RUN_TEST(type_array);
    RUN_TEST(type_ptr_to_array);

    /* Struct type */
    RUN_TEST(parse_struct_decl);
    RUN_TEST(parse_struct_type_usage);
    RUN_TEST(insn_field);

    /* Enum declarations */
    RUN_TEST(parse_enum_decl);
    RUN_TEST(parse_enum_negative);
    RUN_TEST(parse_enum_member_usage);
    RUN_TEST(parse_enum_float_type_rejected);
    RUN_TEST(parse_enum_non_int_value_rejected);
    RUN_TEST(parse_enum_duplicate_member_rejected);
    RUN_TEST(parse_enum_internal_rejected);

    /* Complete program */
    RUN_TEST(parse_complete_program);

    /* Error handling */
    RUN_TEST(parse_error_empty_global);
    RUN_TEST(parse_error_bad_token);
    RUN_TEST(parse_error_no_brace);
    RUN_TEST(parse_error_asm_missing_dialect);
    RUN_TEST(parse_error_asm_constraint_mismatch);
    RUN_TEST(parse_error_asm_label_constraint);
    RUN_TEST(parse_error_asm_raw_newline);
    RUN_TEST(parse_error_asm_unknown_named_operand);
    RUN_TEST(parse_error_asm_ambiguous_operand_name);
    RUN_TEST(parse_error_asm_struct_no_output);
    RUN_TEST(parse_error_duplicate_local);
    RUN_TEST(parse_error_duplicate_param_local);
    RUN_TEST(parse_error_duplicate_label);
    RUN_TEST(parse_duplicate_local_ok_across_functions);
}
