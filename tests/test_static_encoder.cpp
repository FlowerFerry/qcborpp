/*
 * tests/test_static_encoder.cpp
 *
 * Tests for the static-buffer encoder (qcborpp::encoder) — direct QCBOR
 * calls, no deferred encoding.  Also exercises the template builders
 * (basic_map_builder<encoder>, basic_array_builder<encoder>,
 * basic_key_proxy<encoder>) to prove they work identically with both
 * encoder variants.
 *
 * Run with: qcborpp_tests "[static_encoder]"
 */

#include <qcborpp/qcborpp.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <vector>
#include <cstring>

using namespace qcborpp;

// ══════════════════════════════════════════════════════════════════════════
// Construction & basic sanity
// ══════════════════════════════════════════════════════════════════════════

TEST_CASE("static_encoder: construct with buffer", "[static_encoder]") {
    uint8_t buf[256];
    encoder enc(byte_span{buf, sizeof(buf)});
    REQUIRE_NOTHROW(enc.finish());
}

TEST_CASE("static_encoder: finish returns non-empty span", "[static_encoder]") {
    uint8_t buf[256];
    encoder enc(byte_span{buf, sizeof(buf)});
    enc.open_array();
    enc.add_int64(42);
    enc.close_array();
    auto result = enc.finish();
    REQUIRE(result.size() > 0);
}

TEST_CASE("static_encoder: finish twice throws", "[static_encoder]") {
    uint8_t buf[256];
    encoder enc(byte_span{buf, sizeof(buf)});
    enc.add_null();
    enc.finish();
    REQUIRE_THROWS_AS(enc.finish(), error);
}

// ══════════════════════════════════════════════════════════════════════════
// Primitive types (direct add_* API)
// ══════════════════════════════════════════════════════════════════════════

TEST_CASE("static_encoder: add_int64 roundtrip", "[static_encoder]") {
    uint8_t buf[256];
    encoder enc(byte_span{buf, sizeof(buf)});
    enc.open_array();
    enc.add_int64(-123); enc.add_int64(0); enc.add_int64(456789);
    enc.close_array();
    auto data = enc.finish();

    decoder dec(data);
    auto a = dec.array();
    REQUIRE(int64_t(a.next()) == -123);
    REQUIRE(int64_t(a.next()) == 0);
    REQUIRE(int64_t(a.next()) == 456789);
    dec.finish();
}

TEST_CASE("static_encoder: add_uint64 roundtrip", "[static_encoder]") {
    uint8_t buf[256];
    encoder enc(byte_span{buf, sizeof(buf)});
    enc.open_array();
    enc.add_uint64(0); enc.add_uint64(10000000000ULL);
    enc.close_array();
    auto data = enc.finish();

    decoder dec(data);
    auto a = dec.array();
    REQUIRE(int64_t(a.next()) == 0);
    REQUIRE(int64_t(a.next()) == 10000000000ULL);
    dec.finish();
}

TEST_CASE("static_encoder: add_text roundtrip", "[static_encoder]") {
    uint8_t buf[256];
    encoder enc(byte_span{buf, sizeof(buf)});
    enc.open_array();
    enc.add_text("hello"); enc.add_text(std::string_view("world"));
    enc.close_array();
    auto data = enc.finish();

    decoder dec(data);
    auto a = dec.array();
    REQUIRE(std::string_view(a.next()) == "hello");
    REQUIRE(std::string_view(a.next()) == "world");
    dec.finish();
}

TEST_CASE("static_encoder: add_double roundtrip", "[static_encoder]") {
    uint8_t buf[256];
    encoder enc(byte_span{buf, sizeof(buf)});
    enc.open_array();
    enc.add_double(3.14); enc.add_double(-1.5);
    enc.close_array();
    auto data = enc.finish();

    decoder dec(data);
    auto a = dec.array();
    REQUIRE_THAT(double(a.next()), Catch::Matchers::WithinRel(3.14, 1e-9));
    REQUIRE_THAT(double(a.next()), Catch::Matchers::WithinRel(-1.5, 1e-9));
    dec.finish();
}

