# cmap — SSA 的伪 C 可读表示

## 1. 概述

cmap 是一种**伪 C**，把 `.bx` SSA 代码呈现成人能直接读懂的形式。

它不是编程语言：没有编译器、没有语法检查、不要求合法。它的全部意义是**让读者一眼看懂 SSA 在做什么**。

### 1.1 为什么需要它

SSA 为机器处理做了很多牺牲可读性的事：

| 现象 | 例子 |
|---|---|
| **指针舞蹈** | 读一个局部变量要 `%iv = load i32, *%i`，写要 `store i32 0, *%i`，分配要 `%i = let i32` |
| **地址计算外露** | 结构体字段、数组元素必须先 `gep`/`field` 算出指针，再 `load`/`store` |
| **控制流扁平** | 没有 `if`/`while`，只有基本块 + `jmp`/`br` |
| **值爆炸** | 同一个循环变量被写成 `%iv`、`%iv2`、`%iv3`、`%iv4` |

结果：`ssa/c1/test/sort.bx` 里一段 20 行的选择排序展开成 67 行、散落在 7 个基本块里。

cmap 把它还原回去。

### 1.2 设计目标

- **忠实**：SSA 的每条指令、每个类型、每条控制流边都能在 cmap 里找到对应，没有"说不出来"的构造
- **可读**：读起来像普通 C，不需要读者脑内维护基本块和指针
- **机械**：映射规则是确定的，执行映射不需要理解程序的语义

### 1.3 不保证什么

- **不可编译**。`int32`、`φ(...)`、`>>>` 都不是合法 C。cmap 面向人眼，不面向编译器。
- **单向下行**。只定义 SSA → cmap。折叠会丢失信息，不保证能从 cmap 还原出原始 SSA。
- **不做跨函数分析**。不内联、不做常量传播、不消除死代码。一个函数一个函数地看。
- **面向未优化的 SSA**。即 `agents.md` 风格的手写 / LLM 生成代码。LLVM 优化后的 SSA 会含 goto 无法表达的结构，不在范围内。

### 1.4 两层结构

cmap 分两层，各司其职：

| 层 | 性质 | 用途 |
|---|---|---|
| **Core** | 机械 1:1 映射，保留每个基本块标签与 `goto` | 保底。**任何**合法 SSA 都能用 Core 表达 |
| **Structured** | 在 Core 之上重建 `while` / `if` | 可读性。识别不出结构时退回 Core |

映射流程：先转 Core，再尝试结构化。**结构化失败不是错误**——退回 Core 交付即可，信息不丢失。

---

## 2. 类型与字面量

### 2.1 类型

每个 SSA 类型对应唯一的 cmap 类型，无例外、无推断：

| SSA 类型 | cmap 类型 | 说明 |
|---|---|---|
| `i1` | `bool` | 布尔值，比较指令的结果 |
| `i8` | `char` | 字节 |
| `i16` | `int16` | |
| `i32` | `int32` | |
| `i64` | `int64` | |
| `f32` | `float` | |
| `f64` | `double` | |
| `*T` | `T*` | 指针 |
| `[T, N]` | `T[N]` | 数组 |
| `@Name` | `struct Name` | 结构体 |
| `void` | `void` | 仅用于函数返回类型 |

说明：

- `i8` 写作 `char` 而非 `int8`。SSA 的 `i8` 在实践中就是字节缓冲区与字符串元素，`char` 让 `char buf[256]`、`const char msg[] = "..."` 这类代码直接可读。**cmap 中的 `char` 一律按有符号解读**（与 SSA 的比较指令 `slt`/`sle` 等一致）。
- 宽整数写作 `int32`/`int64` 而非 C 的 `int`/`long`，因为 SSA 的宽度是精确的，`int` 的宽度随平台变化，会引入歧义。
- `[T, N]` 写在变量名之后（`int32 arr[6]` 而非 `int32[6] arr`），与 C 的声明语法一致。

### 2.2 字面量

| SSA 字面量 | cmap | 说明 |
|---|---|---|
| `42` | `42` | 十进制整数 |
| `0xFF` | `0xFF` | 十六进制，进制与写法原样保留 |
| `3.14` | `3.14` | 浮点 |
| `true` / `false` | `true` / `false` | `i1` 常量 |
| `null` | `null` | 空指针常量 |
| `'A'` | `'A'` | 字符字面量 |
| `"Hello\n"` | `"Hello\n"` | 字符串字面量 |

两条规则：

- **数值与字符串字面量原样照抄**，格式、进制、转义序列一概不动。
- **字符字面量保留 `'A'` 形式**。SSA 的词法分析会把 `'A'` 直接转成 `65`，语义上二者等价；但 `'A'` 对人可读得多，所以 cmap 保留书写形式。反过来，如果源 SSA 里写的就是 `65`，cmap 照样写 `65`——**cmap 不做反向推断**，只忠实呈现看到的写法。

---

## 3. 顶层结构

