# SSA — 简单静态单赋值表示

## 概述

SSA（Static Single Assignment）是一种中间表示形式，其核心约束是：**每个变量只赋值一次**。当一个变量在不同控制流路径中有多个可能的值时，使用 `phi` 指令合并。这种形式使数据流分析变得简单直接。

本设计的目标是提供一套**最小完备**的 SSA 指令集——指令数量少（38 条）、语法统一、语义清晰，适合手工编写、教学演示以及 LLM 生成和理解。

### 设计原则（面向 LLM 生成）

SSA 的语法设计围绕一个核心目标：**让 LLM 能以最高准确率生成和理解 SSA 代码**。这要求：

- **模式复用** — 每条新语法必须复用已有模式。`field @S, *%p, %i` 的结构与 `gep type, *%b, %i` 一致，LLM 学会一个就等于学会一对。
- **严格固定** — 看到指令关键字就能确定整行结构，无需上下文推断。`store i32 %v, *%p` 永远是这种形式，没有变体。
- **显式无隐式** — 无类型推导、无默认值、无双关语义。所有信息都在当前行上，LLM 不需要"记住"前面的定义。
- **单前缀约定** — `@` 用于所有全局符号（函数、类型、标签、全局变量），`%` 用于所有局部值。不引入新的前缀符。

## 语法约定

- `@name` — 全局符号（全局变量、函数名、基本块标签）。名称可包含 `.` 用于限定（如枚举成员 `@Color.Red`）
- `%name` — 局部 SSA 值（指令结果、函数参数）
- 字面量：`42`（十进制）、`0xFF`（十六进制）、`3.14`（浮点）、`true`/`false`（布尔）、`null`（空指针常量）、`'A'`（字符，等价于 `65`）
- 字符串字面量：`"hello"` 用于 `const @name: [i8, N] = "..."`，自动添加 null 终止符，支持转义 `\n`/`\r`/`\t`/`\0`/`\\`/`\"`/`\xNN`
- 字符字面量：`'A'`（单字符，转义支持同字符串：`'\n'`、`'\r'`、`'\t'`、`'\0'`、`'\\'`、`'\''`、`'\x41'`）── 词法分析时直接转为整数值，可在任何需要整数的地方使用
- `//` — 行注释
- 每条指令独占一行
- 每个基本块以 `@label:` 开头，以控制流指令（`jmp`、`br`、`ret`）结尾——编译器在解析时检查此项，缺失将产生明确的错误信息而非段错误

---

## 类型系统

| 类型 | 含义 | 示例 |
|------|------|------|
| `i1` | 1 位整数（布尔值） | `true`/`false`，比较指令的结果 |
| `i8` | 8 位整数 | 字节 |
| `i16` | 16 位整数 | 短整数 |
| `i32` | 32 位整数 | 标准整数 |
| `i64` | 64 位整数 | 长整数 |
| `f32` | 32 位浮点数 | 单精度浮点 |
| `f64` | 64 位浮点数 | 双精度浮点 |
| `*T` | 指向 `T` 类型的指针 | `*i32` 表示指向 i32 的指针 |
| `[T, N]` | 包含 N 个 T 类型元素的数组 | `[i32, 10]` 表示 10 个 i32 |
| `@Name` | 用户定义的结构体类型 | `@Point` 引用已声明的 struct Point |
| `void` | 无类型（仅用于函数返回类型） | |

---

## 声明顺序

声明顺序不受限制：

- `struct` 可以在首次使用之后定义（编译器先扫描全部结构体名）
- 函数可以调用文件中**后面**定义的函数（编译器先创建全部函数签名，再生成函数体）

因此文件可以按可读性组织，不必按依赖顺序排列。

## 程序结构

一个 SSA 程序由结构体定义、枚举定义、全局数据定义（可读写或只读常量）、外部函数声明和函数定义组成。

### 结构体定义

```
struct @Name { @field1: type1, @field2: type2, ... }
```

在函数内通过 `let @Name` 在栈上分配结构体实例，通过 `field` 指令访问其字段。结构体定义必须在被引用之前声明。

**示例：**

