/*
 * tests/test_numeric_conversion.cpp
 *
 * Boundary / edge-case tests for item_proxy numeric conversions.
 * Covers get_int64 / get_uint64 / get_double (loose, cross-type) and
 * as_int64 / as_uint64 / as_double (strict, type-matched) at values that
 * exercise overflow, wrap-around, NaN/Inf, and precision loss.
 *
 * Tag: [numeric_convert]
 */
#include <catch2/catch_test_macros.hpp>
#include <qcborpp/qcborpp.hpp>
#include <cstdint>
#include <cmath>
#include <limits>
using namespace qcborpp;

// ── helpers ──

static const_byte_span encode_one(uint8_t* buf, size_t sz, const char* key, int64_t v) {
    encoder enc(byte_span{buf, sz});
    { auto m = enc.map();  m[key] = v; }
    return enc.finish();
}
static const_byte_span encode_one(uint8_t* buf, size_t sz, const char* key, uint64_t v) {
    encoder enc(byte_span{buf, sz});
    { auto m = enc.map();  m[key] = v; }
    return enc.finish();
}
static const_byte_span encode_one(uint8_t* buf, size_t sz, const char* key, double v) {
    encoder enc(byte_span{buf, sz});
    { auto m = enc.map();  m[key] = v; }
    return enc.finish();
}
static const_byte_span encode_one(uint8_t* buf, size_t sz, const char* key, float v) {
    encoder enc(byte_span{buf, sz});
    { auto m = enc.map();  m[key] = v; }   // float → encoder trips to float wire when small
    return enc.finish();
}

// Encode a float without preferred encoding to force float32 wire
static const_byte_span encode_float_no_preferred(uint8_t* buf, size_t sz, float v) {
    encoder enc(byte_span{buf, sz});
    enc.open_map();
    enc.add_text("val");
    enc.add_float_no_preferred(v);
    enc.close_map();
    return enc.finish();
}

// ===== get_int64 — boundary =====

TEST_CASE("get_int64: from UINT64_MAX wraps impl-defined", "[numeric_convert]") {
    uint8_t buf[128];
    auto data = encode_one(buf, sizeof(buf), "v", UINT64_MAX);
    decoder dec(data);
    {
        auto m = dec.map();
        int64_t v = m["v"].get_int64();
        // static_cast<int64_t>(UINT64_MAX) is implementation-defined on MSVC: -1
        // We only assert the call does not crash or throw.
        CHECK((v == -1 || v == std::numeric_limits<int64_t>::max())); // either typical
    }
    REQUIRE_FALSE(dec.finish());
}

TEST_CASE("get_uint64: from negative int64 wraps", "[numeric_convert]") {
    uint8_t buf[128];
    auto data = encode_one(buf, sizeof(buf), "v", int64_t(-1));
    decoder dec(data);
    {
        auto m = dec.map();
        uint64_t v = m["v"].get_uint64();
        CHECK(v == UINT64_MAX); // -1 wraps to max
    }
    REQUIRE_FALSE(dec.finish());
}

TEST_CASE("get_int64: from NaN is impl-defined", "[numeric_convert]") {
    uint8_t buf[128];
    double nan = std::numeric_limits<double>::quiet_NaN();
    auto data = encode_one(buf, sizeof(buf), "v", nan);
    decoder dec(data);
    {
        auto m = dec.map();
        int64_t v = m["v"].get_int64();
        // llround(NaN) is implementation-defined (MSVC returns LLONG_MIN)
        // Just verify no throw.
        (void)v;
    }
    REQUIRE_FALSE(dec.finish());
}

TEST_CASE("get_int64: from +Inf is impl-defined", "[numeric_convert]") {
    uint8_t buf[128];
    double inf = std::numeric_limits<double>::infinity();
    auto data = encode_one(buf, sizeof(buf), "v", inf);
    decoder dec(data);
    {
        auto m = dec.map();
        int64_t v = m["v"].get_int64();
        // llround(+Inf): MSVC returns LLONG_MAX
        (void)v;
    }
    REQUIRE_FALSE(dec.finish());
}