TEST_CASE("static_encoder: add_float roundtrip", "[static_encoder]") {
    uint8_t buf[256];
    encoder enc(byte_span{buf, sizeof(buf)});
    enc.open_array();
    enc.add_float(1.5f);
    enc.close_array();
    auto data = enc.finish();

    decoder dec(data);
    auto a = dec.array();
    REQUIRE_THAT(double(a.next()), Catch::Matchers::WithinRel(1.5, 1e-6));
    dec.finish();
}

TEST_CASE("static_encoder: add_bool roundtrip", "[static_encoder]") {
    uint8_t buf[256];
    encoder enc(byte_span{buf, sizeof(buf)});
    enc.open_array();
    enc.add_bool(true); enc.add_bool(false);
    enc.close_array();
    auto data = enc.finish();

    decoder dec(data);
    auto a = dec.array();
    REQUIRE(bool(a.next()) == true);
    REQUIRE(bool(a.next()) == false);
    dec.finish();
}

TEST_CASE("static_encoder: add_null", "[static_encoder]") {
    uint8_t buf[256];
    encoder enc(byte_span{buf, sizeof(buf)});
    enc.open_array();
    enc.add_null();
    enc.close_array();
    auto data = enc.finish();

    decoder dec(data);
    auto a = dec.array();
    REQUIRE(a.next().is_null());
    dec.finish();
}

TEST_CASE("static_encoder: add_undef", "[static_encoder]") {
    uint8_t buf[256];
    encoder enc(byte_span{buf, sizeof(buf)});
    enc.open_array();
    enc.add_undef();
    enc.close_array();
    auto data = enc.finish();

    decoder dec(data);
    auto a = dec.array();
    REQUIRE(a.next().is_undef());
    dec.finish();
}

TEST_CASE("static_encoder: add_tag roundtrip", "[static_encoder]") {
    uint8_t buf[256];
    encoder enc(byte_span{buf, sizeof(buf)});
    // Encode a tag + value inside a map so we can verify the value
    {
        auto m = enc.map();
        enc.add_tag(1);
        m["epoch"] = 1234567890;
    }
    auto data = enc.finish();

    decoder dec(data);
    auto m = dec.map();
    REQUIRE(int64_t(m["epoch"]) == 1234567890);
    dec.finish();
}

TEST_CASE("static_encoder: add_bytes roundtrip", "[static_encoder]") {
    uint8_t buf[256];
    uint8_t raw[] = {0x00, 0xFF, 0xAB};
    encoder enc(byte_span{buf, sizeof(buf)});
    enc.open_array();
    enc.add_bytes(const_byte_span{raw, sizeof(raw)});
    enc.close_array();
    auto data = enc.finish();

    decoder dec(data);
    auto a = dec.array();
    auto bs = a.next().as_bytes();
    REQUIRE(bs.size() == 3);
    REQUIRE(bs[0] == 0x00); REQUIRE(bs[1] == 0xFF); REQUIRE(bs[2] == 0xAB);
    dec.finish();
}

// ══════════════════════════════════════════════════════════════════════════
// Tagged semantic types
// ══════════════════════════════════════════════════════════════════════════

TEST_CASE("static_encoder: add_date_string roundtrip", "[static_encoder]") {
    uint8_t buf[256];
    encoder enc(byte_span{buf, sizeof(buf)});
    enc.open_array();
    enc.add_date_string("2024-01-15T10:30:00Z", true);
    enc.close_array();
    auto data = enc.finish();

    decoder dec(data);
    auto a = dec.array();
    REQUIRE(a.next().as_date_string() == "2024-01-15T10:30:00Z");
    dec.finish();
}