```
struct @Point { @x: i32, @y: i32 }
struct @Rect { @origin: @Point, @size: @Point }

fun @main() -> i32 {
  @entry:
    %pt = let @Point
    %xp = field @Point, *%pt, 0
    store i32 42, *%xp
    ret i32 0
}
```

> 结构体支持嵌套（一个结构体的字段可以是另一个结构体类型）。字段通过从 0 开始的整数索引访问。

### 全局变量定义

```
global @name: type = init_value
global @name: type                      // 未初始化，默认零值
```

**参数说明：**

| 参数 | 说明 |
|------|------|
| `@name` | 全局变量名称，以 `@` 为前缀 |
| `type` | 变量类型 |
| `init_value` | 初始值（可选，省略则默认零值） |

常量声明使用 `const` 代替 `global`，与 LLVM 的 `constant` 对应，不可变且允许更多优化：

```
const @pi: f64 = 3.14
const @hex: [i8, 16] = [ 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 65, 66, 67, 68, 69, 70 ]
```

字符串字面量初始化（自动追加 `\0` 终止符）：

```
const @fpath: [i8, 14] = "file_test.txt"
const @mode: [i8, 2] = "w"
```

数组类型常量使用方括号 `[ val, val, ... ]` 字面量初始化，每个元素为整数常量。字符串字面量等效于整数数组，支持标准转义序列（`\n`、`\t`、`\0`、`\\`、`\"`、`\xNN`）。

### 枚举定义

```
enum @Name: type { @member1 = value1, @member2 = value2, ... }
```

枚举为一组整数常量提供统一的命名前缀。`type` 必须是整数类型（`i1`/`i8`/`i16`/`i32`/`i64`）；每个成员都必须显式写出整数字面量（十进制、十六进制或负数），不存在隐式自增。

每个成员等价于一个 `internal const` 常量，完整名称为 `@Name.member`：

```
enum @Color: i32 {
  @Red = 0,
  @Green = 1,
  @Blue = 2,
  @White = 0xFF
}

fun @next() -> i32 {
@entry:
  %c = add i32 @Color.Blue, 1     // 3
  ret i32 %c
}
```

- 引用时写完整的限定名 `@Color.Blue`，没有简写 `@Blue`。
- 成员类型与枚举声明的类型一致，可用在任何需要整数操作数的地方（算术、比较、`store`、`call` 实参等）；类型不匹配会报错。
- 枚举本身不是类型：`@Color` 只用于限定成员名，不能出现在类型位置——类型位置直接写 `i32` 等。
- 成员与 `struct` 定义一样是**文件内可见**的（等价于 `internal const`）：每个使用该枚举的 `.bx` 文件都要重复声明，不同文件中的重复声明不会在链接时冲突。`internal enum` 是语法错误——枚举成员始终是文件级的。

### 链接作用域

`const` 和 `global` 默认是**全局可见**的（导出到链接器符号表）。不同 `.bx` 文件中的同名全局声明会在链接时冲突。

`internal` 关键字可用于将作用域限制为当前文件（等价于 C 的 `static const`）：

```
internal const @CHAR_TABLE: [i8, 16] = "0123456789ABCDEF"
internal global @counter: i32 = 0
```

`internal` 标记的符号不导出到链接器符号表，不同文件中的同名 `internal` 声明不会冲突。使用规则：

| 声明形式 | 链接 | 多文件同名 | 跨文件引用 |
|----------|------|-----------|-----------|
| `const @X: i32 = 42` | 全局导出 | 冲突 | 可跨文件访问 |
| `internal const @X: i32 = 42` | 文件作用域 | 不冲突 | 不可跨文件访问 |

对于需要跨多个 `.bx` 文件共享的常量，要么只在一个文件中定义不带 `internal` 的 `const`，要么在每个使用到的文件中各自定义一份副本。

### 函数定义

```
fun @name(param1: type1, param2: type2, ...) -> return_type {
  @entry:
    instr1
    instr2
    ...
}
```

**参数说明：**