### 3.1 结构体定义

```
struct @Name { @field1: type1, @field2: type2, ... }
```

```
struct @Point { @x: i32, @y: i32 }
struct @Rect { @origin: @Point, @size: @Point }
```

```c
struct Point { int32 x; int32 y; };
struct Rect { struct Point origin; struct Point size; };
```

字段取 `@` 前缀去掉后的名字。**字段名是从 struct 定义查表得到的真名**，不是索引——这让后续的 `field` 折叠（见 5.4）能写出 `pt->x` 而不是 `pt->field0`。

嵌套结构体的字段类型 `@Point` 在 cmap 里写作 `struct Point`。

### 3.2 全局数据

| SSA | cmap |
|---|---|
| `global @g: i32 = 5` | `int32 g = 5;` |
| `global @buf: [i8, 64]` | `char buf[64];` |
| `const @PI: f64 = 3.14` | `const double PI = 3.14;` |
| `const @K: [i32, 4] = [1, 2, 3, 4]` | `const int32 K[4] = {1, 2, 3, 4};` |
| `const @msg: [i8, 15] = "Hello, World!\n"` | `const char msg[15] = "Hello, World!\n";` |
| `internal const @T: [i8, 16] = "0123456789ABCDEF"` | `static const char T[16] = "0123456789ABCDEF";` |
| `internal global @counter: i32 = 0` | `static int32 counter = 0;` |

- `global` → 普通全局变量；未初始化时省略初始化器（`char buf[64];`）
- `const` → `const`
- `internal` → `static`（文件作用域）
- 数组字面量 `[ a, b, c ]` → C 的 `{a, b, c}`
- 字符串字面量原样保留，包括转义序列

### 3.3 外部函数声明

| SSA | cmap |
|---|---|
| `extern @putchar(c: i32) -> i32` | `extern int32 putchar(int32 c);` |
| `extern @exit(code: i32) -> void` | `extern void exit(int32 code);` |
| `extern @printf(fmt: *i8, ...) -> i32` | `extern int32 printf(char* fmt, ...);` |

参数名保留（`c`、`code`、`fmt`），这样 `fun` 体里的 `%c` 能对应上。

### 3.4 函数定义

```
fun @name(p1: t1, p2: t2) -> ret_type { ... }
```

```c
ret_type name(t1 p1, t2 p2) {
  ...
}
```

函数参数的映射规则：

- `%p1`（在函数体内引用参数）→ `p1`，无需声明——参数已在签名里
- 参数名不加前缀

例：

```
fun @selection_sort(arr: *i32, n: i32) -> void {
  @entry:
    %iv = load i32, *%n
    ...
}
```

```c
void selection_sort(int32* arr, int32 n) {
  int32 iv = n;              // %iv = load i32, *%n
  ...
}
```

### 3.5 名字作用域

`@` 和 `%` 前缀一律去掉：

| SSA | cmap |
|---|---|
| `@selection_sort` | `selection_sort` |
| `@outer_loop`（块标签） | `outer_loop` |
| `%min_idx`（局部值） | `min_idx` |

**块名和局部名按函数作用域解析。** 不同函数里的 `@done`、`@fail` 互不相干——它们在 cmap 里各自出现在所属函数体内，不会冲突。

---

## 4. Core 层：指令映射

### 4.1 Core 层的形式

Core 层保留 SSA 的基本块结构：

- 每个基本块 → 一个 C 标签
- 块内指令 → 顺序语句
- `jmp` / `br` → `goto`

函数体是**平坦的**（不嵌套大括号），变量声明在函数作用域内，标签是函数内的标签：

```c
void selection_sort(int32* arr, int32 n) {
  int32 i;                                   // %i = let i32
  int32 j;                                   // %j = let i32
  i = 0;                                     // store i32 0, *%i
  goto outer_loop;                           // jmp @outer_loop

outer_loop:
  bool c1 = (i < n);                         // %c1 = slt i32 %iv, %n
  if (c1) goto inner_init; else goto done;   // br %c1, @inner_init, @done

done:
  return;                                    // ret void
}
```

规范：

- **产生结果的指令写成 C 的"声明 + 初始化"**（`bool c1 = ...`），因为 SSA 值的定义点唯一
- 标签独占一行、顶格（与 C 惯例一致），块内语句缩进两格
- `label:` 后直接跟声明在真 C 里需要 C23，伪 C 允许——这是伪 C 的特权

### 4.2 栈分配

| SSA | cmap |
|---|---|
| `%p = let T` | `T p;` 并登记 `%p` 指代变量 `p` |
| `%p = let T, align` | 同上（`align` 在 cmap 中省略） |

`let` 是唯一"只声明不初始化"的映射——因为 SSA 的 `let` 本身也不初始化栈内存（见 `agents.md` 第 10 节）。

```
%i = let i32
%buf = let [i8, 256]
```

```c
int32 i;
char buf[256];
```

