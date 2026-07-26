# qcborpp

**Header-only C++17 wrapper for [QCBOR](https://github.com/laurencelundblade/QCBOR) — a CBOR (RFC 8949) encoder/decoder.**

qcborpp provides modern C++ idioms — RAII, `operator[]`, method chaining, exceptions — on top of QCBOR's battle-tested C implementation.

## Features

- **Header-only** — `#include <qcborpp/qcborpp.hpp>`
- **Two encoders** — static `encoder` (zero-copy, fast) and `dynamic_encoder` (auto-sizing, flexible)
- **Template builders** — `basic_map_builder<Enc>`, `basic_array_builder<Enc>`, `basic_key_proxy<Enc>` work with both encoders
- **Initializer-list encoding** — `encoder(buf, {{"key", 42}, ...})` for declarative one-shot construction
- **RAII** — maps/arrays auto-close; `finish()` invalidates stale builders
- **`operator[]`** — `m["key"] = value` for encoding; `auto v = m["key"]` for decoding
- **Method chaining** — `m["config"]["host"] = "localhost"`
- **`operator<<`** — `arr << 1 << 2 << "text"`
- **Exceptions** — encoding/decoding errors throw `qcborpp::error`
- **`std::error_code`** — `decoder::finish()` returns error_code
- **Prefetch cache** — `m.prefetch()` traverses the map once, then `m["key"]` is O(1)
- **Batch decode** — `m.get_items()` and `dec.get_items_in_map()` use QCBOR's native single-pass lookup
- **All QCBOR types** — int, uint, text, bytes, double, float, bool, null, undef, tags, bignum, decimal fraction, bigfloat, URI, base64, regex, MIME, UUID, epoch dates
- **Low-level access** — direct QCBOR C API available when needed

## Dependencies

You must provide QCBOR yourself. qcborpp does not fetch, build, or vendor QCBOR.

Requirements:
- C++17 compiler (GCC 8+, Clang 7+, MSVC 2017+)
- [QCBOR](https://github.com/laurencelundblade/QCBOR) (v1.1+) installed or built in your project

### Providing QCBOR

**Option 1: CMake find_package**
```cmake
find_package(QCBOR REQUIRED)
target_link_libraries(my_app PRIVATE qcborpp QCBOR::qcbor)
```

**Option 2: CMake FetchContent**
```cmake
include(FetchContent)
FetchContent_Declare(qcbor
    GIT_REPOSITORY https://github.com/laurencelundblade/QCBOR.git
    GIT_TAG v1.2
)
FetchContent_MakeAvailable(qcbor)
target_link_libraries(my_app PRIVATE qcborpp qcbor)
```

**Option 3: Manual paths**
```cmake
set(QCBOR_INCLUDE_DIR /path/to/qcbor/inc)
set(QCBOR_LIBRARY /path/to/libqcbor.a)
add_subdirectory(path/to/qcborpp)
target_link_libraries(my_app PRIVATE qcborpp)
```

## Quick Start

### Encoding

```cpp
#include <qcborpp/qcborpp.hpp>
using namespace qcborpp;

// static encoder: pre-allocated buffer, zero-copy
uint8_t buf[512];
encoder enc(byte_span{buf, sizeof(buf)});

{
    auto m = enc.map();
    m["name"]    = "example";
    m["count"]   = 42;
    m["active"]  = true;
    m["version"] = 1.5;
    m["comment"] = nullptr;       // CBOR null

    // Nested array
    {
        auto arr = m["tags"].array();
        arr << "cbor" << "c++" << "header-only";
    }

    // Nested map (chained)
    m["meta"]["author"] = "qcborpp team";
}

auto data = enc.finish();  // const_byte_span
```

### Encoding (dynamic)
```cpp
// dynamic_encoder: auto-sizing buffer, two-pass deferred encoding
dynamic_encoder enc;

{
    auto m = enc.map();
    m["key"] = "value";
    m["num"] = 100;
}

auto data = enc.finish();
```

### Decoding

```cpp
decoder dec(data);

if (dec.is_map()) {
    auto m = dec.map();

    auto name    = std::string_view{m["name"]};
    auto count   = int64_t{m["count"]};
    auto active  = bool{m["active"]};
    auto version = double{m["version"]};

    // Nested array
    auto tags = m["tags"].as_array();
    while (!tags.done()) {
        std::string_view tag = tags.next();
        // ...
    }

    // Chained nested map
    auto author = std::string_view{m["meta"]["author"]};
}

auto ec = dec.finish();
if (ec) { /* handle error */ }
```

## Performance Guide

`force_prefetch` (default `true`) controls whether `dec.map()` and `as_map()` automatically traverse the map on entry. When `true`, all entries are cached immediately — subsequent `operator[]` calls are O(1) cache hits. Set to `false` for maps where you only need 1-2 lookups or want explicit control via `prefetch()`.

```cpp
decoder dec(data);
dec.set_force_prefetch(false);   // opt out of auto-prefetch
auto m = dec.map();              // does NOT prefetch — lazy mode
auto v = m["key1"].get_or(0);   // Spiffy lookup, no cache build
```

Decoding a map with many key lookups has three strategies:

| Strategy | 50-key lookup | When to use |
|----------|---------------|-------------|
| `operator[]` (default) | slow (~240 us) | 1-2 lookups only |
| `m.get_items(specs, out)` | fast (~31 us) | Known key list, one-shot extract |
| `m.prefetch()` + `operator[]` | fastest (~16 us) | Many lookups, unknown key set |

```cpp
auto m = dec.map();

// Option A: prefetch — future operator[] hits are O(1)
m.prefetch();
auto v1 = int64_t{m["key1"]};
auto v2 = int64_t{m["key2"]};
// ... as many as needed

// Option B: batch — pass all keys at once
std::vector<decoder::item_spec> specs;
specs.push_back({"key1", cbor_type::int64});
specs.push_back({"key2", cbor_type::text_string});
std::vector<decoded_item> out;
m.get_items(specs, out);
```

Encoder choice:

| Encoder | Buffer | Speed | When to use |
|---------|--------|-------|-------------|
| `encoder(buf)` | caller-provided | fastest (1.2x QCBOR C) | Known size, performance-critical |
| `dynamic_encoder` | auto heap buffer | 2-5x slower | Unknown size, one-off encoding |

---

## Cookbook

### Dates and Times

```cpp
// ── encode ──
using namespace std::chrono;

// epoch seconds (CBOR tag 1)
m["created"] = int64_t(1710000000);          // raw epoch via operator=
m["updated"] = system_clock::now();            // std::chrono auto-converts
enc.add_date_epoch(1710000000);                // low-level

// epoch days (CBOR tag 100)
m["expires"] = duration<unsigned, ratio<86400>>(30);  // 30 days via operator=
enc.add_days_epoch(30);                                // low-level int

// date string (CBOR tag 0) / days string (CBOR tag 1004)
enc.add_date_string("2024-03-09");
enc.add_days_string("2024-03-09");

// ── decode ──
auto created = m["created"].as_time_point();          // → time_point
auto expires = m["expires"].as_days_duration();       // → duration<int64_t,ratio<86400>>
int64_t raw_sec  = m["created"].as_date_epoch();      // → 1710000000
int64_t raw_days = m["expires"].as_days_epoch();      // → 30
```

### Semantic Tags — URI, base64, UUID, regex, MIME

```cpp
// ── encode ──
m["homepage"]    = std::string_view{"https://example.com"};  // plain text...
enc.add_uri("https://example.com");                          // ...or tagged URI
enc.add_b64_text("SGVsbG8=");
enc.add_binary_uuid(byte_span{uuid_bytes.data(), 16});
enc.add_regex("^[a-z]+@[a-z]+\\\\.com$");
enc.add_mime_data("{\"key\": 1}");

// ── decode ──
std::string_view uri = m["homepage"].as_uri();    // requires tag 32
std::string_view b64 = m["payload"].as_b64_text();
const_byte_span uuid  = m["id"].as_uuid();        // requires tag 37
```

### Bignums, Decimal Fractions, Bigfloats

```cpp
uint8_t big[] = {0x01, 0x00, 0x00, 0x00};   // 16777216 in big-endian

enc.add_bignum_positive(byte_span{big, 4});  // CBOR tag 2
enc.add_bignum_negative(byte_span{big, 4});  // CBOR tag 3
enc.add_decimal_fraction(314159, -5);        // 3.14159 (tag 4)
enc.add_bigfloat(13107, -13);                // 1.6 (tag 5)

// decode
auto bn = m["big"].as_bignum();  // decoded_item with bytes + sign
auto df = m["val"].as_decimal_fraction();  // exp_and_mantissa
// df.is_bignum() → false: df.as_integer() → std::optional<int64_t>
// df.is_bignum() → true: df.as_big_num() → std::optional<const_byte_span>
```

### Integer-Keyed Maps

```cpp
// encode
auto m = enc.map();
m[1] = "first";
m[2] = true;
m[3] = 3.14;

// decode
auto v1 = int64_t{m[1]};
auto v2 = m[2].as_bool();
```

### Complex Nesting — map → array → map

```cpp
// encode
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

// decode
auto items = m["items"].as_array();
while (!items.done()) {
    auto item = items.next().as_map();
    auto id   = int64_t{item["id"]};
    auto name = std::string_view{item["name"]};
}
```

### Initializer-List Encoding (C++17)

Encode complete CBOR structures in a single expression — no builders, no `finish()` calls for
nested containers. Use `encoder(buf, {...})` for static buffers or `dynamic_encoder({...})` for
auto-sizing output.

**Flat map with mixed types:**

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

**Nested structures:**

```cpp
encoder enc(buf, {
    {"answer", {{"everything", 42}}},             // nested map
    {"list",   {1, 0, 2}},                         // array (auto-detected)
    {"object", {{"currency", "USD"}, {"value", 42.99}}},
});
```

**Inference rules:**

- `{1, 0, 2}` — all integers → inferred as CBOR array
- `{{"key", val}, {"key2", val2}}` — all string-keyed pairs → inferred as CBOR map
- `{{1, "a"}, {2, "b"}}` — integer-keyed pairs → inferred as **array of arrays** (not map)
- `{"a", "b", "c"}` — string list → inferred as CBOR array

**Override inference with factories:**

```cpp
encoder enc(buf, {
    {"tags",     arr({"a", "b", "c"})},            // force array
    {"metadata", map({{"ver", 1}, {"lang", "en"}})}, // force map
    {"dict",     imap({{1, "first"}, {2, "second"}})},// force int-keyed map
});
```

**Arrays of objects:**

```cpp
encoder enc(buf, {
    {"rows", arr({
        map({{"id", 1}, {"name", "alpha"}}),
        map({{"id", 2}, {"name", "beta"}}),
        map({{"id", 3}, {"name", "gamma"}}),
    })},
});
```

**Dynamic encoder (auto-sizing):**

```cpp
dynamic_encoder enc({
    {"key", "value"},
    {"nested", {{"inner", 99}}},
    {"items", arr({1, 2, 3})},
});
auto data = enc.finish();
```

**Chrono support:**

```cpp
using namespace std::chrono;
encoder enc(buf, {
    {"created", system_clock::now()},
    {"expires", duration<int, ratio<86400>>(30)},
});
```

**Decoding init-list output:**

The produced CBOR is standard — decode normally:
```cpp
decoder dec(data);
auto m = dec.map();
auto v = int64_t{m["answer"]["everything"]};  // 42
```

> **Scope safety:** When decoding arrays of maps, wrap each `.as_map()` in its own `{}` block
> so the inner map scope is destroyed before the next `next()` call:
> ```cpp
> auto arr = m["rows"].as_array();
> {
>     auto r1 = arr.next().as_map();
>     // ... use r1 ...
> }
> {
>     auto r2 = arr.next().as_map();
>     // ... use r2 ...
> }
> ```

### map_builder::merge — append pairs to an existing map

After `enc.map()`, use `merge()` to add entries without `m[key]=value` assignment.

**Single key-value merge (chaining returns *this):**

```cpp
auto m = enc.map();
m["base"] = "static_value";
m.merge("count", 42)
 .merge("name", std::string_view{"Niels"})
 .merge("active", true);        // map stays open after merge
```

**Initializer-list merge:**

```cpp
auto m = enc.map();
m.merge({{"a", 1}, {"b", "hello"}, {"c", 3.14}});
```

**Works with dynamic_encoder too:**

```cpp
dynamic_encoder enc;
auto m = enc.map();
m.merge("version", 1.5);
m.merge("flag", nullptr);
```

**Additional types (uint64_t, byte span, chrono):**

```cpp
auto m = enc.map();
m.merge("max_val", uint64_t(18446744073709551615ULL));
m.merge("blob", const_byte_span{raw_data, len});
m.merge("timestamp", std::chrono::system_clock::now());
m.merge("expiry", std::chrono::days(30));
```

### Decode Performance — pick your strategy

```cpp
auto m = dec.map();

// Strategy A: prefetch (best for many lookups)
m.prefetch();
for (auto& key : keys) {                   // 50 keys → ~16 us
    auto v = int64_t{m[key]};
}

// Strategy B: batch (best for known key list)
std::vector<decoder::item_spec> specs = {
    {"key1", cbor_type::int64},
    {"key2", cbor_type::text_string},
};
std::vector<decoded_item> out;
m.get_items(specs, out);                    // single pass for all keys

// Strategy C: lazy (best for 1-2 lookups)
auto v = int64_t{m["key1"]};               // scans on first access
```

### Safe Decode: get_or, contains, size, for_each

Convenience methods that eliminate try/catch boilerplate and make decode code safer
and more readable.

> **Auto-prefetch behavior:** When `force_prefetch` is `true` (the default), `dec.map()` and `as_map()`
> implicitly call `prefetch()` on entry, caching all map entries for O(1) subsequent lookups.
> When `false`, no auto-prefetch occurs — use explicit `prefetch()` or let convenience methods
> (`contains()`, `size()`, `for_each()`) trigger on-demand prefetch. `get_or()` and `try_get()`
> on `item_proxy` never trigger prefetch; they use QCBOR Spiffy single-key lookup.

**get_or — safe access with default fallback:**

```cpp
auto m = dec.map();
int64_t port = m["port"].get_or(8080);           // missing key → default
auto name    = m["name"].get_or(std::string_view{"anonymous"});
bool debug   = m["debug"].get_or(false);
double ratio = m["ratio"].get_or(0.5);
// Also works for uint64_t, and has an int overload for literals:
int64_t count = m["count"].get_or(0);            // 0 deduces to int → int64_t
```

**get_or — key+default on the map itself:**

```cpp
auto m = dec.map();
int64_t port  = m.get_or("port", 8080);        // one call, no operator[]
auto name     = m.get_or("name", std::string_view{"anonymous"});
bool debug    = m.get_or("debug", false);
// Also supports int64_t key:
int64_t val   = m.get_or(42, -1);
```

**try_get — std::optional access (C++17):**

```cpp
auto m = dec.map();

// Clean separation: nullopt means "absent", 0 means "explicitly zero"
auto count = m["count"].try_get<int64_t>();  // std::optional<int64_t>
if (count) { use_count(*count); }

auto opt = m["optional_key"].try_get<std::string_view>();
if (opt) { /* key exists and is a string */ }

// Works with any supported type and returns nullopt on type mismatch
auto bad = m["name"].try_get<int64_t>();     // "Niels" is string → nullopt

// Also works directly on map_scope for one less bracket:
auto count = m.try_get<int64_t>("count");     // same as m["count"].try_get<int64_t>()
auto ghost = m.try_get<std::string_view>("ghost"); // nullopt
```

**contains — key existence check without consuming the cursor:**

```cpp
auto m = dec.map();
if (m.contains("optional_field")) {
    auto val = m["optional_field"];               // safe to access after check
}
// contains() auto-prefetches; subsequent operator[] is O(1) from cache

// Integer-keyed maps also supported:
auto m2 = dec2.map();
if (m2.contains(42)) {
    auto val = std::string_view{m2[42]};           // safe access by integer key
}
```

**size — map entry count:**

```cpp
auto m = dec.map();
size_t n = m.size();                               // key-value pair count from CBOR header
// array_scope::size() also available for arrays
```

**empty — check if map has no entries:**

```cpp
auto m = dec.map();
if (m.empty()) { /* nothing to do */ }
```

**for_each — iterate all entries in one pass:**

```cpp
auto m = dec.map();
m.for_each([](std::string_view key, decoded_item val) {
    if (key == "name") {
        std::string_view name = val.value.text;
        // ...
    }
});
// for_each auto-prefetches and returns *this for chaining
```

**for_each_int — iterate integer-keyed entries:**

```cpp
auto m = dec.map();
m.for_each_int([](int64_t key, decoded_item val) {
    // key is the CBOR integer label
    int64_t v = val.value.int64;
    // ...
});
// for_each_int auto-prefetches and returns *this for chaining
```

**Combined pattern:**

```cpp
auto m = dec.map();
if (m.contains("items") && m.size() > 0) {
    auto items = m["items"].as_array();
    // ...
}
int64_t count = m["count"].get_or(0);
```

### Error Handling

```cpp
// encoder — exceptions
try {
    auto data = enc.finish();
} catch (const qcborpp::error& e) {
    // e.code() → errc, e.what() → message
    if (e.code() == errc::buffer_too_small) { /* ... */ }
}

// decoder — error_code
auto ec = dec.finish();
if (ec) {
    std::cerr << ec.message() << "\\n";
}

// chain guard (built-in)
auto kp = m["outer"];
kp["inner"] = 100;
m["other"]  = 42;  // THROWS error(errc::close_mismatch) — depth guard
```

### Low-Level API

```cpp
// Direct QCBOR calls — no builders, no RAII
encoder enc(buf);
enc.open_map();
    enc.add_text("key");
    enc.add_int64(42);
enc.close_map();
auto data = enc.finish();

// Works with both encoder types
dynamic_encoder dyn;
dyn.open_array();
    dyn.add_int64(1); dyn.add_int64(2); dyn.add_int64(3);
dyn.close_array();
auto result = dyn.finish();
```

---

## API Reference

### Encoder Types

#### `encoder` (static buffer)
```cpp
#include <qcborpp/qcborpp.hpp>

uint8_t buf[1024];
encoder enc(byte_span{buf, sizeof(buf)});

// ── init-list constructor ──
encoder(byte_span buf, std::initializer_list<cbor_ref> init);
```
| Param | Description |
|-------|-------------|
| `buf` | Pre-allocated output buffer |
| `init` | CBOR data as nested initializer lists (see [Cookbook](#initializer-list-encoding-c17)) |

```cpp
// ── top-level ──
basic_map_builder<encoder>   enc.map();       // start encoding as CBOR map
basic_array_builder<encoder> enc.array();     // start encoding as CBOR array
bool enc.is_map()   const noexcept;
bool enc.is_array() const noexcept;
const_byte_span enc.finish();                 // complete encoding, return slice of buffer

// ── low-level direct-add ──
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

// ── tagged semantic types ──
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

#### `dynamic_encoder` (auto-sizing)
```cpp
dynamic_encoder enc;
dynamic_encoder enc(size_t reserve_hint);     // pre-allocate recording buffer
dynamic_encoder enc(std::initializer_list<cbor_ref> init);  // init-list constructor

void enc.reserve(size_t n);                   // pre-allocate internal buffers
size_t enc.capacity() const noexcept;          // current reserved capacity

// ── top-level (same as encoder) ──
basic_map_builder<dynamic_encoder>   enc.map();
basic_array_builder<dynamic_encoder> enc.array();
bool enc.is_map()   const noexcept;
bool enc.is_array() const noexcept;
const_byte_span enc.finish();
const_byte_span enc.data()   const noexcept;

// ── zero-copy reference APIs (caller ensures data outlives finish()) ──
enc.add_text_ref(std::string_view);     // store (ptr, len) only — no copy
enc.add_bytes_ref(const_byte_span);     // store (ptr, len) only — no copy

// ── low-level (same API surface as encoder) ──
// All add_*, open_map/array, close_map/array, tagged types
// — identical to encoder above.
```

> **Performance note** — `dynamic_encoder` now tracks encoded size during recording and skips the sizing pass (Phase 1) when no `float` / `double` preferred operations are used. The fast path uses a conservative margin for container headers and falls back to full two-pass if the margin is exceeded.

### Encoding Builders (template, work with both encoders)

#### `basic_map_builder<Enc>`
```cpp
// Type aliases (for backward compatibility):
using map_builder = basic_map_builder<encoder>;
using dynamic_map_builder = basic_map_builder<dynamic_encoder>;
```
| Method | Description |
|--------|-------------|
| `operator[](std::string_view)` | Set key, returns `basic_key_proxy<Enc>` |
| `operator[](int64_t)` | Set integer key |
| `operator[](const char*)` | Set C-string key |

#### `basic_key_proxy<Enc>`
| Method | Description |
|--------|-------------|
| `operator=(int64_t)` | Assign integer value |
| `operator=(uint64_t)` | Assign unsigned integer |
| `operator=(std::string_view)` | Assign text value |
| `operator=(const char*)` | Assign C-string value |
| `operator=(double)` | Assign double value |
| `operator=(bool)` | Assign boolean value |
| `operator=(std::nullptr_t)` | Assign CBOR null |
| `operator=(const_byte_span)` | Assign byte string |
| `operator=(float)` | Assign float (promoted to double) |
| `operator=(std::chrono::system_clock::time_point)` | Assign as epoch date (tag 1) |
| `operator=(std::chrono::duration<Rep,Period>)` | Assign as epoch days (tag 100) |
| `map()` | Open nested map; returns `basic_map_builder<Enc>` |
| `array()` | Open nested array; returns `basic_array_builder<Enc>` |
| `operator[](std::string_view)` | Implicit nested map key access (chaining) |
| `operator[](int64_t)` | Nested map integer key access |
| `operator[](const char*)` | Nested map C-string key access |

> **Chaining rules** — When `operator[]` opens an implicit nested map for chaining (e.g. `m["a"]["b"] = 42`), it increments an internal `proxy_map_depth_` counter on the encoder. The counter is decremented when the innermost `key_proxy` destroys itself (RAII deferred-close).
>
> If a `key_proxy` with an unclosed implicit map (`depth > 0`) is still alive when `basic_map_builder::operator[]` is called, the builder **throws `error(errc::close_mismatch)` immediately** — preventing silent CBOR corruption. Normal one-shot chaining (`m["a"]["b"] = 42`) is unaffected because the temporary `key_proxy` objects are destroyed at the semicolon. Only holding a `key_proxy` across multiple builder operations triggers the guard.
>
> **Example — BAD (throws):**
> ```cpp
> auto kp = m["outer"];      // depth = 0 (kp.owns_map_ = false)
> kp["inner"] = 100;          // depth = 1 — kp opened implicit map
> m["other"] = 42;            // THROWS! — builder sees depth > 0
> ```
>
> **Example — OK:**
> ```cpp
> m["outer"]["inner"] = 100;  // temporary kp, destroyed at ;
> m["other"] = 42;            // depth = 0, no error
> ```

#### `basic_array_builder<Enc>`
```cpp
// Type aliases:
using array_builder = basic_array_builder<encoder>;
using dynamic_array_builder = basic_array_builder<dynamic_encoder>;
```
| Method | Description |
|--------|-------------|
|| `add(T v)` | Add element of type T (int64_t, uint64_t, string_view, double, float, bool, nullptr, const_byte_span) |
|| `operator<<(T v)` | Stream-style add (same types as add) |
| `add_map()` | Add nested map element, returns `basic_map_builder<Enc>` |
| `add_array()` | Add nested array element, returns `basic_array_builder<Enc>` |

---

### Decoding

#### `decoder`
```cpp
decoder dec(const_byte_span data);
```
| Method | Description |
|--------|-------------|
| `is_map()` / `is_array()` | Check top-level type |
| `map()` | Enter top-level map; returns `map_scope` |
| `array()` | Enter top-level array; returns `array_scope` |
| `finish()` | Complete decode; returns `std::error_code` |
| `set_force_prefetch(bool)` | Enable/disable auto-prefetch on `map()` and `as_map()` (default: true) |
| `force_prefetch()` | Query current force-prefetch setting |
| `set_mem_pool(byte_span, bool)` | Set memory pool for indefinite-length strings |
| `rewind()` | Reset decode cursor to start |
| `v_get_next()` | Get next item (V-variant, throws on error), returns `decoded_item` |
| `v_peek_next()` | Peek at next item without consuming, returns `decoded_item` |
| `get_next()` | Get next item (returns error via return), returns `decoded_item` |
| `item_spec` | Struct: `{int64_t label_int; const char* label_str; cbor_type type; bool is_int_label;}` |
| `get_items_in_map(specs, out)` | Batch-decode from top-level map; returns `std::error_code` |

#### `map_scope` — returned by `dec.map()`
| Method | Description |
|--------|-------------|
| `prefetch()` | **One-pass cache**: decode all items into internal hash map. Subsequent `operator[]` calls are O(1) cache hits. Called automatically when `force_prefetch` is true. |
| `operator[](std::string_view)` | Look up by string label. If prefetched, O(1); otherwise uses QCBOR Spiffy single-key scan. |
| `operator[](int64_t)` | Look up by integer label |
| `operator[](const char*)` | Look up by C-string label |
| `get_items(specs, out)` | Batch-decode using QCBOR native `GetItemsInMap`. Single traversal for all keys. |
| `contains(std::string_view)` | Auto-prefetches; check if a string-key exists |
| `contains(int64_t)` | Auto-prefetches; check if an int-key exists |
| `size()` | Entry count (auto-prefetches) |
| `empty()` | True if size == 0 (auto-prefetches) |
| `for_each(f)` | Iterate (key, item_proxy) pairs — string keys only (auto-prefetches) |
| `for_each_int(f)` | Iterate (int64_t key, decoded_item val) — int keys only (auto-prefetches) |
| `get_or(key, def)` | Lookup with fallback; returns decoded_item |
| `try_get<T>(key)` | Lookup returning `std::optional<T>` |

#### `item_proxy` — returned by `m["key"]`
| Method | Description |
|--------|-------------|
| **Implicit conversions** | `int64_t`, `uint64_t`, `std::string_view`, `double`, `bool`, `const_byte_span` — loose cross-numeric convert |
| `get_int64()` / `get_uint64()` | Loose integer getter (accepts int64/uint64/double/float) |
| `get_double()` | Loose double getter (accepts double/float/int64/uint64) |
| `as_int64()` / `as_uint64()` | Strict integer getter (exact type match only) |
| `as_double()` | Strict double getter |
| `as_string()` | Explicit UTF-8 text getter |
| `as_bytes()` | Explicit byte string getter |
| `as_bool()` | Explicit bool getter |
| `get_or(T default_val)` | Return value or default on missing key / type mismatch (uses loose convert for numeric T) |
| `try_get<T>()` | Return `std::optional<T>` (uses loose convert for numeric T) |
| `as_map()` / `as_array()` | Enter nested map/array |
| `as_date_string()` / `as_days_string()` | Tagged string getters |
| `as_date_epoch()` / `as_days_epoch()` | Tagged epoch getters (int64_t) |
| `as_time_point()` | Extract as `std::chrono::system_clock::time_point` (tag 1) |
| `as_days_duration()` | Extract as `std::chrono::duration<int64_t, std::ratio<86400>>` (tag 100) |
| `as_uri()` / `as_b64_text()` / `as_b64url()` | Tagged tag getters |
| `as_regex()` / `as_mime_data()` / `as_uuid()` | Tagged tag getters |
| `as_bignum()` | Bignum getter |
| `as_decimal_fraction()` / `as_bigfloat()` | Arithmetic type getters |
| `type()` | Query CBOR type without consuming |
| `is_int64()` / `is_uint64()` / `is_double()` / `is_float()` | Type queries |
| `is_string()` / `is_bytes()` / `is_bool()` | Type queries |
| `is_bool_true()` / `is_bool_false()` / `is_null()` / `is_undef()` | Literal queries |
| `is_map()` / `is_array()` / `is_tag()` | Container queries |
| `operator[](std::string_view)` | Chain into nested map by key |
| `operator[](int64_t)` | Chain into nested map by integer key |

#### `array_scope` — returned by `dec.array()`
| Method | Description |
|--------|-------------|
| `done()` | Check if all items consumed |
| `next()` | Read next item; returns `item_proxy` |
| `size()` | Total item count (if available) |

---

### Type System

#### `cbor_type` enum
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

#### `decoded_item` struct
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

#### Spans
```cpp
struct byte_span       { uint8_t* ptr;       size_t len; };  // mutable
struct const_byte_span { const uint8_t* ptr; size_t len; };  // immutable
```

---

## Error Handling

Encoding errors throw `qcborpp::error` (derived from `std::runtime_error`):
```cpp
try {
    auto data = enc.finish();
} catch (const qcborpp::error& e) {
    // e.code() returns errc
    // e.what() returns description
}
```

Decoding `finish()` returns `std::error_code`:
```cpp
auto ec = dec.finish();
if (ec) {
    // ec.message() returns description
}
```

---

## Building Tests

Tests use Catch2 and auto-fetch QCBOR via CMake FetchContent:

```bash
cmake -S . -B build -DQCBORPP_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

Network access to GitHub is required for the initial build.

---

## License

BSD 3-Clause. See [LICENSE](LICENSE).