| 参数 | 说明 |
|------|------|
| `@name` | 函数名，以 `@` 为前缀 |
| `paramN: typeN` | 参数名和类型，参数在函数体内直接以 `%paramN` 形式使用 |
| `return_type` | 返回值类型，`void` 表示无返回值 |
| `@label:` | 基本块标签 |
| `instr` | SSA 指令序列 |

### 外部函数声明

```
extern @name(param1: type1, param2: type2, ...) -> return_type
```

声明一个外部函数（如 libc 函数），供当前程序调用。外部函数没有函数体，由链接器在链接时解析。

**参数说明：**

| 参数 | 说明 |
|------|------|
| `@name` | 函数名，链接器会按此名称查找符号 |
| `paramN: typeN` | 参数名和类型 |
| `return_type` | 返回值类型 |

**示例：**

```
extern @putchar(c: i32) -> i32
extern @exit(code: i32) -> void
extern @write(fd: i32, buf: *i8, len: i64) -> i64

fun @main() -> i32 {
  @entry:
    call @putchar(65)       // 输出 'A'
    ret i32 0
}
```

> 外部函数声明必须在 `fun` 定义之前，编译器会将其生成为 LLVM 函数声明（无函数体）。链接过程由 `c1` 编译器自动调用系统 `cc` 完成，无需手动指定链接库。使用 `--static` 可将编译结果打包为 `.a` 静态库，供其他程序链接。

---
## 栈分配指令

### `let` — 栈上分配

在函数栈帧上分配一块内存，返回指向该内存的指针。

```
%ptr = let type, align
%ptr = let type               // 使用默认对齐
```

**参数说明：**

| 参数 | 说明 |
|------|------|
| `type` | 要分配的类型 |
| `align` | 可选，对齐字节数 |
| `%ptr` | 结果指针，类型为 `*type` |

> 栈分配在函数返回时自动回收。`let` 指令必须在函数的 `@entry` 基本块中执行（保证可控的栈展开）。

---

## 算术运算指令

所有算术指令的语法统一为：

```
%result = op type %a, %b
```

| 参数 | 说明 |
|------|------|
| `op` | 指令名称 |
| `type` | 操作数类型（i8/i16/i32/i64/f32/f64） |
| `%a, %b` | 两个操作数，类型必须与 `type` 一致 |
| `%result` | 计算结果，类型为 `type` |

### `add` — 加法

```
%r = add i32 %a, %b
```

语义：`%r = %a + %b`

### `sub` — 减法

```
%r = sub i32 %a, %b
```

语义：`%r = %a - %b`

### `mul` — 乘法

```
%r = mul i32 %a, %b
```

语义：`%r = %a × %b`

### `div` — 有符号除法

```
%r = div i32 %a, %b
```

语义：`%r = %a ÷ %b`（向零取整）。若 `%b = 0`，行为未定义。

### `rem` — 有符号取余

```
%r = rem i32 %a, %b
```

语义：`%r = %a - (%a ÷ %b) × %b`，结果的符号与被除数一致。若 `%b = 0`，行为未定义。

---

## 无符号运算指令

### `udiv` / `urem` — 无符号除法与取余

```
%r = udiv type %a, %b
%r = urem type %a, %b
```

与 `div`/`rem` 形式相同，但把操作数按无符号整数解释。

### `mulhi` — 无符号乘法高半

```
%r = mulhi type %a, %b
```

返回两个无符号数乘积的高半部分（结果宽度与操作数相同）。`i64` 上对应 AArch64 的 `umulh` 或 x86-64 的 `mulx`/`rdx`，大数乘法中用来取得 64×64→128 的高位。

## 位运算指令

语法与算术运算一致。

### `and` — 按位与

```
%r = and i32 %a, %b
```

语义：`%r = %a & %b`（按位 AND）

### `or` — 按位或

```
%r = or i32 %a, %b
```

语义：`%r = %a | %b`（按位 OR）

### `xor` — 按位异或

```
%r = xor i32 %a, %b
```

语义：`%r = %a ^ %b`（按位 XOR）

### `shl` — 左移

```
%r = shl i32 %a, %b
```

语义：`%r = %a << %b`。低位补零。若 `%b ≥ 类型位宽`，行为未定义。

### `lshr` — 逻辑右移