TEST_CASE("get_int64: from -Inf is impl-defined", "[numeric_convert]") {
    uint8_t buf[128];
    double ninf = -std::numeric_limits<double>::infinity();
    auto data = encode_one(buf, sizeof(buf), "v", ninf);
    decoder dec(data);
    {
        auto m = dec.map();
        int64_t v = m["v"].get_int64();
        // llround(-Inf): MSVC returns LLONG_MIN
        (void)v;
    }
    REQUIRE_FALSE(dec.finish());
}

TEST_CASE("get_uint64: from negative double wraps or errors", "[numeric_convert]") {
    uint8_t buf[128];
    auto data = encode_one(buf, sizeof(buf), "v", -3.14);
    decoder dec(data);
    {
        auto m = dec.map();
        uint64_t v = m["v"].get_uint64();
        // static_cast<uint64_t>(llround(-3.14)) = (uint64_t)(-3) = huge
        CHECK(v > (UINT64_MAX >> 1)); // definitely wrapped
    }
    REQUIRE_FALSE(dec.finish());
}

TEST_CASE("get_int64: from float converts", "[numeric_convert]") {
    uint8_t buf[128];
    auto data = encode_float_no_preferred(buf, sizeof(buf), 42.0f);
    decoder dec(data);
    {
        auto m = dec.map();
        CHECK(m["val"].get_int64() == 42);
    }
    REQUIRE_FALSE(dec.finish());
}

// ===== get_double — boundary =====

TEST_CASE("get_double: from UINT64_MAX loses precision", "[numeric_convert]") {
    uint8_t buf[128];
    auto data = encode_one(buf, sizeof(buf), "v", UINT64_MAX);
    decoder dec(data);
    {
        auto m = dec.map();
        double d = m["v"].get_double();
        // UINT64_MAX = 2^64 - 1, not exactly representable in double (> 2^53).
        // Nearest IEEE 754 double is exactly 2^64 (exponent=64, mantissa=0).
        // static_cast<uint64_t>(2^64) is UB — behaviour differs across
        // architectures (x86 wraps, ARM saturates). Compare the double value
        // directly: it must equal the IEEE 754 rounding of UINT64_MAX (i.e.
        // 2^64), which proves the original integer value was not preserved.
        CHECK(d > 0.0);
        CHECK(d == static_cast<double>(UINT64_MAX)); // rounds to 2^64
    }
    REQUIRE_FALSE(dec.finish());
}

TEST_CASE("get_double: from INT64_MIN preserves sign", "[numeric_convert]") {
    uint8_t buf[128];
    auto data = encode_one(buf, sizeof(buf), "v", std::numeric_limits<int64_t>::min());
    decoder dec(data);
    {
        auto m = dec.map();
        double d = m["v"].get_double();
        CHECK(d < 0.0);
        CHECK(std::abs(d - static_cast<double>(std::numeric_limits<int64_t>::min())) < 1e10);
    }
    REQUIRE_FALSE(dec.finish());
}

TEST_CASE("get_double: from float converts", "[numeric_convert]") {
    uint8_t buf[128];
    auto data = encode_float_no_preferred(buf, sizeof(buf), 3.14f);
    decoder dec(data);
    {
        auto m = dec.map();
        double d = m["val"].get_double();
        CHECK(std::abs(d - 3.14) < 1e-4);
    }
    REQUIRE_FALSE(dec.finish());
}

TEST_CASE("get_double: NaN round-trips", "[numeric_convert]") {
    uint8_t buf[128];
    double nan = std::numeric_limits<double>::quiet_NaN();
    auto data = encode_one(buf, sizeof(buf), "v", nan);
    decoder dec(data);
    {
        auto m = dec.map();
        double d = m["v"].get_double();
        CHECK(std::isnan(d));
    }
    REQUIRE_FALSE(dec.finish());
}