TEST_CASE("static_encoder: add_days_string roundtrip", "[static_encoder]") {
    uint8_t buf[256];
    encoder enc(byte_span{buf, sizeof(buf)});
    enc.open_array();
    enc.add_days_string("2024-01-15", true);
    enc.close_array();
    auto data = enc.finish();

    decoder dec(data);
    auto a = dec.array();
    REQUIRE(a.next().as_days_string() == "2024-01-15");
    dec.finish();
}

TEST_CASE("static_encoder: add_b64_text roundtrip", "[static_encoder]") {
    uint8_t buf[256];
    encoder enc(byte_span{buf, sizeof(buf)});
    enc.open_array();
    enc.add_b64_text("SGVsbG8=", true);
    enc.close_array();
    auto data = enc.finish();

    decoder dec(data);
    auto a = dec.array();
    REQUIRE(a.next().as_b64_text() == "SGVsbG8=");
    dec.finish();
}

TEST_CASE("static_encoder: add_b64url_text roundtrip", "[static_encoder]") {
    uint8_t buf[256];
    encoder enc(byte_span{buf, sizeof(buf)});
    enc.open_array();
    enc.add_b64url_text("SGVsbG8", true);
    enc.close_array();
    auto data = enc.finish();

    decoder dec(data);
    auto a = dec.array();
    REQUIRE(a.next().as_b64url() == "SGVsbG8");
    dec.finish();
}

TEST_CASE("static_encoder: add_regex roundtrip", "[static_encoder]") {
    uint8_t buf[256];
    encoder enc(byte_span{buf, sizeof(buf)});
    enc.open_array();
    enc.add_regex("[a-z]+", true);
    enc.close_array();
    auto data = enc.finish();

    decoder dec(data);
    auto a = dec.array();
    REQUIRE(a.next().as_regex() == "[a-z]+");
    dec.finish();
}

TEST_CASE("static_encoder: add_mime_data roundtrip", "[static_encoder]") {
    uint8_t buf[256];
    encoder enc(byte_span{buf, sizeof(buf)});
    enc.open_array();
    enc.add_mime_data("text/plain", true);
    enc.close_array();
    auto data = enc.finish();

    decoder dec(data);
    auto a = dec.array();
    bool is_bin = false;
    REQUIRE(a.next().as_mime_data(&is_bin) == "text/plain");
    dec.finish();
}

TEST_CASE("static_encoder: add_uri roundtrip", "[static_encoder]") {
    uint8_t buf[256];
    encoder enc(byte_span{buf, sizeof(buf)});
    enc.open_array();
    enc.add_uri("https://example.com", true);
    enc.close_array();
    auto data = enc.finish();

    decoder dec(data);
    auto a = dec.array();
    REQUIRE(a.next().as_uri() == "https://example.com");
    dec.finish();
}

TEST_CASE("static_encoder: add_decimal_fraction roundtrip", "[static_encoder]") {
    uint8_t buf[256];
    encoder enc(byte_span{buf, sizeof(buf)});
    enc.open_array();
    enc.add_decimal_fraction(314, -2, true);
    enc.close_array();
    auto data = enc.finish();

    decoder dec(data);
    auto a = dec.array();
    // Tag 4 → array [exp10, mantissa] → just verify decodes OK
    REQUIRE_NOTHROW(a.next());
    dec.finish();
}

// ══════════════════════════════════════════════════════════════════════════
// is_map / is_array
// ══════════════════════════════════════════════════════════════════════════

TEST_CASE("static_encoder: is_map after map()", "[static_encoder]") {
    uint8_t buf[256];
    encoder enc(byte_span{buf, sizeof(buf)});
    enc.map();
    REQUIRE(enc.is_map());
    REQUIRE(!enc.is_array());
}

TEST_CASE("static_encoder: is_array after array()", "[static_encoder]") {
    uint8_t buf[256];
    encoder enc(byte_span{buf, sizeof(buf)});
    enc.array();
    REQUIRE(enc.is_array());
    REQUIRE(!enc.is_map());
}