```
%r = lshr i32 %a, %b
```

语义：同 `shr`，高位补零。

### `ashr` — 算术右移

```
%r = ashr i32 %a, %b
```

语义：`%r = %a >> %b`（算术右移，高位补符号位）。对负数右移保留符号。若 `%b ≥ 类型位宽`，行为未定义。

---

## 比较运算指令

比较指令返回 `i1` 类型（布尔值）。语法为：

```
%result = op type %a, %b
```

| 指令 | 含义 | 条件 |
|------|------|------|
| `eq` | 相等 | `%a == %b` |
| `ne` | 不等 | `%a != %b` |
| `slt` | 有符号小于 | `%a < %b` |
| `sle` | 有符号小于等于 | `%a <= %b` |
| `sgt` | 有符号大于 | `%a > %b` |
| `sge` | 有符号大于等于 | `%a >= %b` |

> 所有比较指令均按有符号语义比较。对于浮点数，同样使用 `eq`/`ne`/`slt`/`sle`/`sgt`/`sge`。指针类型的空值检查使用 `eq *T %ptr, null`（`null` 是空指针常量）。

---

## 内存访问指令

### `load` — 从内存加载

```
%value = load type, *%ptr
```

**参数说明：**

| 参数 | 说明 |
|------|------|
| `type` | 加载的数据类型 |
| `%ptr` | 源指针，类型必须为 `*type` |
| `%value` | 加载的值，类型为 `type` |

语义：从 `%ptr` 指向的地址读取一个 `type` 类型的值。

### `store` — 写入内存

```
store type %value, *%ptr
```

**参数说明：**

| 参数 | 说明 |
|------|------|
| `type` | 写入的数据类型 |
| `%value` | 要写入的值，类型为 `type` |
| `%ptr` | 目标指针，类型必须为 `*type` |

> `store` 不产生 SSA 值（无 `%result =`）。

### `gep` — 指针元素计算

```
%ptr = gep type, *%base, %index
```

**参数说明：**

| 参数 | 说明 |
|------|------|
| `type` | 元素的类型 |
| `%base` | 基指针，类型为 `*[type, N]` 或 `*type` |
| `%index` | 元素索引（i32），从 0 开始 |
| `%ptr` | 计算后的指针，类型为 `*type` |

语义：等效于 `&base[index]`。将指针向前移动 `%index` 个 `type` 元素。不进行边界检查。

### `field` — 结构体字段访问

```
%ptr = field @StructType, *%base, %field_index
```

**参数说明：**

| 参数 | 说明 |
|------|------|
| `@StructType` | 已声明的结构体类型名称 |
| `%base` | 指向结构体实例的指针，类型为 `*@StructType` |
| `%field_index` | 字段索引（整数常量），从 0 开始 |
| `%ptr` | 指向该字段的指针，类型为该字段类型的指针 |

语义：计算结构体指定字段的内存地址。等价于 C 语言的 `&base->field`。

**示例：**

```
struct @Point { @x: i32, @y: i32 }

%pt = let @Point
%xp = field @Point, *%pt, 0     // 取 x 字段地址，类型 *i32
%yp = field @Point, *%pt, 1     // 取 y 字段地址，类型 *i32
store i32 100, *%xp

// 嵌套结构体
struct @Rect { @origin: @Point, @size: @Point }
%r = let @Rect
%op = field @Rect, *%r, 0               // @origin 字段，类型 *@Point
%xp2 = field @Point, *%op, 0           // origin.x 字段，类型 *i32
```

> `field` 与 `gep` 的区别：`gep` 用于数组元素索引，`field` 用于结构体字段访问。两者底层都映射到 LLVM GEP 指令。

---

## 原子操作指令

原子操作提供多线程环境下的安全内存访问。

### `load atomic` — 原子读取

```
%value = load atomic type, *%ptr, ordering
```

### `store atomic` — 原子写入

```
store atomic type %value, *%ptr, ordering
```

### `atomicrmw` — 原子读-改-写

```
%old = atomicrmw operation, type, *%ptr, %value, ordering
```

操作码：