TEST_CASE("get_double: +Inf round-trips", "[numeric_convert]") {
    uint8_t buf[128];
    double inf = std::numeric_limits<double>::infinity();
    auto data = encode_one(buf, sizeof(buf), "v", inf);
    decoder dec(data);
    {
        auto m = dec.map();
        double d = m["v"].get_double();
        CHECK(std::isinf(d));
        CHECK(d > 0.0);
    }
    REQUIRE_FALSE(dec.finish());
}

TEST_CASE("get_double: -Inf round-trips", "[numeric_convert]") {
    uint8_t buf[128];
    double ninf = -std::numeric_limits<double>::infinity();
    auto data = encode_one(buf, sizeof(buf), "v", ninf);
    decoder dec(data);
    {
        auto m = dec.map();
        double d = m["v"].get_double();
        CHECK(std::isinf(d));
        CHECK(d < 0.0);
    }
    REQUIRE_FALSE(dec.finish());
}

// ===== as_* — strict type rejection =====

TEST_CASE("as_int64: rejects uint64 when value exceeds INT64_MAX (no finish)", "[numeric_convert]") {
    uint8_t buf[128];
    // Must be > INT64_MAX so QCBOR encodes as uint64, not normalized to int64
    auto data = encode_one(buf, sizeof(buf), "v", UINT64_MAX);
    decoder dec(data);
    auto m = dec.map();
    CHECK_THROWS_AS(m["v"].as_int64(), error);
}

TEST_CASE("as_double: accepts double value", "[numeric_convert]") {
    // QCBOR transparently promotes float to double — we cannot test float rejection.
    // Verify as_double() succeeds with actual double input instead.
    uint8_t buf[128];
    auto data = encode_one(buf, sizeof(buf), "v", 3.14);
    decoder dec(data);
    {
        auto m = dec.map();
        CHECK(m["v"].as_double() == 3.14);
    }
    REQUIRE_FALSE(dec.finish());
}

TEST_CASE("as_uint64: rejects int64 negative (no finish check)", "[numeric_convert]") {
    // as_uint64 throws, leaving error in context — finish() would fail
    uint8_t buf[128];
    auto data = encode_one(buf, sizeof(buf), "v", int64_t(-5));
    decoder dec(data);
    auto m = dec.map();
    CHECK_THROWS_AS(m["v"].as_uint64(), error);
}

// ===== get_or / try_get — boundary silent fallback =====

TEST_CASE("get_or: type mismatch returns default not via conversion", "[numeric_convert]") {
    uint8_t buf[128];
    auto data = encode_one(buf, sizeof(buf), "v", int64_t(100));
    decoder dec(data);
    {
        auto m = dec.map();
        // get_or<double> with int64 item → cross-numeric convert, not default
        CHECK(m["v"].get_or(0.0) == 100.0);
        // get_or<int64_t> with string → type mismatch → returns default
        uint8_t b2[128];
        encoder enc(byte_span{b2, sizeof(b2)});
        {
            auto mx = enc.map();
            mx["name"] = "Niels";
        }
        decoder dec2(enc.finish());
        auto m2 = dec2.map();
        CHECK(m2["name"].get_or(int64_t(-1)) == -1);
    }
    REQUIRE_FALSE(dec.finish());
}

// ===== get_uint64 — 0 and max =====

TEST_CASE("get_uint64: from uint64_t 0", "[numeric_convert]") {
    uint8_t buf[128];
    auto data = encode_one(buf, sizeof(buf), "v", uint64_t(0));
    decoder dec(data);
    {
        auto m = dec.map();
        CHECK(m["v"].get_uint64() == 0);
    }
    REQUIRE_FALSE(dec.finish());
}

TEST_CASE("get_uint64: from int64 0", "[numeric_convert]") {
    uint8_t buf[128];
    auto data = encode_one(buf, sizeof(buf), "v", int64_t(0));
    decoder dec(data);
    {
        auto m = dec.map();
        CHECK(m["v"].get_uint64() == 0);
    }
    REQUIRE_FALSE(dec.finish());
}