`align` 在 cmap 里丢弃：它是给 LLVM 的对齐提示，对读者无意义。

### 4.3 算术

统一形式 `%r = op T %a, %b` → `T r = a op b;`

| SSA | cmap |
|---|---|
| `%r = add i32 %a, %b` | `int32 r = a + b;` |
| `%r = sub i32 %a, %b` | `int32 r = a - b;` |
| `%r = mul i32 %a, %b` | `int32 r = a * b;` |
| `%r = div i32 %a, %b` | `int32 r = a / b;` |
| `%r = rem i32 %a, %b` | `int32 r = a % b;` |

浮点同理（`f64` → `double`，运算符相同）。

`div`/`rem` 是**有符号**运算，而 C 的 `/`、`%` 对有符号类型同样是向零取整、余数随被除数，语义一致。

### 4.4 位运算

| SSA | cmap |
|---|---|
| `%r = and i32 %a, %b` | `int32 r = a & b;` |
| `%r = or i32 %a, %b` | `int32 r = a \| b;` |
| `%r = xor i32 %a, %b` | `int32 r = a ^ b;` |
| `%r = shl i32 %a, %b` | `int32 r = a << b;` |
| `%r = ashr i32 %a, %b` | `int32 r = a >> b;` |
| `%r = lshr i32 %a, %b` | `int32 r = a >>> b;` |

**`>>>` 是 cmap 引入的记号**，表示逻辑右移（高位补零）。C 的 `>>` 对有符号数是算术右移，表达不了逻辑右移；写成 `(unsigned)a >> b` 又会引入 SSA 里根本不存在的无符号类型。用 `>>>` 一目了然。

三个移位在 cmap 里保持可区分：`<<`、`>>`（算术）、`>>>`（逻辑）。

### 4.5 比较

统一形式 `%r = op T %a, %b` → `bool r = (a op b);`（**结果加括号**）

| SSA | cmap |
|---|---|
| `%r = eq i32 %a, %b` | `bool r = (a == b);` |
| `%r = ne i32 %a, %b` | `bool r = (a != b);` |
| `%r = slt i32 %a, %b` | `bool r = (a < b);` |
| `%r = sle i32 %a, %b` | `bool r = (a <= b);` |
| `%r = sgt i32 %a, %b` | `bool r = (a > b);` |
| `%r = sge i32 %a, %b` | `bool r = (a >= b);` |

浮点比较用相同运算符——`double` 的 `<` 就是浮点比较。

**空指针比较**：`null` 在 cmap 里写作 `null`。

| SSA | cmap |
|---|---|
| `%r = eq *i8 %p, null` | `bool r = (p == null);` |
| `%r = ne *i8 %p, null` | `bool r = (p != null);` |

结果加括号，是因为比较值总是要嵌进 `if (...)` 或 `while (...)`。

### 4.6 内存访问

`load`/`store`/`gep`/`field` 的映射**取决于指针的来源**，主体规则在第 5 章。指针无法折叠时（见 5.5）用这里的兜底形式：

| SSA | cmap（兜底） |
|---|---|
| `%v = load T, *%p` | `T v = *p;` |
| `store T %v, *%p` | `*p = v;` |
| `%q = gep T, *%base, %i` | `T* q = &base[i];` |
| `%q = field @S, *%base, %N` | `字段类型* q = &base->字段N;` |

`field` 的兜底形式要查 struct 定义拿字段名。若字段类型是 `@Point`，写 `struct Point* q = &base->origin;`。

### 4.7 原子操作

| SSA | cmap |
|---|---|
| `%v = load atomic T, *%p, ord` | `T v = atomic_load(p, ord);` |
| `store atomic T %v, *%p, ord` | `atomic_store(p, v, ord);` |
| `%old = atomicrmw xchg, T, *%p, %v, ord` | `T old = atomic_xchg(p, v, ord);` |
| `%old = atomicrmw add, T, *%p, %v, ord` | `T old = atomic_add(p, v, ord);` |
| `%old = atomicrmw sub, T, *%p, %v, ord` | `T old = atomic_sub(p, v, ord);` |
| `%old = atomicrmw and, T, *%p, %v, ord` | `T old = atomic_and(p, v, ord);` |
| `%old = atomicrmw or, T, *%p, %v, ord` | `T old = atomic_or(p, v, ord);` |
| `%old = atomicrmw xor, T, *%p, %v, ord` | `T old = atomic_xor(p, v, ord);` |
| `%ok = cmpxchg T, *%p, %c, %n, so, fo` | `bool ok = atomic_cas(p, c, n, so, fo);` |

内存序 `ord` 原样保留（`seq_cst` / `acquire` / `release` / `acq_rel` / `relaxed`）——它是并发语义的核心，不能略。`cmpxchg` 的成功/失败两个内存序都保留。

把 `atomicrmw add` 收拢成 `atomic_add` 更紧凑，同时保留原操作码词根以便对照。

### 4.8 控制流