| 操作码 | 效果 |
|--------|------|
| `xchg` | `old = *ptr; *ptr = value` |
| `add` | `old = *ptr; *ptr = old + value` |
| `sub` | `old = *ptr; *ptr = old - value` |
| `and` | `old = *ptr; *ptr = old & value` |
| `or`  | `old = *ptr; *ptr = old \| value` |
| `xor` | `old = *ptr; *ptr = old ^ value` |

### `cmpxchg` — 比较并交换

```
%ok = cmpxchg type, *%ptr, %cmp, %new, succ_ordering, fail_ordering
```

比较 `*ptr` 与 `%cmp`，若相等则写入 `%new`。返回 `i1` 表示是否成功。`succ_ordering` 和 `fail_ordering` 分别是成功和失败时的内存序。

### 内存序参数

所有原子指令最后必须指定内存序，可选值：

| 关键字 | LLVM 对应 | 说明 |
|--------|-----------|------|
| `seq_cst` | SequentiallyConsistent | 顺序一致性（默认推荐） |
| `acquire` | Acquire | 读获取 |
| `release` | Release | 写释放 |
| `acq_rel` | AcquireRelease | 读获取+写释放 |
| `relaxed` | Monotonic | 仅保证原子性 |

**示例：**

```
%counter = let i32
store i32 0, *%counter

// 原子加 1，返回旧值
%old = atomicrmw add, i32, *%counter, 1, seq_cst

// 原子 CAS
%ok = cmpxchg i32, *%counter, %expected, %new_val, acquire, seq_cst
```

---

## 控制流指令

### `jmp` — 无条件跳转

```
jmp @target
```

**参数说明：**

| 参数 | 说明 |
|------|------|
| `@target` | 目标基本块标签 |

语义：控制流无条件转移到 `@target` 基本块。

### `br` — 条件分支

```
br %cond, @true_label, @false_label
```

**参数说明：**

| 参数 | 说明 |
|------|------|
| `%cond` | 条件值，类型必须为 `i1` |
| `@true_label` | 条件为真时的目标基本块 |
| `@false_label` | 条件为假时的目标基本块 |

语义：若 `%cond = true`，跳转到 `@true_label`；否则跳转到 `@false_label`。

### `phi` — Phi 节点

```
%result = phi type [%val1, @label1], [%val2, @label2], ...
%result = phi [%val1, @label1], [%val2, @label2], ...          // 省略类型时按 i32 处理
```

**参数说明：**

| 参数 | 说明 |
|------|------|
| `[%val, @label]` | 一对值与前驱基本块标签，表示从 `@label` 进入时取 `%val` |
| `%result` | phi 节点结果。类型可以显式写出；省略时 lowering 按 `i32` 处理，因此 64 位值和指针必须写出类型 |

语义：phi 节点根据**控制流来自哪个前驱基本块**来选择对应的值。所有 `%val` 必须具有相同类型。

> Phi 节点的本质是变量的多定义合并——它表示一个变量在不同控制流路径上取不同的值。所有前驱基本块都必须有对应的 `[值, 标签]` 对。

### `ret` — 返回

```
ret type %value
ret void
```

**参数说明：**

| 参数 | 说明 |
|------|------|
| `type %value` | 返回值的类型和值，与函数签名中的返回类型一致 |
| `void` | 无返回值 |

语义：从当前函数返回。若函数返回类型为 `void`，使用 `ret void`。

---

## 函数调用指令

### `call` — 调用函数

```
%result = call @func(%arg1, %arg2, ...)
call @func(%arg1, %arg2, ...)       // 无返回值时
```

**参数说明：**

| 参数 | 说明 |
|------|------|
| `@func` | 被调用函数名 |
| `%argN` | 实参，按位置对应函数定义的形参 |
| `%result` | 函数返回值（若返回类型非 void） |

语义：以给定实参调用函数，实参类型必须与形参类型一一匹配。

**字符串字面量参数：** 在 `call` 的参数位置可以直接使用字符串字面量，解析器自动为其创建匿名 `const` 全局变量并传递指针：

