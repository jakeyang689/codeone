# Agents — AI Agent 生成 SSA 编码规则

本文档约束 AI agent 在生成 `.bx` SSA 代码时的风格和模式。与 `ssa.md`（语言规范）、`box.md`（标准库规范）配合使用。

## 1. 变量命名

| 模式 | 命名 | 示例 |
|------|------|------|
| 循环索引 | `%i`, `%j`, `%k` | |
| 长度/计数 | `%len`, `%count`, `%n` | |
| 指针 | `%p`, `%<name>_ptr` | `%bp`, `%keys_ptr` |
| 从 load 重用的值 | 源名 + 后缀 | `%i` → `%iv`, `%j` → `%jv` |
| 比较结果 | 有意义的缩写 | `%eq`, `%lt`, `%neg`, `%free`, `%done` |
| 临时结果 | 不用 `%1`, `%2` 编号 | 用 `%tmp` 或上下文名 |
| GEP 计算的指针 | `%p` + 数组名 | `%pi`, `%pj`, `%pk` |
| 函数参数 | 直接用参数名 | `%key`, `%val`, `%arr` |
| 测试结果 | `%r0`, `%r1` ...（按测试编号） | |
| 字符比较 | `%c0v`, `%c5v`, `%dc0v` | 字符索引 + `v` 后缀 |

## 2. 基本块命名

| 模式 | 标签 | 使用场景 |
|------|------|----------|
| 入口 | `@entry` | 每个函数的首个基本块 |
| 循环三件套 | `@<prefix>_cond` / `@<prefix>_body` / `@<prefix>_next` | `@init_cond`, `@inner_body`, `@inext` |
| 条件分支 | 描述性名称 | `@update_min`, `@swap_step`, `@fix`, `@gfound` |
| 汇聚出口 | `@done` | 循环/函数结束点 |
| 测试 sink | `@pass` / `@fail` | 测试通过/失败出口 |
| 线性断言链 | 测试编号 | `@t2`, `@c7`, `@v_h`, `@e1` |

函数内块名不能重复，且应在函数名上下文中有含义（不必全局唯一）。

## 3. Phi 与内存模式

`phi` 指令直接降低到 LLVM phi，用于在基本块入口合并不同前驱路径的值：

```
%val = phi i32 [%a, @then], [%b, @else]
```

类型建议显式写出。省略时 lowering 按 `i32` 处理，因此 64 位值和指针必须写类型。

`phi` 必须出现在基本块开头、所有非 phi 指令之前。所有前驱基本块都必须有对应的 `[值, 标签]` 对。

当 `phi` 不方便使用时（如循环内需要在不同迭代间持久化状态），用 `let` + `store` + `load` 替代：

```
// phi 版本（适用于简单合并）：
@entry:
  jmp @loop
@loop:
  %i = phi [0, @entry], [%next, @body]
  ...
@body:
  ...
  %next = add i32 %i, 1
  jmp @loop

// 内存版本（适用于持久化/复杂控制流）：
@entry:
  %iptr = let i32
  store i32 0, *%iptr
  jmp @loop
@loop:
  %i = load i32, *%iptr
  ...
@body:
  ...
  %next = add i32 %i, 1
  store i32 %next, *%iptr
  jmp @loop
```

规则：
- `phi` 优先于 `let/store/load`——生成的 IR 更精简，LLVM 优化器处理更好
- 循环变量持久化用 `let/store/load`——循环跨迭代需要可变的栈变量
- `let` 在使用前分配，同一作用域不重复 `let` 同名变量
- 指针通过 `gep` 计算，不硬编码地址偏移
- 避免跨基本块的 `let`——在需要的块内就近分配

## 4. 循环结构

每个循环用规范的**三个基本块**：

```
@entry:           // 1. 初始化
  %i = let i32
  store i32 0, *%i
  jmp @cond

@cond:            // 2. 条件检查
  %iv = load i32, *%i
  %c = slt i32 %iv, %n
  br %c, @body, @done

@body:            // 3. 循环体
  ...
  %iv2 = load i32, *%i
  %next = add i32 %iv2, 1
  store i32 %next, *%i
  jmp @cond
```

循环变量更新始终在 body 末尾，以 `jmp @cond` 结尾。

## 5. 函数结构

- `@entry` 放所有 `let` 分配和初始 store，然后 `jmp` 到第一个逻辑块
- 每个基本块以 **控制流指令**（`jmp`、`br`、`ret`）结尾——编译器在解析时检查此项，缺失会产生清晰错误，不再是静默 SIGSEGV
- 控制流指令不能出现在块的中间位置
- 不引用不存在的 `@label`

## 6. 测试函数模式

使用 `c1 test` 子命令：

```
fun @test_xxx() -> i32 { ... }    // 返回 0 通过，非 0 失败
```

内联断言模式：
```
  %eq = eq i32 %actual, %expected
  br %eq, @pass, @fail
@pass:
  ret i32 0
@fail:
  ret i32 1
```

规则：
- 测试函数名以 `@test_` 开头
- 签名固定 `() -> i32`
- 不共享可变状态
- 每个测试独立可重复执行

## 7. Box 编写约定

函数命名 `@<dir>_<file>_<name>`：

| 文件路径 | 函数名示例 |
|----------|-----------|
| `boxes/std/core/math.bx` | `@std_core_math_min` |
| `boxes/std/io/print.bx`  | `@std_io_print_i32` |
| `boxes/std/net/socket.bx` | `@net_socket_create`（`net/` 沿用 `@net_` 前缀） |