TEST_CASE("static_encoder: double map() throws", "[static_encoder]") {
    uint8_t buf[256];
    encoder enc(byte_span{buf, sizeof(buf)});
    enc.map();
    REQUIRE_THROWS_AS(enc.map(), error);
}

TEST_CASE("static_encoder: double array() throws", "[static_encoder]") {
    uint8_t buf[256];
    encoder enc(byte_span{buf, sizeof(buf)});
    enc.array();
    REQUIRE_THROWS_AS(enc.array(), error);
}

// ══════════════════════════════════════════════════════════════════════════
// map_builder (basic_map_builder<encoder>)
// ══════════════════════════════════════════════════════════════════════════

TEST_CASE("static_encoder: map_builder basics", "[static_encoder]") {
    uint8_t buf[512];
    encoder enc(byte_span{buf, sizeof(buf)});
    {
        auto m = enc.map();
        m["name"]    = "Alice";
        m["age"]     = 30;
        m["score"]   = 99.5;
        m["active"]  = true;
        m["unset"]   = nullptr;
    }
    auto data = enc.finish();

    decoder dec(data);
    auto m = dec.map();
    REQUIRE(std::string_view(m["name"]) == "Alice");
    REQUIRE(int64_t(m["age"]) == 30);
    REQUIRE_THAT(double(m["score"]), Catch::Matchers::WithinRel(99.5, 1e-9));
    REQUIRE(bool(m["active"]) == true);
    REQUIRE(m["unset"].is_null());
    dec.finish();
}

TEST_CASE("static_encoder: map_builder int key", "[static_encoder]") {
    uint8_t buf[256];
    encoder enc(byte_span{buf, sizeof(buf)});
    {
        auto m = enc.map();
        m[1] = "one";
        m[2] = "two";
    }
    auto data = enc.finish();

    decoder dec(data);
    auto m = dec.map();
    REQUIRE(std::string_view(m[1]) == "one");
    REQUIRE(std::string_view(m[2]) == "two");
    dec.finish();
}

TEST_CASE("static_encoder: map_builder nested map via key_proxy", "[static_encoder]") {
    uint8_t buf[512];
    encoder enc(byte_span{buf, sizeof(buf)});
    {
        auto m = enc.map();
        auto nested = m["address"].map();
        nested["city"] = "NYC";
        nested["zip"]  = 10001;
    }
    auto data = enc.finish();

    decoder dec(data);
    auto m = dec.map();
    // Read nested map: iterate entry with is_map check
    REQUIRE(m["address"].is_map());
    dec.finish();
}

TEST_CASE("static_encoder: map_builder nested array via key_proxy", "[static_encoder]") {
    uint8_t buf[512];
    encoder enc(byte_span{buf, sizeof(buf)});
    {
        auto m = enc.map();
        auto arr = m["tags"].array();
        arr << "fast" << "reliable";
    }
    auto data = enc.finish();

    decoder dec(data);
    auto m = dec.map();
    REQUIRE(m["tags"].is_array());
    dec.finish();
}

TEST_CASE("static_encoder: map_builder move semantics", "[static_encoder]") {
    uint8_t buf[512];
    encoder enc(byte_span{buf, sizeof(buf)});
    {
        map_builder m1 = enc.map();
        m1["a"] = 1;
        {
            map_builder m2 = std::move(m1);
            m2["b"] = 2;
        }
    }
    auto data = enc.finish();

    decoder dec(data);
    auto m = dec.map();
    REQUIRE(int64_t(m["a"]) == 1);
    REQUIRE(int64_t(m["b"]) == 2);
    dec.finish();
}