```
call @fwrite("Hello\n", 1, 6, %fp)   // 自动创建 const @_s0: [i8, 6] = [72,101,108,108,111,10,0]
call @print_str("RSA OK\n")          // 自动创建 const @_s1: [i8, 7] = [82,83,65,32,79,75,10,0]
```

这在功能上等价于：
```
const @msg: [i8, 7] = "RSA OK\n"
%p = gep i8, *@msg, 0
call @print_str(%p)
```

字符串字面量参数不支持在非 `call` 上下文（如全局初始化、赋值等）使用——这些场景仍然需要显式的 `const` 声明。

---

## 类型转换指令

### `zext` — 零扩展

```
%result = zext type %value to type
```

将较小整数类型零扩展为较大整数类型，高位补零。

### `sext` — 符号扩展

```
%result = sext type %value to type
```

将较小整数类型符号扩展为较大整数类型，高位补符号位。

### `trunc` — 截断

```
%result = trunc type %value to type
```

将较大整数类型截断为较小整数类型，丢弃高位。

---

## 内联汇编指令

### `asm` — 嵌入目标汇编

```
%result = asm [alignstack] att type, "template", "constraints", type %operand, ...
asm [alignstack] att void, "template", "constraints", type %operand, ...
```

**参数说明：**

| 参数 | 说明 |
|------|------|
| `alignstack` | 可选标记，要求汇编代码保持栈对齐 |
| `att` / `intel` | 汇编方言，必填，无默认值 |
| `type` | 结果类型；无结果时写 `void`；多输出时写结构体类型 |
| `"template"` | 汇编模板，`$0`、`$1`… 引用操作数 |
| `"constraints"` | LLVM 约束串，原样传递 |
| `type %operand` | 输入操作数，类型逐个显式标注 |

语法与 `call` 保持同一模式：类型在前、操作数在后、逗号分隔。

```ssa
%sum = asm att i64, "add $0, $1, $2", "=r,r,r", i64 %a, i64 %b
asm att void, "dmb ish", "~{memory}"
asm alignstack att void, "nop", ""
```

**操作数编号：** 编号顺序由约束串决定——先输出、再输入，与 LLVM 一致。上例中 `$0` 是输出，`$1`/`$2` 是输入。clobber（`~{reg}`、`~{memory}`、`~{cc}`）不消耗操作数。模板中需要字面量 `$` 时写 `$$`。

**命名操作数：** 输入操作数可以用 `${name}` 按其 `%name` 引用，解析期重写为对应的 `${N}`；带修饰符时写 `${name:mod}`（如 `${a:c}`）。数字形式 `${1}` 原样保留。同名操作数出现两次会报歧义错误。

```ssa
%r = asm att i64, "add $0, ${lhs}, ${rhs:l}", "=r,r,r", i64 %lhs, i64 %rhs
```

**多输出（结构体结果）：** 当结果类型是结构体时，`=` 输出约束的数量必须等于结构体字段数，模板按字段顺序写入 `$0`、`$1`…。结果是聚合值，必须先 `store` 到槽位再按字段读取。

```ssa
struct @Pair64 { @lo: i64, @hi: i64 }

%p = asm att @Pair64, "add $0, $2, #1\nadd $1, $2, #2", "=&r,=&r,r", i64 40
store @Pair64 %p, *%slot
%lo_p = field @Pair64, *%slot, 0
```

模板在读完所有输入前就写入多个输出时，输出必须用 `=&`（early-clobber）。否则寄存器分配器可能让输出与输入甚至彼此共用同一个寄存器。

**约束串：**

| 前缀 | 含义 |
|------|------|
| `=` | 输出（单结果时恰好一个；结构体结果时等于字段数） |
| `=&` | 提前抢占的输出 |
| `=*` | 间接输出（内存） |
| 无前缀 | 输入 |
| `~{...}` | clobber |

**校验规则（解析期强制）：**

- 有 `%result` 且类型非结构体时约束串必须恰好有一个 `=` 输出；结构体结果时输出数必须等于字段数（lowering 期校验）；`void` 时必须没有输出。
- 输入约束数量必须等于操作数数量。
- 模板与约束串必须写成字符串字面量，转义规则同普通字符串；**不允许裸换行**，多行模板用 `\n`。
- `asm` 一律带 `sideeffect`，不会被优化删除；但不会隐式声明内存副作用，需要时显式写 `~{memory}`。