| SSA | cmap |
|---|---|
| `jmp @L` | `goto L;` |
| `br %c, @T, @F` | `if (c) goto T; else goto F;` |
| `%r = phi [%a, @A], [%b, @B]` | `T r = φ(A → a, B → b);` |
| `ret T %v` | `return v;` |
| `ret void` | `return;` |

**phi 在 Core 层写成显式来源表**：

```
%x = phi [0, @entry], [%next, @body]
```

```c
int32 x = φ(entry → 0, body → next);
```

`φ(A → a, B → b)` 读作：控制流从块 `A` 进来时取 `a`，从块 `B` 进来时取 `b`。C 没有对应物——正因如此 phi 必须显式写出，不能藏起来。多前驱时按 SSA 里的顺序列出全部来源对。

### 4.9 函数调用

| SSA | cmap |
|---|---|
| `%r = call @f(%a, %b)` | `T r = f(a, b);` |
| `call @f(%a, %b)` | `f(a, b);` |

返回类型 `T` 取自 `@f` 的签名。

**字符串字面量参数直接还原成字面量**。SSA 里 `call` 的字符串参数由解析器隐式创建匿名 `const` 全局 + `gep`（见 `ssa.md`），源文件里看不见；cmap 里写回字面量才是最自然的读法：

```
call @printf("Hello, %d\n", %n)
```

```c
printf("Hello, %d\n", n);
```

这些匿名 `const` 的**定义本身不出现在 cmap 里**。它们的内容已经在调用点还原成字面量，再列一遍只是噪音。同理，cmap 里全局符号一律不写 `@`——注意解析器给匿名常量取的名字（`@_s0_0`）是带 `@` 的，而词法层给普通符号的名字不带，两者都要归一到不带 `@` 的形式。

跨 box 调用只是去掉 `@`：

```
%m = call @std_core_math_min(3, 7)
```

```c
int32 m = std_core_math_min(3, 7);
```

### 4.10 类型转换

| SSA | cmap |
|---|---|
| `%r = zext i8 %v to i32` | `int32 r = zext<int32>(v);` |
| `%r = sext i32 %v to i64` | `int64 r = sext<int64>(v);` |
| `%r = trunc i32 %v to i8` | `char r = trunc<char>(v);` |

**不用 C 的强制转换 `(int32)v`**：C 的 cast 语义依赖源与目标类型的有符号性，区分不了"零扩展"和"符号扩展"，而这两个在 SSA 里是不同指令、语义不同。写成 `zext<T>(v)` / `sext<T>(v)` / `trunc<T>(v)` 把意图写在脸上。

### 4.11 内联汇编

汇编模板与约束串原样写出，不翻译、不重排。方言与约束放在行尾注释里，因为它们决定模板每一段怎么读：

| SSA | cmap |
|---|---|
| `%r = asm att i64, "add $0, $1, $1", "=r,r", i64 %a` | `int64 r = __asm("add $0, $1, $1");  /* operands: a */  /* att, constraints "=r,r" */` |
| `asm att void, "dmb ish", "~{memory}"` | `__asm("dmb ish");  /* att, constraints "~{memory}" */` |
| `asm alignstack att void, "nop", ""` | `__asm("nop");  /* alignstack att, constraints "" */` |

操作数按 SSA 顺序列出，`$N` 编号保持原样——编号由约束串决定（先输出后输入），cmap 不重新编号，否则读者对不上模板里的 `$N`。`${name}` 在解析期已重写为 `${N}`，cmap 同样按位号输出。

---

## 5. 指针折叠

### 5.1 折叠是什么

SSA 访问一处存储要三步：`let` 拿指针 → `gep`/`field` 偏移 → `load`/`store` 访问。这三步对机器必要，对读者全是噪音。

折叠就是：**如果一个 SSA 值指向一处可命名的存储，就把它读作那处存储的名字。**

### 5.2 三类可折叠的指针来源

只有三类 SSA 值能"命名"存储：

| 来源 | 指针值 | 指代的名字 | 登记为 |
|---|---|---|---|
| `%p = let T` | `%p` | 变量 `p` | `%p` ≡ 变量 `p` |
| `%q = gep T, *%base, %i` | `%q` | `base[i]` 的位置 | `%q` ≡ `&base[i]` |
| `%q = field @S, *%base, %N` | `%q` | `base->字段N` 的位置 | `%q` ≡ `&base->字段N` |

`gep`/`field` 的 `base` 可以有不同来源，都可折叠：

| `base` 的来源 | 例 | 折叠为 |
|---|---|---|
| `let` 出的数组 | `%arr = let [i32, 6]` | `arr[i]` |
| **全局数组 / 字符串常量** | `const @_b64_chars: [i8, 64] = "..."` | `_b64_chars[i]` |
| 已折叠的名字（链式） | `%sr = field ...` 后再 `gep` | 见 5.4 |

全局数组的 `gep` 常用于查表与取字符串常量地址：