- 写注释说明参数含义和前置条件
- 避免全局可变状态（`global` 少用，`const` 可以）

## 7a. 测试要求

在 `boxes/std/` 下的 `.bx` 文件中新增函数时，必须在 `c1/test/` 中为该函数添加单元测试：

| 场景 | 测试方式 | 示例 |
|------|----------|------|
| 新 `.bx` 文件 | 新建 `c1/test/<name>_test.bx` | `boxes/std/crypto/sha256.bx` → `c1/test/sha256_test.bx` |
| 已有 `.bx` 文件新增函数 | 在已有测试文件中追加 | `boxes/std/mem/alloc.bx` 加 `@malloc_aligned` → 在 `c1/test/std_test.bx` 追加 |

规则：
- 测试覆盖**至少一个已知结果**（用 hardcoded 期望值做断言）
- 测试文件必须有 `@main` 入口函数，返回 `0` 通过、`1` 失败
- 编译方式：`./c1 test/<name>.bx ../boxes/std/.../<file>.bx -o test/<name>`

## 8. 避免的反模式
- 不在非尾部位置写控制流指令
- 不依赖隐式类型推断——类型始终显式写出
- 不引用不存在的 `@label`
- 不重复分配已存在的栈变量
- 不在不同文件中定义同名函数
- 函数参数名不要用 `%` 前缀重命名——直接使用参数名
- **不通过 `putchar` 逐字符输出字符串**——应使用 `call @fwrite("...", 1, N, %fp)` 或预先定义的 `print_str` 一次性写入
- **变参 extern 函数必须声明 `...`**——例如 `extern @printf(fmt: *i8, ...) -> i32`。遗漏 `...` 会使 LLVM 函数类型被标记为非变参，`-O2` 下优化器会产生错误的参数传递代码
- **一组相关的整数常量用 `enum` 声明**——如 `enum @Color: i32 { @Red = 0, @Green = 1 }`；成员用完整限定名 `@Color.Red` 引用，值必须显式写出（无隐式自增）
- `const` 不带 `internal` 时为**全局可见**——不同 `.bx` 文件中的同名 `const` 会在链接时冲突。`internal const` 是**文件作用域**，同名不冲突但也不能跨文件引用。需要在多文件间共享的常量，用不带 `internal` 的 `const` 且只在其中一个文件中定义，或每个文件各自定义一份

## 9. 结构体约束

能用结构体表达的数据，**必须**使用 `struct` + `field` 访问，不能用扁平的 `[i8, N]` 数组加硬编码偏移。

```
// 错误：用扁平数组 + 硬编码偏移
%ctx = let [i8, 288]
%fd_p = gep i8, *%ctx, 0       // fd at offset 0
%fd = load i32, *%fd_p
%ver_p = gep i8, *%ctx, 8      // version at offset 8
store i32 771, *%ver_p

// 正确：用结构体
struct @MyContext {
  @fd: i32,
  @pad: [i8, 4],
  @version: i32,
}
%ctx = let @MyContext
%fd_p = field @MyContext, *%ctx, 0
%fd = load i32, *%fd_p
%ver_p = field @MyContext, *%ctx, 2
store i32 771, *%ver_p
```

规则：
- 多个关联字段构成一个逻辑实体时，定义 `struct` 并用 `field` 访问
- 用 `struct` 表达的代码中，`field` 访问的是具名字段索引（0, 1, 2...），而非内存字节偏移——LLVM 会自动处理填充对齐
- 禁止在 `[i8, N]` 上用 `gep` 加硬编码常量访问不同字段

## 10. 栈初始化约束

`let` 分配栈空间但**不进行零初始化**——分配后的内存内容是不确定的（栈残留数据）：

```
// 错误：假设 buf 内容是确定的或全零的
%buf = let [i8, 64]
// 此时 buf[0..63] 内容为栈垃圾，不可预测

// 正确：显式初始化
%buf = let [i8, 64]
%bp = gep i8, *%buf, 0
%n64 = let i64
store i64 64, *%n64
%n = load i64, *%n64
call @memset(%bp, 0, %n)
```

规则：
- 所有 `let` 分配的栈内存，内容都是**未定义的**
- 需要确定初始值（包括全零）时，必须显式通过 `store` 或 `call @memset` 初始化

## 11. 内联汇编约束

`asm` 只用于标准库尚未暴露的**硬件指令**（如屏障、专用寄存器访问）。它不是绕过 SSA 语义的通道：

- 优先使用现有指令或 `boxes/std/` 函数；只有在没有等价能力时才写 `asm`
- 汇编代码集中放在**单一包装模块**中，通过普通 `fun` 暴露，调用方看不到 `asm`
- 模板是**目标相关**的：aarch64 与 x86_64 必须分开模块/函数，不能用同一份模板跨目标编译
- 有内存副作用时必须显式写 `~{memory}`，`asm` 不会隐式声明
- 模板用 `\n` 分隔多行，禁止裸换行；操作数编号从输出开始（`$0` 是第一个输出）
- 输入较多时优先用 `${name}` 引用 `%name`，比数 `$N` 更不易错；`${name:mod}` 可带修饰符
- 多输出用结构体结果，输出约束数与字段数一致；结果是聚合值，必须 `store` 后按字段读
- 模板在读完输入前写入多个输出时，输出必须用 `=&`，否则分配器可能让输出与输入共用寄存器
