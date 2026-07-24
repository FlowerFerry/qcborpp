# qcborpp

**纯头文件 C++17 封装，基于 [QCBOR](https://github.com/laurencelundblade/QCBOR) —— RFC 8949 CBOR 编解码器。**

qcborpp 在 QCBOR 久经考验的 C 实现之上，提供现代 C++ 惯用法 —— RAII、`operator[]`、方法链式调用、异常。

## 特性

- **纯头文件** —— `#include <qcborpp/qcborpp.hpp>` 即可
- **双编码器** —— 静态 `encoder`（零拷贝，快）和 `dynamic_encoder`（自动扩缩，灵活）
- **模板 Builder** —— `basic_map_builder<Enc>`、`basic_array_builder<Enc>`、`basic_key_proxy<Enc>` 对两种编码器通用
- **初始化列表编码** —— `encoder(buf, {{"key", 42}, ...})` 声明式一次性构建
- **RAII** —— map/array 自动关闭；`finish()` 后 builder 失效
- **`operator[]`** —— 编码 `m["key"] = value`；解码 `auto v = m["key"]`
- **方法链式调用** —— `m["config"]["host"] = "localhost"`
- **`operator<<`** —— `arr << 1 << 2 << "text"`
- **异常** —— 编解码错误抛出 `qcborpp::error`
- **`std::error_code`** —— `decoder::finish()` 返回 error_code
- **预取缓存** —— `m.prefetch()` 一次遍历，之后 `m["key"]` O(1) 命中
- **批量解码** —— `m.get_items()` 和 `dec.get_items_in_map()` 利用 QCBOR 原生单趟查找
- **全部 QCBOR 类型** —— int, uint, text, bytes, double, float, bool, null, undef, tag, bignum, decimal fraction, bigfloat, URI, base64, regex, MIME, UUID, epoch 日期
- **底层访问** —— 必要时可直接调用 QCBOR C API

## 依赖

需自行提供 QCBOR。qcborpp 不会自动获取、构建或内置 QCBOR。

要求：
- C++17 编译器（GCC 8+、Clang 7+、MSVC 2017+）
- [QCBOR](https://github.com/laurencelundblade/QCBOR)（v1.1+）已安装或已集成到项目中

### 提供 QCBOR 的三种方式

**方式一：CMake find_package**
```cmake
find_package(QCBOR REQUIRED)
target_link_libraries(my_app PRIVATE qcborpp QCBOR::qcbor)
```

**方式二：CMake FetchContent**
```cmake
include(FetchContent)
FetchContent_Declare(qcbor
    GIT_REPOSITORY https://github.com/laurencelundblade/QCBOR.git
    GIT_TAG v1.2
)
FetchContent_MakeAvailable(qcbor)
target_link_libraries(my_app PRIVATE qcborpp qcbor)
```

**方式三：手动路径**
```cmake
set(QCBOR_INCLUDE_DIR /path/to/qcbor/inc)
set(QCBOR_LIBRARY /path/to/libqcbor.a)
add_subdirectory(path/to/qcborpp)
target_link_libraries(my_app PRIVATE qcborpp)
```

## 快速开始

### 编码

```cpp
#include <qcborpp/qcborpp.hpp>
using namespace qcborpp;

// 静态编码器：预分配 buffer，零拷贝
uint8_t buf[512];
encoder enc(byte_span{buf, sizeof(buf)});

{
    auto m = enc.map();
    m["name"]    = "example";
    m["count"]   = 42;
    m["active"]  = true;
    m["version"] = 1.5;
    m["comment"] = nullptr;       // CBOR null

    // 嵌套数组
    {
        auto arr = m["tags"].array();
        arr << "cbor" << "c++" << "header-only";
    }

    // 嵌套 map（链式）
    m["meta"]["author"] = "qcborpp team";
}

auto data = enc.finish();  // const_byte_span
```

### 编码（动态）
```cpp
// dynamic_encoder：自动扩缩 buffer，两阶段延迟编码
dynamic_encoder enc;

{
    auto m = enc.map();
    m["key"] = "value";
    m["num"] = 100;
}

auto data = enc.finish();
```

### 解码

```cpp
decoder dec(data);

if (dec.is_map()) {
    auto m = dec.map();

    auto name    = std::string_view{m["name"]};
    auto count   = int64_t{m["count"]};
    auto active  = bool{m["active"]};
    auto version = double{m["version"]};

    // 嵌套数组
    auto tags = m["tags"].as_array();
    while (!tags.done()) {
        std::string_view tag = tags.next();
        // ...
    }

    // 链式嵌套 map
    auto author = std::string_view{m["meta"]["author"]};
}

auto ec = dec.finish();
if (ec) { /* 处理错误 */ }
```

## 性能指南

解码含大量 key 的 map 时有三种策略：

| 策略 | 50 次 key 查询 | 适用场景 |
|------|---------------|----------|
| `operator[]`（默认） | 慢（~240 us） | 仅 1-2 次查询 |
| `m.get_items(specs, out)` | 快（~31 us） | 已知 key 列表，一次性提取 |
| `m.prefetch()` + `operator[]` | 最快（~16 us） | 多次查询，key 集合未知 |

```cpp
auto m = dec.map();

// 方案 A：预取 —— 之后 operator[] 全部 O(1) 命中
m.prefetch();
auto v1 = int64_t{m["key1"]};
auto v2 = int64_t{m["key2"]};
// …… 任意多次

// 方案 B：批量 —— 一次性传入所有 key
std::vector<decoder::item_spec> specs;
specs.push_back({"key1", cbor_type::int64});
specs.push_back({"key2", cbor_type::text_string});
std::vector<decoded_item> out;
m.get_items(specs, out);
```

编码器选择：

| 编码器 | Buffer | 速度 | 适用场景 |
|--------|--------|------|----------|
| `encoder(buf)` | 调用者提供 | 最快（约 1.2 倍 QCBOR C） | 已知大小，性能敏感 |
| `dynamic_encoder` | 自动堆分配 | 慢 3-5 倍 | 未知大小，一次性编码 |

---

## Cookbook

### 日期与时间

```cpp
// ── 编码 ──
using namespace std::chrono;

// epoch 秒 (CBOR tag 1)
m["created"] = int64_t(1710000000);          // 原始 epoch，通过 operator=
m["updated"] = system_clock::now();            // std::chrono 自动转换
enc.add_date_epoch(1710000000);                // 底层 API

// epoch 天 (CBOR tag 100)
m["expires"] = duration<unsigned, ratio<86400>>(30);  // 30 天，通过 operator=
enc.add_days_epoch(30);                                // 底层 API

// 日期字符串 (CBOR tag 0) / 天数字符串 (CBOR tag 1004)
enc.add_date_string("2024-03-09");
enc.add_days_string("2024-03-09");

// ── 解码 ──
auto created = m["created"].as_time_point();          // → time_point
auto expires = m["expires"].as_days_duration();       // → duration<int64_t,ratio<86400>>
int64_t raw_sec  = m["created"].as_date_epoch();      // → 1710000000
int64_t raw_days = m["expires"].as_days_epoch();      // → 30
```

### 语义标签 — URI、base64、UUID、regex、MIME

```cpp
// ── 编码 ──
m["homepage"]    = std::string_view{"https://example.com"};  // 纯文本...
enc.add_uri("https://example.com");                          // ...或带标签 URI
enc.add_b64_text("SGVsbG8=");
enc.add_binary_uuid(byte_span{uuid_bytes.data(), 16});
enc.add_regex("^[a-z]+@[a-z]+\\\\.com$");
enc.add_mime_data("{\"key\": 1}");

// ── 解码 ──
std::string_view uri = m["homepage"].as_uri();    // 需要 tag 32
std::string_view b64 = m["payload"].as_b64();     // 需要 tag 34
const_byte_span uuid  = m["id"].as_uuid();        // 需要 tag 37
```

### 大数、十进制浮点数、大浮点数

```cpp
uint8_t big[] = {0x01, 0x00, 0x00, 0x00};   // 大端序 16777216

enc.add_bignum_positive(byte_span{big, 4});  // CBOR tag 2
enc.add_bignum_negative(byte_span{big, 4});  // CBOR tag 3
enc.add_decimal_fraction(314159, -5);        // 3.14159 (tag 4)
enc.add_bigfloat(13107, -13);                // 1.6 (tag 5)

// 解码
auto bn = m["big"].as_bignum();  // decoded_item 含 bytes + 符号
auto df = m["val"].as_decimal_fraction();  // exp_and_mantissa
```

### 整数 key 的 Map

```cpp
// 编码
auto m = enc.map();
m[1] = "first";
m[2] = true;
m[3] = 3.14;

// 解码
auto v1 = int64_t{m[1]};
auto v2 = m[2].as_bool();
```

### 复杂嵌套 — map → array → map

```cpp
// 编码
auto m = enc.map();
{
    auto arr = m["items"].array();
    {
        auto item = arr.add_map();
        item["id"] = 1;
        item["name"] = "alpha";
    }
    {
        auto item = arr.add_map();
        item["id"] = 2;
        item["name"] = "beta";
    }
}

// 解码
auto items = m["items"].as_array();
while (!items.done()) {
    auto item = items.next().as_map();
    auto id   = int64_t{item["id"]};
    auto name = std::string_view{item["name"]};
}
```

### 初始化列表编码（C++17）

用单条表达式编码完整 CBOR 结构——无需 builder，无需手动调用嵌套容器的 `finish()`。
静态 buffer 用 `encoder(buf, {...})`，自动扩缩用 `dynamic_encoder({...})`。

**平铺 map，混合类型：**

```cpp
uint8_t buf[512];
encoder enc(byte_span{buf, sizeof(buf)}, {
    {"pi",      3.141},
    {"happy",   true},
    {"name",    "Niels"},
    {"nothing", nullptr},
});
auto data = enc.finish();
// → {"pi": 3.141, "happy": true, "name": "Niels", "nothing": null}
```

**嵌套结构：**

```cpp
encoder enc(buf, {
    {"answer", {{"everything", 42}}},             // 嵌套 map
    {"list",   {1, 0, 2}},                         // array（自动推断）
    {"object", {{"currency", "USD"}, {"value", 42.99}}},
});
```

**推断规则：**

- `{1, 0, 2}` — 全部整数 → 推断为 CBOR array
- `{{"key", val}, {"key2", val2}}` — 全部是字符串 key 的 pair → 推断为 CBOR map
- `{{1, "a"}, {2, "b"}}` — 整数 key 的 pair → 推断为 **array of arrays**（非 map）
- `{"a", "b", "c"}` — 字符串列表 → 推断为 CBOR array

**用工厂函数覆盖推断：**

```cpp
encoder enc(buf, {
    {"tags",     arr({"a", "b", "c"})},            // 强制为 array
    {"metadata", map({{"ver", 1}, {"lang", "en"}})}, // 强制为 map
    {"dict",     imap({{1, "first"}, {2, "second"}})},// 强制为整数 key map
});
```

**对象数组：**

```cpp
encoder enc(buf, {
    {"rows", arr({
        map({{"id", 1}, {"name", "alpha"}}),
        map({{"id", 2}, {"name", "beta"}}),
        map({{"id", 3}, {"name", "gamma"}}),
    })},
});
```

**动态编码器（自动扩缩）：**

```cpp
dynamic_encoder enc({
    {"key", "value"},
    {"nested", {{"inner", 99}}},
    {"items", arr({1, 2, 3})},
});
auto data = enc.finish();
```

**Chrono 支持：**

```cpp
using namespace std::chrono;
encoder enc(buf, {
    {"created", system_clock::now()},
    {"expires", duration<int, ratio<86400>>(30)},
});
```

**解码 init-list 输出：**

生成的 CBOR 是标准的——正常解码即可：
```cpp
decoder dec(data);
auto m = dec.map();
auto v = int64_t{m["answer"]["everything"]};  // 42
```

> **作用域安全：** 解码 map 的数组时，将每个 `.as_map()` 用 `{}` 块包裹，
> 确保内层 map scope 在下一次 `next()` 调用前析构：
> ```cpp
> auto arr = m["rows"].as_array();
> {
>     auto r1 = arr.next().as_map();
>     // ... 使用 r1 ...
> }
> {
>     auto r2 = arr.next().as_map();
>     // ... 使用 r2 ...
> }
> ```

### 解码性能 — 选择合适的策略

```cpp
auto m = dec.map();

// 策略 A：预取（大量查询时最快）
m.prefetch();
for (auto& key : keys) {                   // 50 次查询 → ~16 us
    auto v = int64_t{m[key]};
}

// 策略 B：批量（已知 key 列表时最佳）
std::vector<decoder::item_spec> specs = {
    {"key1", cbor_type::int64},
    {"key2", cbor_type::text_string},
};
std::vector<decoded_item> out;
m.get_items(specs, out);                    // 单趟遍历所有 key

// 策略 C：延迟（仅 1-2 次查询时适用）
auto v = int64_t{m["key1"]};               // 首次访问时扫描
```

### 安全解码：get_or、contains、size、for_each

受 `nlohmann/json` 启发的便捷方法，消除 try/catch 样板代码，让解码更安全、更可读。

**get_or — 带默认值的安全访问：**

```cpp
auto m = dec.map();
int64_t port = m["port"].get_or(8080);           // 不存在的 key → 默认值
auto name    = m["name"].get_or(std::string_view{"匿名"});
bool debug   = m["debug"].get_or(false);
double ratio = m["ratio"].get_or(0.5);
// 也支持 uint64_t，并提供 int 重载以消除字面量歧义：
int64_t count = m["count"].get_or(0);            // 0 推断为 int → int64_t
```

**value_or — 更简洁：直接在 map 上指定 key + 默认值：**

```cpp
auto m = dec.map();
int64_t port  = m.value_or("port", 8080);        // 一次调用，无需 operator[]
auto name     = m.value_or("name", std::string_view{"匿名"});
bool debug    = m.value_or("debug", false);
// 也支持 int64_t 键：
int64_t val   = m.value_or(42, -1);
```

**contains — 检查 key 是否存在而不消耗解码器游标：**

```cpp
auto m = dec.map();
if (m.contains("optional_field")) {
    auto val = m["optional_field"];               // 检查后安全访问
}
// contains() 自动预取；后续 operator[] 从缓存 O(1) 读取
```

**size — map 条目数：**

```cpp
auto m = dec.map();
size_t n = m.size();                               // CBOR map header 中的 key-value 对数
// array_scope::size() 同样适用于数组
```

**for_each — 一次遍历所有条目：**

```cpp
auto m = dec.map();
m.for_each([](std::string_view key, decoded_item val) {
    if (key == "name") {
        std::string_view name = val.value.text;
        // ...
    }
});
// for_each 自动预取，返回 *this 支持链式调用
```

**组合使用：**

```cpp
auto m = dec.map();
if (m.contains("items") && m.size() > 0) {
    auto items = m["items"].as_array();
    // ...
}
int64_t count = m["count"].get_or(0);
```

### 错误处理

```cpp
// 编码器 — 异常
try {
    auto data = enc.finish();
} catch (const qcborpp::error& e) {
    // e.code() → errc, e.what() → 描述信息
    if (e.code() == errc::buffer_too_small) { /* ... */ }
}

// 解码器 — error_code
auto ec = dec.finish();
if (ec) {
    std::cerr << ec.message() << "\\n";
}

// 链式保护（内置）
auto kp = m["outer"];
kp["inner"] = 100;
m["other"]  = 42;  // 抛出 error(errc::close_mismatch) — depth guard
```

### 底层 API

```cpp
// 直接调用 QCBOR — 不用 builder，不用 RAII
encoder enc(buf);
enc.open_map();
    enc.add_text("key");
    enc.add_int64(42);
enc.close_map();
auto data = enc.finish();

// 两种编码器均可
dynamic_encoder dyn;
dyn.open_array();
    dyn.add_int64(1); dyn.add_int64(2); dyn.add_int64(3);
dyn.close_array();
auto result = dyn.finish();
```

---

## API 参考

### 编码器类型

#### `encoder`（静态 buffer）
```cpp
#include <qcborpp/qcborpp.hpp>

uint8_t buf[1024];
encoder enc(byte_span{buf, sizeof(buf)});

// ── init-list 构造函数 ──
encoder(byte_span buf, std::initializer_list<cbor_ref> init);
```
| 参数 | 说明 |
|------|------|
| `buf` | 预分配输出缓冲区 |
| `init` | 嵌套初始化列表形式的 CBOR 数据（见 [Cookbook](#初始化列表编码c17)） |

```cpp
// ── 顶层 ──
basic_map_builder<encoder>   enc.map();       // 开始编码 CBOR map
basic_array_builder<encoder> enc.array();     // 开始编码 CBOR array
bool enc.is_map()   const noexcept;
bool enc.is_array() const noexcept;
const_byte_span enc.finish();                 // 完成编码，返回 buffer 切片
const_byte_span enc.data()   const noexcept;  // 查看当前输出（finish() 之前）

// ── 底层直接添加 API ──
encoder& enc.add_int64(int64_t v);
encoder& enc.add_uint64(uint64_t v);
encoder& enc.add_text(std::string_view v);
encoder& enc.add_text(const char* v);
encoder& enc.add_bytes(const_byte_span v);
encoder& enc.add_double(double v);
encoder& enc.add_float(float v);
encoder& enc.add_double_no_preferred(double v);
encoder& enc.add_float_no_preferred(float v);
encoder& enc.add_bool(bool v);
encoder& enc.add_null();
encoder& enc.add_undef();
encoder& enc.add_simple(uint64_t v);
encoder& enc.add_tag(uint64_t tag);
encoder& enc.add_encoded(const_byte_span encoded_cbor);
encoder& enc.open_map();
encoder& enc.close_map();
encoder& enc.open_array();
encoder& enc.close_array();

// ── 带标签的语义类型 ──
encoder& enc.add_date_epoch(int64_t sec, bool as_tag = true);
encoder& enc.add_date(std::chrono::system_clock::time_point tp, bool as_tag = true);
encoder& enc.add_days_epoch(int64_t days, bool as_tag = true);
template<typename Rep, typename Period>
encoder& enc.add_days(std::chrono::duration<Rep, Period> dur, bool as_tag = true);
encoder& enc.add_date_string(std::string_view date, bool as_tag = true);
encoder& enc.add_days_string(std::string_view date, bool as_tag = true);
encoder& enc.add_bignum_positive(const_byte_span bytes, bool as_tag = true);
encoder& enc.add_bignum_negative(const_byte_span bytes, bool as_tag = true);
encoder& enc.add_decimal_fraction(int64_t mantissa, int64_t exp10, bool as_tag = true);
encoder& enc.add_decimal_fraction_bignum(const_byte_span mantissa, bool is_neg, int64_t exp10, bool as_tag = true);
encoder& enc.add_bigfloat(int64_t mantissa, int64_t exp2, bool as_tag = true);
encoder& enc.add_bigfloat_bignum(const_byte_span mantissa, bool is_neg, int64_t exp2, bool as_tag = true);
encoder& enc.add_uri(std::string_view uri, bool as_tag = true);
encoder& enc.add_b64_text(std::string_view b64, bool as_tag = true);
encoder& enc.add_b64url_text(std::string_view b64url, bool as_tag = true);
encoder& enc.add_regex(std::string_view regex, bool as_tag = true);
encoder& enc.add_mime_data(std::string_view data, bool as_tag = true);
encoder& enc.add_binary_uuid(const_byte_span uuid, bool as_tag = true);
```

#### `dynamic_encoder`（自动扩缩）
```cpp
dynamic_encoder enc;
dynamic_encoder enc(std::initializer_list<cbor_ref> init);  // init-list 构造函数

// ── 顶层（与 encoder 相同） ──
basic_map_builder<dynamic_encoder>   enc.map();
basic_array_builder<dynamic_encoder> enc.array();
bool enc.is_map()   const noexcept;
bool enc.is_array() const noexcept;
const_byte_span enc.finish();
const_byte_span enc.data()   const noexcept;

// ── 底层 API（与 encoder 完全一致的接口） ──
// add_*、open_map/array、close_map/array、带标签类型
// —— 均与上述 encoder 相同。
```

### 编码 Builder（模板，两种编码器通用）

#### `basic_map_builder<Enc>`
```cpp
// 类型别名（保持向后兼容）：
using map_builder = basic_map_builder<encoder>;
using dynamic_map_builder = basic_map_builder<dynamic_encoder>;
```
| 方法 | 说明 |
|------|------|
| `operator[](std::string_view)` | 设置 key，返回 `basic_key_proxy<Enc>` |
| `operator[](int64_t)` | 设置整数 key |
| `operator[](const char*)` | 设置 C 字符串 key |

#### `basic_key_proxy<Enc>`
| 方法 | 说明 |
|------|------|
| `operator=(int64_t)` | 赋整数值 |
| `operator=(uint64_t)` | 赋无符号整数 |
| `operator=(std::string_view)` | 赋文本值 |
| `operator=(const char*)` | 赋 C 字符串值 |
| `operator=(double)` | 赋双精度浮点值 |
| `operator=(bool)` | 赋布尔值 |
| `operator=(std::nullptr_t)` | 赋 CBOR null |
| `operator=(const_byte_span)` | 赋字节串 |
| `operator=(std::chrono::system_clock::time_point)` | 赋为 epoch 日期 (tag 1) |
| `operator=(std::chrono::duration<Rep,Period>)` | 赋为 epoch 天数 (tag 100) |
| `map()` | 打开嵌套 map；返回 `basic_map_builder<Enc>` |
| `array()` | 打开嵌套 array；返回 `basic_array_builder<Enc>` |
| `operator[](std::string_view)` | 隐式嵌套 map key 访问（链式） |
| `operator[](int64_t)` | 嵌套 map 整数 key 访问 |
| `operator[](const char*)` | 嵌套 map C 字符串 key 访问 |

> **链式规则** — 当 `operator[]` 为了链式访问而打开隐式嵌套 map 时（如 `m["a"]["b"] = 42`），encoder 内部 `proxy_map_depth_` 计数器自增。最内层 `key_proxy` 析构时计数器自减（RAII 延迟关闭）。
>
> 若 `key_proxy` 持有未关闭的隐式 map（`depth > 0`）时，又调用 `basic_map_builder::operator[]`，builder 会**立即抛出 `error(errc::close_mismatch)`**——防止静默产生非法 CBOR 数据。正常的单次链式调用（`m["a"]["b"] = 42`）不受影响，因为临时 `key_proxy` 对象在分号处即析构。只有跨多次 builder 操作持有 `key_proxy` 才会触发此保护。
>
> **示例 — 错误（会抛异常）：**
> ```cpp
> auto kp = m["outer"];      // depth = 0 (kp.owns_map_ = false)
> kp["inner"] = 100;          // depth = 1 — kp 打开了隐式 map
> m["other"] = 42;            // 抛异常！ — builder 发现 depth > 0
> ```
>
> **示例 — 正确：**
> ```cpp
> m["outer"]["inner"] = 100;  // 临时 kp，在 ; 处析构
> m["other"] = 42;            // depth = 0，正常
> ```

#### `basic_array_builder<Enc>`
```cpp
// 类型别名：
using array_builder = basic_array_builder<encoder>;
using dynamic_array_builder = basic_array_builder<dynamic_encoder>;
```
| 方法 | 说明 |
|------|------|
| `add(T v)` | 添加 T 类型元素 |
| `operator<<(T v)` | 流式添加 |
| `add_map()` | 添加嵌套 map 元素，返回 `basic_map_builder<Enc>` |
| `add_array()` | 添加嵌套 array 元素，返回 `basic_array_builder<Enc>` |

---

### 解码

#### `decoder`
```cpp
decoder dec(const_byte_span data);
```
| 方法 | 说明 |
|------|------|
| `is_map()` / `is_array()` | 检查顶层类型 |
| `map()` | 进入顶层 map；返回 `map_scope` |
| `array()` | 进入顶层 array；返回 `array_scope` |
| `finish()` | 完成解码；返回 `std::error_code` |
| `set_mem_pool(byte_span, bool)` | 为不定长字符串设置内存池 |
| `rewind()` | 重置解码光标 |
| `v_get_next()` | 获取下一项（V 变体，出错抛异常），返回 `decoded_item` |
| `v_peek_next()` | 查看下一项但不消耗，返回 `decoded_item` |
| `get_next()` | 获取下一项（返回值指示错误），返回 `decoded_item` |
| `item_spec` | 结构体：`{int64_t label_int; const char* label_str; cbor_type type; bool is_int_label;}` |
| `get_items_in_map(specs, out)` | 从顶层 map 批量解码；返回 `std::error_code` |

#### `map_scope` —— 由 `dec.map()` 返回
| 方法 | 说明 |
|------|------|
| `prefetch()` | **单趟缓存**：解码所有项到内部哈希表。之后 `operator[]` O(1) 命中。适合大量 key 查询场景。 |
| `operator[](std::string_view)` | 按字符串标签查找。若已 prefetch，O(1)；否则创建延迟 `item_proxy`，首次访问时才扫描。 |
| `operator[](int64_t)` | 按整数标签查找 |
| `operator[](const char*)` | 按 C 字符串标签查找 |
| `get_items(specs, out)` | 批量解码，使用 QCBOR 原生 `GetItemsInMap`。单趟遍历所有 key。 |

#### `item_proxy` —— 由 `m["key"]` 返回
| 方法 | 说明 |
|------|------|
| **隐式转换** | `int64_t`、`uint64_t`、`std::string_view`、`double`、`bool`、`const_byte_span` |
| `as_int64()` / `as_uint64()` | 显式整数获取 |
| `as_string()` | 显式 UTF-8 文本获取 |
| `as_bytes()` | 显式字节串获取 |
| `as_double()` / `as_bool()` | 显式标量获取 |
| `as_map()` / `as_array()` | 进入嵌套 map/array |
| `as_date_string()` / `as_days_string()` | 带标签的字符串获取 |
| `as_date_epoch()` / `as_days_epoch()` | 带标签的 epoch 获取 (int64_t) |
| `as_time_point()` | 提取为 `std::chrono::system_clock::time_point` (tag 1) |
| `as_days_duration()` | 提取为 `std::chrono::duration<int64_t, std::ratio<86400>>` (tag 100) |
| `as_uri()` / `as_b64()` / `as_b64url()` | 带标签的标签获取 |
| `as_regex()` / `as_mime()` / `as_uuid()` | 带标签的标签获取 |
| `as_bignum()` | 大数获取 |
| `as_decimal_fraction()` / `as_bigfloat()` | 算术类型获取 |
| `type()` | 查询 CBOR 类型，不消耗 |
| `is_int64()` / `is_uint64()` / `is_double()` / `is_float()` | 类型查询 |
| `is_string()` / `is_bytes()` / `is_bool()` | 类型查询 |
| `is_bool_true()` / `is_bool_false()` / `is_null()` / `is_undef()` | 字面量查询 |
| `is_map()` / `is_array()` / `is_tag()` | 容器查询 |
| `operator[](std::string_view)` | 链式进入嵌套 map（按 key） |
| `operator[](int64_t)` | 链式进入嵌套 map（按整数 key） |

#### `array_scope` —— 由 `dec.array()` 返回
| 方法 | 说明 |
|------|------|
| `done()` | 检查是否已消费完所有元素 |
| `next()` | 读取下一元素；返回 `item_proxy` |
| `size()` | 元素总数（如有） |

---

### 类型系统

#### `cbor_type` 枚举
```cpp
enum class cbor_type : uint8_t {
    none, any,
    int64, uint64, array, map, byte_string, text_string,
    pos_bignum, neg_bignum,
    date_string, date_epoch,
    unknown_simple, decimal_fraction, decimal_fraction_pos_bignum,
    decimal_fraction_neg_bignum, bigfloat, bigfloat_pos_bignum,
    bigfloat_neg_bignum,
    false_v, true_v, null_v, undef_v,
    float_v, double_v, map_as_array, wrapped_cbor,
    uri, base64url, base64, regex, mime, uuid,
    wrapped_cbor_sequence, binary_mime, days_string, days_epoch,
};
```

#### `decoded_item` 结构体
```cpp
struct decoded_item {
    cbor_type type;
    cbor_type label_type;
    union { int64_t int64_val; uint64_t uint64_val; double double_val;
            float float_val; bool bool_val; const_byte_span bytes;
            std::string_view text; exp_and_mantissa exp_mantissa;
            epoch_date epoch_date_val; int64_t epoch_days; uint8_t simple_val; } value;
    union { std::string_view text_label; int64_t int64_label; uint64_t uint64_label; } label;
    uint8_t nesting_level, next_nesting_level;
    bool data_allocated, label_allocated;
};
```

#### Span 类型
```cpp
struct byte_span       { uint8_t* ptr;       size_t len; };  // 可变
struct const_byte_span { const uint8_t* ptr; size_t len; };  // 不可变
```

---

## 错误处理

编码错误抛出 `qcborpp::error`（继承自 `std::runtime_error`）：
```cpp
try {
    auto data = enc.finish();
} catch (const qcborpp::error& e) {
    // e.code() 返回 errc
    // e.what() 返回描述信息
}
```

解码 `finish()` 返回 `std::error_code`：
```cpp
auto ec = dec.finish();
if (ec) {
    // ec.message() 返回描述信息
}
```

---

## 构建测试

测试使用 Catch2，通过 CMake FetchContent 自动获取 QCBOR：

```bash
cmake -S . -B build -DQCBORPP_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

首次构建需要网络访问 GitHub。

---

## 许可证

BSD 3-Clause。详见 [LICENSE](LICENSE)。