```
%c  = gep i8, *@_b64_chars, %idx      // 查表
%v  = load i8, *%c

%msg = gep i8, *@hello, 0             // 取字符串常量首地址（等价于数组名本身）
call @print_str(%msg)
```

```c
char c = _b64_chars[idx];

print_str(hello);
```

`gep T, *@arr, 0` 索引为 0 时，折叠结果就是数组名本身，不必写成 `arr[0]` 的地址形式。

其中 `base` 本身可能是已折叠的名字，于是形成链（见 5.4）。

### 5.3 三条展开规则

登记之后，所有使用该指针的地方按下面展开：

**规则 A — 作为 `load` 的源：写名字**

| SSA | cmap |
|---|---|
| `%v = load i32, *%i`（`%i` 是 let 变量 `i`） | `i` |
| `%v = load i32, *%p`（`%p` ≡ `&arr[j]`） | `arr[j]` |
| `%v = load i32, *%f`（`%f` ≡ `&pt->x`） | `pt->x` |

**规则 B — 作为 `store` 的目标：名字在左侧**

| SSA | cmap |
|---|---|
| `store i32 0, *%i` | `i = 0;` |
| `store i32 %v, *%p`（`%p` ≡ `&arr[j]`） | `arr[j] = v;` |
| `store i32 42, *%f`（`%f` ≡ `&pt->x`） | `pt->x = 42;` |

**规则 C — 作为地址传递（`call` 实参）：加 `&`**

| SSA | cmap |
|---|---|
| `call @f(%i)`（`%i` 是 `let i32` 的变量） | `f(&i);` |
| `call @selection_sort(%arr, 6)`（`%arr` 是 `let [i32, 6]`） | `selection_sort(arr, 6);` |

数组例外：`let [T, N]` 的变量名在 C 里本身就是地址，不加 `&`。

### 5.4 链式折叠

`field` 的结果可以再被 `field`/`gep` 作用，形成链。折叠时把整条链读成一个成员访问表达式：

```
%t12 = field @TLSContext,   *%ctx, 0     // @TLSContext 的字段 0 名为 tls12
%sr  = field @TLS12Context, *%t12, 14    // 该字段类型为 [i8, 64]
%p   = gep   i8,            *%sr,  0
%v   = load  i8,            *%p
```

逐步登记：

| 值 | 登记为 |
|---|---|
| `%t12` | `&ctx->tls12` |
| `%sr` | `&ctx->tls12.sr` |
| `%p` | `&ctx->tls12.sr[0]` |

最终：

```c
char v = ctx->tls12.sr[0];
```

整个链折叠成一个表达式，中间三个指针值都不出现在 cmap 里。`field → gep`（结构体里的数组字段）同理折叠为 `ctx->buf[i]`。

### 5.5 无法折叠的情形

下列情况下指针值无法命名，退回 4.6 节的兜底形式，保留显式指针变量：

- **指针被存入内存**（`store *T %p, *%slot`）——它不再唯一对应一处存储
- **指针参与指针算术或比较**，且其来源不是 `let`
- **指针跨函数边界传递**——只有函数内可见的链才可折叠
- `let` 出现在**非 `@entry` 块**（实测存在，如 `socket.bx @recv`）：变量仍在首次 `let` 处声明，只是不在函数顶部。**不要假定所有声明都在开头**

### 5.6 折叠时的来源标注

折叠会让 SSA 里的值名消失（`%iv`、`%iv2`、`%iv3` 全变成 `i`）。为了仍能对着原 SSA 读，**在变量声明处标注 SSA 来源**：

```c
int32 i;                    // %i = let i32
int32 min_idx;              // %min_idx = let i32
int32 j_start = i + 1;      // %j_start = add i32 %iv2, 1
```

标注规则：

- **`let` 折叠出的变量**：声明行尾标注 `// %<原名> = let <类型>`
- **计算值**（`add`、`slt` 等）：名字本身保留着（`j_start`），无需标注
- **`load`/`gep`/`field` 折叠出的别名**（`%iv`、`%pj`）：**不标注**。它们指向的存储已在声明处标注过；逐点标注会让代码无法阅读

需要对每个值逐一对号时，用第 8 章那种 SSA ↔ cmap 并列格式，而不是往 cmap 里塞满注释。

### 5.7 快照值不折叠

规则 A（`load` 折叠成存储名）有一个例外：**如果 load 的结果被写进另一处存储，它就不能折叠**。

```
%vi = load i32, *%pi        // %pi ≡ &arr[i]
%vm = load i32, *%pm        // %pm ≡ &arr[min_idx]
store i32 %vm, *%pi
store i32 %vi, *%pm
```

若把 `%vi` 折叠成 `arr[i]`，最后一行就变成 `arr[min_idx] = arr[i]` —— 但此时 `arr[i]` 已被上一行改写，**语义就错了**。`%vi` 是一份快照，必须保持独立：