TEST_CASE("static_encoder: map_builder c-string key", "[static_encoder]") {
    uint8_t buf[256];
    encoder enc(byte_span{buf, sizeof(buf)});
    {
        auto m = enc.map();
        m["key"] = "value";
    }
    auto data = enc.finish();

    decoder dec(data);
    auto m = dec.map();
    REQUIRE(std::string_view(m["key"]) == "value");
    dec.finish();
}

// ══════════════════════════════════════════════════════════════════════════
// array_builder (basic_array_builder<encoder>)
// ══════════════════════════════════════════════════════════════════════════

TEST_CASE("static_encoder: array_builder basics", "[static_encoder]") {
    uint8_t buf[256];
    encoder enc(byte_span{buf, sizeof(buf)});
    {
        auto a = enc.array();
        a << 10 << "hello" << 3.14 << true << nullptr;
    }
    auto data = enc.finish();

    decoder dec(data);
    auto a = dec.array();
    REQUIRE(int64_t(a.next()) == 10);
    REQUIRE(std::string_view(a.next()) == "hello");
    REQUIRE_THAT(double(a.next()), Catch::Matchers::WithinRel(3.14, 1e-9));
    REQUIRE(bool(a.next()) == true);
    REQUIRE(a.next().is_null());
    dec.finish();
}

TEST_CASE("static_encoder: array_builder add overloads", "[static_encoder]") {
    uint8_t buf[256];
    encoder enc(byte_span{buf, sizeof(buf)});
    {
        auto a = enc.array();
        a.add(int64_t(1));
        a.add(uint64_t(2));
        a.add("text");
        a.add(true);
    }
    auto data = enc.finish();

    decoder dec(data);
    auto a = dec.array();
    REQUIRE(int64_t(a.next()) == 1);
    REQUIRE(int64_t(a.next()) == 2);
    REQUIRE(std::string_view(a.next()) == "text");
    REQUIRE(bool(a.next()) == true);
    dec.finish();
}

TEST_CASE("static_encoder: array_builder add_map", "[static_encoder]") {
    uint8_t buf[512];
    encoder enc(byte_span{buf, sizeof(buf)});
    {
        auto a = enc.array();
        {
            auto m = a.add_map();
            m["inner"] = "value";
        }
        a << 42;
    }
    auto data = enc.finish();

    decoder dec(data);
    auto a = dec.array();
    // as_map() returns a map_scope whose destructor exits the map
    {
        auto inner = a.next().as_map();
        REQUIRE(std::string_view(inner["inner"]) == "value");
    }
    REQUIRE(int64_t(a.next()) == 42);
    dec.finish();
}

TEST_CASE("static_encoder: array_builder add_array nested", "[static_encoder]") {
    uint8_t buf[512];
    encoder enc(byte_span{buf, sizeof(buf)});
    {
        auto a = enc.array();
        {
            auto inner = a.add_array();
            inner << 1 << 2;
        }
        a << 3;
    }
    auto data = enc.finish();

    decoder dec(data);
    auto a = dec.array();
    // as_array() returns an array_scope whose destructor exits the nested array
    {
        auto inner = a.next().as_array();
        REQUIRE(int64_t(inner.next()) == 1);
        REQUIRE(int64_t(inner.next()) == 2);
    }
    REQUIRE(int64_t(a.next()) == 3);
    dec.finish();
}

TEST_CASE("static_encoder: array_builder move semantics", "[static_encoder]") {
    uint8_t buf[256];
    encoder enc(byte_span{buf, sizeof(buf)});
    {
        array_builder a1 = enc.array();
        a1 << 10;
        {
            array_builder a2 = std::move(a1);
            a2 << 20 << 30;
        }
    }
    auto data = enc.finish();

    decoder dec(data);
    auto a = dec.array();
    REQUIRE(int64_t(a.next()) == 10);
    REQUIRE(int64_t(a.next()) == 20);
    REQUIRE(int64_t(a.next()) == 30);
    dec.finish();
}

// ══════════════════════════════════════════════════════════════════════════
// key_proxy (basic_key_proxy<encoder>)
// ══════════════════════════════════════════════════════════════════════════