**限制：** 不支持 `asm goto`（`!` 标签约束在解析期报错）和 `unwind` 标记。模板由目标平台的集成汇编器解析，指令助记符和寄存器名都属于**目标相关**内容——为 aarch64 写的模板不能编到 x86_64，反之亦然。汇编器报错会使整个编译失败。

---

## 指令速查表

| 类别 | 指令 | 形式 | 含义 |
|------|------|------|------|
| 栈分配 | `let` | `%r = let type` | 栈上分配 |
| 算术 | `add` | `%r = add type %a, %b` | 加法 |
| | `sub` | `%r = sub type %a, %b` | 减法 |
| | `mul` | `%r = mul type %a, %b` | 乘法 |
| | `div` | `%r = div type %a, %b` | 有符号除法 |
| | `rem` | `%r = rem type %a, %b` | 有符号取余 |
| | `udiv` | `%r = udiv type %a, %b` | 无符号除法 |
| | `urem` | `%r = urem type %a, %b` | 无符号取余 |
| | `mulhi` | `%r = mulhi type %a, %b` | 无符号乘法高半 |
| 位运算 | `and` | `%r = and type %a, %b` | 按位与 |
| | `or` | `%r = or type %a, %b` | 按位或 |
| | `xor` | `%r = xor type %a, %b` | 按位异或 |
| | `shl` | `%r = shl type %a, %b` | 左移 |
| | `lshr` | `%r = lshr type %a, %b` | 逻辑右移 |
| | `ashr` | `%r = ashr type %a, %b` | 算术右移 |
| 比较 | `eq` | `%r = eq type %a, %b` | 相等 |
| | `ne` | `%r = ne type %a, %b` | 不等 |
| | `slt` | `%r = slt type %a, %b` | 有符号小于 |
| | `sle` | `%r = sle type %a, %b` | 有符号小于等于 |
| | `sgt` | `%r = sgt type %a, %b` | 有符号大于 |
| | `sge` | `%r = sge type %a, %b` | 有符号大于等于 |
| 内存 | `load` | `%r = load type, *%p` | 从指针读 |
| | `store` | `store type %v, *%p` | 写入指针 |
| | `gep` | `%r = gep type, *%b, %i` | 指针偏移（数组） |
| | `field` | `%r = field @S, *%b, %i` | 结构体字段访问 |
| 原子 | `load atomic` | `%r = load atomic type, *%p, ord` | 原子读 |
| | `store atomic` | `store atomic type %v, *%p, ord` | 原子写 |
| | `atomicrmw` | `%r = atomicrmw op, type, *%p, %v, ord` | 原子读-改-写 |
| | `cmpxchg` | `%r = cmpxchg type, *%p, %cmp, %new, so, fo` | 比较并交换 |
| 控制流 | `jmp` | `jmp @label` | 无条件跳转 |
| | `br` | `br %c, @t, @f` | 条件分支 |
| | `phi` | `%r = phi type [v1, @l1], [v2, @l2]` | Phi 节点 |
| | `ret` | `ret type %v` / `ret void` | 返回 |
| 函数调用 | `call` | `%r = call @f(%args...)` | 调用函数 |
| 类型转换 | `zext` | `%r = zext T %v to T2` | 零扩展 |
| | `sext` | `%r = sext T %v to T2` | 符号扩展 |
| | `trunc` | `%r = trunc T %v to T2` | 截断 |
| 内联汇编 | `asm` | `%r = asm [alignstack] att T, "tpl", "cons", T %a` | 嵌入目标汇编 |

## SSA 规则总结

1. **每个 `%` 值只定义一次**，不可重复赋值
2. **使用前必须先定义**（在支配该使用点的基本块中）
3. **每个基本块以标签开头**，以控制流指令（`jmp`/`br`/`ret`）结尾
4. **Phi 节点出现在基本块开头**，为来自不同前驱的值做合并
5. **类型必须匹配**：所有操作数类型必须与指令声明的类型一致