```c
int32 vi = arr[i];
int32 vm = arr[min_idx];
arr[i] = vm;
arr[min_idx] = vi;
```

判定规则：

| store 的目标 | 处理 |
|---|---|
| `let` 出的标量变量（`store i32 %v, *%min_idx`） | 不涉及别名，`%v` 正常折叠 |
| 数组元素、结构体字段、来源不明的指针（参数等） | 视为可能别名 → load 结果保留为独立变量 |

这条规则不做精确的别名分析——只要**可能**别名就保守保留。宁可多出一个临时变量，也不产出语义错误的代码。

---

## 6. Structured 层：控制流重建

### 6.1 什么时候用

Structured 层叠在 Core 之上。识别成功 → 更好读；识别失败 → 退回 Core。

**回退不是错误，是预期行为。** 宁可交付一个朴素的 goto 版本，也不猜一个可能猜错的结构。

### 6.2 while 循环

识别模式：某块 `@cond` 以 `br %c, @body, @exit` 结尾，且 `@body` 可达 `@cond`（存在回边），`@exit` 在循环体外。

**`br` 的两个目标谁写在前面并不固定。** `br %c, @body, @exit` 和 `br %c, @exit, @body` 一样常见——后者读作 `if (cond) break;`，在 `pem.bx` 里就有。判断依据是**哪个目标能到达回边源**，而不是操作数顺序；当循环体落在 false 分支时，条件取反后输出 `while (!(c))`。

```
@init:  ...              jmp @cond          // 可选
@cond:  ...  br %c, @body, @exit
@body:  ...              jmp @cond          // 或经其他块回到 @cond
```

```c
<init 语句>
while (c) {
  <body 语句>
}
<@exit 的内容>
```

要点：

- 条件表达式取自循环头的 `br`
- **循环体内多条路径回到 `@cond` 是正常的**，C 的 `while` 天然支持。实测 `convert.bx` 的 `@parse_loop` 有 4 条回边，一样能重建
- `@exit` 的代码接在 `while` 之后

### 6.3 if / if-else

识别模式：`@cur` 以 `br %c, @T, @F` 结尾，两支最终汇聚到同一块 `@J`（`@J` 不等于 `@T`、`@F`）。

```
@cur:  ...  br %c, @T, @F
@T:    ...  jmp @J
@F:    ...  jmp @J
@J:    ...
```

```c
if (c) {
  <@T 内容>
} else {
  <@F 内容>
}
<@J 内容>
```

变体：

| 形态 | cmap |
|---|---|
| `@F == @J` | `if (c) { <@T> }` |
| 两支都以 `ret` 结尾 | `if (c) { return ...; } else { return ...; }` |
| 仅 `@F` 以 `ret` 结尾 | `if (c) { <@T> }`，`@F` 的 `return` 接在 `if` 之后 |
| 仅 `@T` 以 `ret` 结尾 | `if (c) { return ...; }`，`@F` 的内容接在 `if` 之后 |
| `@T == @F` | `if (c) goto T;` |
| `@F` 指向 `@cur` 自身 | 这是循环，按 6.2 处理 |

### 6.4 链式 br 即 if-else if

IR 没有 `switch`，多路分支写成链式 `br`：

```
@a:  br %c1, @x, @b
@b:  br %c2, @y, @z
```

```c
if (c1) { goto x; }
else if (c2) { goto y; }
else { goto z; }
```

各支若还能继续结构化，则递归展开。

### 6.5 内联糖

Structured 层允许把**只被使用一次**的计算值内联到使用处：

```
%j_start = add i32 %iv2, 1
store i32 %j_start, *%j
```

- Core 层：`int32 j_start = i + 1;` 换行 `j = j_start;`
- Structured 层：`j = i + 1;`

这是 Structured 层唯一的额外折叠。它让代码更接近手写 C，代价是丢掉 `%j_start` 这个名字——所以**需要严格对照时看 Core 层**。

### 6.6 回退条件

出现下列任一情况，该函数退回 Core 层，不做结构猜测：

1. **循环有循环头条件之外的退出边**（`break` 语义）。保守规则不重建 `break`。
   实例：`hashmap.bx` 的 `@gnext: br %gmore, @gloop, @gmiss`——`@gmiss` 是"跳出循环、继续后面的代码"。
   注意：循环体内直接 `return` **不需要**回退，C 的 `return` 直接可用（实例：`hashmap.bx` 的 `@iput`）。
2. **某块有超过 2 个前驱，且入口需要 phi**——多路汇聚降不成 if/else。
3. **不可归约 CFG**（循环有多个入口）。
4. **`jmp` 转发链**导致块结构有歧义。

实测：2/3/4 在当前代码库中均不存在，1 存在（`hashmap.bx`），所以回退是少数情况。

---

## 7. phi 处理

### 7.1 Core 层：显式来源表

```c
int32 x = φ(entry → 0, body → next);
```

不做任何消解——来源信息完整保留。