TEST_CASE("static_encoder: key_proxy move semantics", "[static_encoder]") {
    uint8_t buf[256];
    encoder enc(byte_span{buf, sizeof(buf)});
    {
        auto m = enc.map();
        key_proxy kp = std::move(m["x"]);
        kp = 100;
    }
    auto data = enc.finish();

    decoder dec(data);
    auto m = dec.map();
    REQUIRE(int64_t(m["x"]) == 100);
    dec.finish();
}

TEST_CASE("static_encoder: key_proxy int key", "[static_encoder]") {
    uint8_t buf[256];
    encoder enc(byte_span{buf, sizeof(buf)});
    {
        auto m = enc.map();
        m[1] = "one";
        m[2] = "two";
    }
    auto data = enc.finish();

    decoder dec(data);
    auto m = dec.map();
    REQUIRE(std::string_view(m[1]) == "one");
    REQUIRE(std::string_view(m[2]) == "two");
    dec.finish();
}

// ══════════════════════════════════════════════════════════════════════════
// open_map / close_map (low-level, no RAII)
// ══════════════════════════════════════════════════════════════════════════

TEST_CASE("static_encoder: open_map/close_map low-level", "[static_encoder]") {
    uint8_t buf[256];
    encoder enc(byte_span{buf, sizeof(buf)});
    enc.open_map();
    enc.add_text("key"); enc.add_int64(42);
    enc.close_map();
    auto data = enc.finish();

    decoder dec(data);
    auto m = dec.map();
    REQUIRE(int64_t(m["key"]) == 42);
    dec.finish();
}

// ══════════════════════════════════════════════════════════════════════════
// encoded insertion
// ══════════════════════════════════════════════════════════════════════════

TEST_CASE("static_encoder: add_encoded", "[static_encoder]") {
    uint8_t buf[256];
    uint8_t inner_buf[64];
    encoder inner(byte_span{inner_buf, sizeof(inner_buf)});
    inner.add_int64(99);
    auto inner_data = inner.finish();

    encoder outer(byte_span{buf, sizeof(buf)});
    outer.open_array();
    outer.add_encoded(inner_data);
    outer.close_array();
    auto outer_data = outer.finish();

    decoder dec(outer_data);
    auto a = dec.array();
    // add_encoded inserts raw bytes — decodes as byte string
    auto bs = a.next().as_bytes();
    REQUIRE(bs.size() > 0);
    dec.finish();
}

// ══════════════════════════════════════════════════════════════════════════
// Buffer too small
// ══════════════════════════════════════════════════════════════════════════

TEST_CASE("static_encoder: buffer too small throws", "[static_encoder]") {
    uint8_t buf[1];
    encoder enc(byte_span{buf, sizeof(buf)});
    REQUIRE_THROWS_AS(enc.add_text("this won't fit"), error);
}

// ══════════════════════════════════════════════════════════════════════════
// No-preferred float encodings
// ══════════════════════════════════════════════════════════════════════════

TEST_CASE("static_encoder: add_double_no_preferred roundtrip", "[static_encoder]") {
    uint8_t buf[256];
    encoder enc(byte_span{buf, sizeof(buf)});
    enc.open_array(); enc.add_double_no_preferred(3.14); enc.close_array();
    auto data = enc.finish();

    decoder dec(data);
    auto a = dec.array();
    REQUIRE_THAT(double(a.next()), Catch::Matchers::WithinRel(3.14, 1e-9));
    dec.finish();
}

TEST_CASE("static_encoder: add_float_no_preferred roundtrip", "[static_encoder]") {
    uint8_t buf[256];
    encoder enc(byte_span{buf, sizeof(buf)});
    enc.open_array(); enc.add_float_no_preferred(1.5f); enc.close_array();
    auto data = enc.finish();

    decoder dec(data);
    auto a = dec.array();
    REQUIRE_THAT(double(a.next()), Catch::Matchers::WithinRel(1.5, 1e-6));
    dec.finish();
}