### 7.2 Structured 层：消解

**循环头的 phi** → 循环前初始化 + 循环末尾回写：

```c
int32 x = 0;                 // 来自 entry
while (...) {
  ...
  x = next;                  // 来自 body 的回边
}
```

**if 汇聚处的 phi** → 提前声明 + 各支赋值：

```c
int32 x;                     // 声明提前到 if 之前
if (c) { x = a; } else { x = b; }
```

### 7.3 无法消解时

- phi 有**超过 2 个前驱** → 退回 Core，用 `φ` 记号
- phi 的值来自**非相邻块** → 同上

实测：全库 phi 均为 2 前驱，且循环变量几乎全走 `let` + `store`/`load`（与 `agents.md` 宣称的"phi 优先"相反），因此 7.2 的两条规则已覆盖实际用例。

---

## 8. 完整示例

`ssa/c1/test/sort.bx` 的 `@selection_sort`——一个双层循环的选择排序。

### 8.1 SSA 原文（67 行）

```
fun @selection_sort(arr: *i32, n: i32) -> void {
  @entry:
    %i = let i32
    %j = let i32
    %min_idx = let i32
    store i32 0, *%i
    jmp @outer_loop

  @outer_loop:
    %iv = load i32, *%i
    %c1 = slt i32 %iv, %n
    br %c1, @inner_init, @done

  @inner_init:
    %iv2 = load i32, *%i
    store i32 %iv2, *%min_idx
    %j_start = add i32 %iv2, 1
    store i32 %j_start, *%j
    jmp @inner_cond

  @inner_cond:
    %jv = load i32, *%j
    %c2 = slt i32 %jv, %n
    br %c2, @inner_body, @swap_step

  @inner_body:
    %jv2 = load i32, *%j
    %pj = gep i32, *%arr, %jv2
    %vj = load i32, *%pj

    %mv = load i32, *%min_idx
    %pm = gep i32, *%arr, %mv
    %vm = load i32, *%pm

    %lt = slt i32 %vj, %vm
    br %lt, @update_min, @inner_next

  @update_min:
    %jv3 = load i32, *%j
    store i32 %jv3, *%min_idx
    jmp @inner_next

  @inner_next:
    %jv4 = load i32, *%j
    %j_next = add i32 %jv4, 1
    store i32 %j_next, *%j
    jmp @inner_cond

  @swap_step:
    %iv3 = load i32, *%i
    %pi = gep i32, *%arr, %iv3
    %vi = load i32, *%pi

    %mv2 = load i32, *%min_idx
    %pm2 = gep i32, *%arr, %mv2
    %vm2 = load i32, *%pm2

    store i32 %vm2, *%pi
    store i32 %vi, *%pm2

    %i_next = add i32 %iv3, 1
    store i32 %i_next, *%i
    jmp @outer_loop

  @done:
    ret void
}
```

### 8.2 Core 层（48 行）

```c
void selection_sort(int32* arr, int32 n) {
  int32 i;                                          // %i = let i32
  int32 j;                                          // %j = let i32
  int32 min_idx;                                    // %min_idx = let i32
  i = 0;                                            // store i32 0, *%i
  goto outer_loop;                                  // jmp @outer_loop

outer_loop:
  bool c1 = (i < n);                                // %c1 = slt i32 %iv, %n
  if (c1) goto inner_init; else goto done;          // br %c1, @inner_init, @done

inner_init:
  min_idx = i;                                      // store i32 %iv2, *%min_idx
  int32 j_start = i + 1;                            // %j_start = add i32 %iv2, 1
  j = j_start;                                      // store i32 %j_start, *%j
  goto inner_cond;                                  // jmp @inner_cond

inner_cond:
  bool c2 = (j < n);                                // %c2 = slt i32 %jv, %n
  if (c2) goto inner_body; else goto swap_step;

inner_body:
  int32 vj = arr[j];                                // %vj = load i32, *%pj
  int32 vm = arr[min_idx];                          // %vm = load i32, *%pm
  bool lt = (vj < vm);                              // %lt = slt i32 %vj, %vm
  if (lt) goto update_min; else goto inner_next;

update_min:
  min_idx = j;                                      // store i32 %jv3, *%min_idx
  goto inner_next;                                  // jmp @inner_next

inner_next:
  int32 j_next = j + 1;                             // %j_next = add i32 %jv4, 1
  j = j_next;                                       // store i32 %j_next, *%j
  goto inner_cond;                                  // jmp @inner_cond

swap_step:
  int32 vi = arr[i];                                // %vi = load i32, *%pi
  int32 vm2 = arr[min_idx];                         // %vm2 = load i32, *%pm2
  arr[i] = vm2;                                     // store i32 %vm2, *%pi
  arr[min_idx] = vi;                                // store i32 %vi, *%pm2
  int32 i_next = i + 1;                             // %i_next = add i32 %iv3, 1
  i = i_next;                                       // store i32 %i_next, *%i
  goto outer_loop;                                  // jmp @outer_loop

done:
  return;                                           // ret void
}
```

注意中间那一堆指针是怎么消失的：

| SSA 值 | 登记 | 用法 |
|---|---|---|
| `%pj` | `&arr[j]` | `%vj = load i32, *%pj` → `arr[j]` |
| `%pm` | `&arr[min_idx]` | `%vm = load i32, *%pm` → `arr[min_idx]` |
| `%pi` | `&arr[i]` | `store i32 %vm2, *%pi` → `arr[i] = vm2;` |
| `%pm2` | `&arr[min_idx]` | `store i32 %vi, *%pm2` → `arr[min_idx] = vi;` |

同时 `%iv`、`%iv2`、`%iv3`、`%jv`、`%jv2`、`%jv3`、`%jv4` 七个别名全部折叠成 `i` 和 `j`。

### 8.3 Structured 层（25 行）

```c
void selection_sort(int32* arr, int32 n) {
  int32 i;                                          // %i = let i32
  int32 j;                                          // %j = let i32
  int32 min_idx;                                    // %min_idx = let i32

  i = 0;                                            // store i32 0, *%i
  while (i < n) {                                   // @outer_loop
    min_idx = i;                                    // @inner_init
    j = i + 1;                                      // @inner_init（内联糖）

    while (j < n) {                                 // @inner_cond
      if (arr[j] < arr[min_idx]) {                  // @inner_body
        min_idx = j;                                // @update_min
      }
      j = j + 1;                                    // @inner_next（内联糖）
    }

    // @swap_step：交换 arr[i] 与 arr[min_idx]
    int32 vi = arr[i];
    int32 vm2 = arr[min_idx];
    arr[i] = vm2;
    arr[min_idx] = vi;

    i = i + 1;                                      // @swap_step（内联糖）
  }
}                                                   // @done: ret void
```

三层的规模：**SSA 67 行 → Core 48 行 → Structured 25 行**。

Structured 层识别到的结构：

| 结构 | 对应块 |
|---|---|
| 外层 `while (i < n)` | 循环头 `@outer_loop`，回边来自 `@swap_step` |
| 内层 `while (j < n)` | 循环头 `@inner_cond`，回边来自 `@inner_next` |
| `if (arr[j] < arr[min_idx])` | `@inner_body` 的 `br`，两支汇聚到 `@inner_next` |
| 内层循环的 `@exit` = `@swap_step` | 落在外层循环体内，故后续语句接在内层 `while` 之后 |

内层循环的出口 `@swap_step` 不是函数出口，而是**外层循环体的一部分**——这正是嵌套循环在 CFG 里的样子，结构化时按"内层 `while` 结束后继续"处理。

---

## 9. 限制与非目标

### 9.1 不可编译

`int32`、`φ(...)`、`>>>`、`zext<T>(v)`、`label:` 后直接跟声明——这些都不是合法 C。cmap 面向人眼。不要试图用 C 编译器验证它。

### 9.2 单向下行，不保证可逆

折叠会丢信息：`%iv`/`%iv2`/`%iv3` 都变成 `i` 之后，无法还原出原来用了几个别名。第 8 章的并列格式是给人工对照用的，不是可逆编码。

### 9.3 泛型容器无法友好表达

SSA 没有泛型，`ssa/boxes/std/data/stack.bx` 这类容器用 `*i8` + `elem_size` 实现类型无关：

```
%stack = let [i8, 256]
call @std_data_stack_init(%stack, 64, 4)
```

```c
char stack[256];
std_data_stack_init(stack, 64, 4);
```

元素类型和长度信息在 cmap 里**完全丢失**——它看起来就是一片字节缓冲区。这是 SSA 层面就缺失的信息，cmap 无法补回来。读者需要结合调用约定自行还原。

### 9.4 不做跨函数分析

不内联、不做常量传播、不消除死代码。一个函数一个函数地读。跨 box 调用只显示为普通函数调用（`std_core_math_min(3, 7)`）。

### 9.5 只面向未优化的 SSA

即 `agents.md` 风格的手写 / LLM 生成代码：所有 `let` 集中在少数位置、每个基本块以 `jmp`/`br`/`ret` 结尾、块名有意义。

LLVM 优化后的 SSA 含 goto 无法表达的结构（如 `select`、多路 phi、跨块的值重用），不在范围内。

### 9.6 其他退化情形

| 情形 | 处理 |
|---|---|
| `let` 出现在非 `@entry` 块 | 变量仍在首次 `let` 处声明，不假定声明都在函数顶部 |
| `let [i8, 0]` 空数组 | `char x[0];`（照写） |
| `let @Struct` | `struct S x;`（照写） |
| 块名跨函数重名（`@done` 31 处、`@fail` 223 处） | 按函数作用域解析，互不相干 |
| `call` 传入字符串字面量 | 直接还原为字面量，不显示匿名 `const` 全局 |