// ══════════════════════════════════════════════════════════════════════════
// dynamic_encoder: verify builder templates with both classes
// ══════════════════════════════════════════════════════════════════════════

TEST_CASE("dynamic_encoder: template builder roundtrip", "[dynamic_encoder][static_encoder]") {
    dynamic_encoder enc;
    {
        auto m = enc.map();
        m["int"]    = 42;
        m["text"]   = "hello";
        m["float"]  = 2.718;
        m["bool"]   = true;
    }
    auto data = enc.finish();

    decoder dec(data);
    auto m = dec.map();
    REQUIRE(int64_t(m["int"]) == 42);
    REQUIRE(std::string_view(m["text"]) == "hello");
    REQUIRE_THAT(double(m["float"]), Catch::Matchers::WithinRel(2.718, 1e-9));
    REQUIRE(bool(m["bool"]) == true);
    dec.finish();
}

TEST_CASE("dynamic_encoder: data() accessor", "[dynamic_encoder]") {
    dynamic_encoder enc;
    enc.add_int64(123);
    auto data = enc.finish();
    REQUIRE(data.size() > 0);
    REQUIRE(data.size() == enc.data().size());
}

TEST_CASE("dynamic_encoder: reserve / capacity", "[dynamic_encoder]") {
    dynamic_encoder enc(512);
    REQUIRE(enc.capacity() >= 512);
    enc.reserve(1024);
    REQUIRE(enc.capacity() >= 1024);
}

// ── proxy_map_depth_ guard tests ──

TEST_CASE("proxy_map_depth_: normal chaining ok", "[encoder][depth]") {
    uint8_t buf[256];
    encoder enc(byte_span{buf, sizeof(buf)});
    {
        auto m = enc.map();
        m["a"]["b"] = 42;
        CHECK(enc.proxy_map_depth() == 0);
    }
    enc.finish();
}

TEST_CASE("proxy_map_depth_: throws when depth > 0", "[encoder][depth]") {
    uint8_t buf[256];
    encoder enc(byte_span{buf, sizeof(buf)});
    auto   m = enc.map();
    auto   kp = m["outer"];          // kp alive, depth still 0
    kp["inner"] = 100;               // kp now owns_map_, depth = 1
    REQUIRE(enc.proxy_map_depth() == 1);
    CHECK_THROWS_AS(m["bad"] = 42, qcborpp::error);  // builder blocked
    // kp destroyed here → close_if_owns → depth → 0
}

TEST_CASE("proxy_map_depth_: multi-level chaining ok", "[encoder][depth]") {
    uint8_t buf[256];
    encoder enc(byte_span{buf, sizeof(buf)});
    {
        auto m = enc.map();
        m["a"]["b"]["c"] = 999;
        CHECK(enc.proxy_map_depth() == 0);
    }
    enc.finish();
}

TEST_CASE("proxy_map_depth_: dynamic_encoder throws", "[dynamic_encoder][depth]") {
    dynamic_encoder enc;
    auto   m = enc.map();
    auto   kp = m["outer"];
    kp["inner"] = 100;
    REQUIRE(enc.proxy_map_depth() == 1);
    CHECK_THROWS_AS(m["bad"] = 42, qcborpp::error);
}

TEST_CASE("proxy_map_depth_: depth released after kp destroyed", "[encoder][depth]") {
    uint8_t buf[256];
    encoder enc(byte_span{buf, sizeof(buf)});
    {
        auto m = enc.map();
        {
            auto kp = m["outer"];
            kp["inner"] = 100;
            REQUIRE(enc.proxy_map_depth() == 1);
        }
        // kp 析构，depth 归 0，后续操作正常
        CHECK(enc.proxy_map_depth() == 0);
        m["safe"] = 42;
    }
    enc.finish();
}
