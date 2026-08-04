/*
 * tests/test_wire_format.cpp
 *
 * Exact wire-format (RFC 8949 known-answer) tests for qcborpp encoders.
 * Each test encodes a value and asserts the exact CBOR bytes produced.
 * This catches encoding regressions that roundtrip tests miss: wrong
 * major type, wrong additional info, unexpected preferred serialization, etc.
 *
 * Intepretation of RFC 8949 Appendix A examples and common test vectors.
 */

#include <qcborpp/qcborpp.hpp>
#include <catch2/catch_test_macros.hpp>
#include <array>

using namespace qcborpp;

// ── helpers ──

/** Assert that a byte span exactly matches an expected hex string (no spaces). */
static void require_bytes(const_byte_span actual, const std::vector<uint8_t>& expected) {
    REQUIRE(actual.size() == expected.size());
    for (size_t i = 0; i < expected.size(); ++i) {
        INFO("byte mismatch at index " << i);
        REQUIRE(actual[i] == expected[i]);
    }
}

// ── unsigned integers (major type 0) ──

TEST_CASE("wire: uint64_t 0", "[wire_unsigned]") {
    uint8_t buf[16];
    encoder enc(byte_span{buf, sizeof(buf)});
    enc.add_uint64(0);
    auto data = enc.finish();
    require_bytes(data, {0x00});
}

TEST_CASE("wire: uint64_t 1", "[wire_unsigned]") {
    uint8_t buf[16];
    encoder enc(byte_span{buf, sizeof(buf)});
    enc.add_uint64(1);
    auto data = enc.finish();
    require_bytes(data, {0x01});
}

TEST_CASE("wire: uint64_t 10", "[wire_unsigned]") {
    uint8_t buf[16];
    encoder enc(byte_span{buf, sizeof(buf)});
    enc.add_uint64(10);
    auto data = enc.finish();
    require_bytes(data, {0x0A});
}

TEST_CASE("wire: uint64_t 23", "[wire_unsigned]") {
    uint8_t buf[16];
    encoder enc(byte_span{buf, sizeof(buf)});
    enc.add_uint64(23);
    auto data = enc.finish();
    require_bytes(data, {0x17});
}

TEST_CASE("wire: uint64_t 24", "[wire_unsigned]") {
    uint8_t buf[16];
    encoder enc(byte_span{buf, sizeof(buf)});
    enc.add_uint64(24);
    auto data = enc.finish();
    require_bytes(data, {0x18, 0x18});
}

TEST_CASE("wire: uint64_t 100", "[wire_unsigned]") {
    uint8_t buf[16];
    encoder enc(byte_span{buf, sizeof(buf)});
    enc.add_uint64(100);
    auto data = enc.finish();
    require_bytes(data, {0x18, 0x64});
}

TEST_CASE("wire: uint64_t 1000", "[wire_unsigned]") {
    uint8_t buf[16];
    encoder enc(byte_span{buf, sizeof(buf)});
    enc.add_uint64(1000);
    auto data = enc.finish();
    require_bytes(data, {0x19, 0x03, 0xE8});
}

TEST_CASE("wire: uint64_t 1000000", "[wire_unsigned]") {
    uint8_t buf[16];
    encoder enc(byte_span{buf, sizeof(buf)});
    enc.add_uint64(1000000);
    auto data = enc.finish();
    require_bytes(data, {0x1A, 0x00, 0x0F, 0x42, 0x40});
}

TEST_CASE("wire: uint64_t 1000000000000", "[wire_unsigned]") {
    uint8_t buf[16];
    encoder enc(byte_span{buf, sizeof(buf)});
    enc.add_uint64(1000000000000LL);
    auto data = enc.finish();
    require_bytes(data, {0x1B, 0x00, 0x00, 0x00, 0xE8, 0xD4, 0xA5, 0x10, 0x00});
}

TEST_CASE("wire: uint64_t max", "[wire_unsigned]") {
    uint8_t buf[16];
    encoder enc(byte_span{buf, sizeof(buf)});
    enc.add_uint64(18446744073709551615ULL);
    auto data = enc.finish();
    require_bytes(data, {0x1B, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF});
}

// ── negative integers (major type 1) ──

TEST_CASE("wire: int64_t -1", "[wire_negative]") {
    uint8_t buf[16];
    encoder enc(byte_span{buf, sizeof(buf)});
    enc.add_int64(-1);
    auto data = enc.finish();
    require_bytes(data, {0x20});
}

TEST_CASE("wire: int64_t -10", "[wire_negative]") {
    uint8_t buf[16];
    encoder enc(byte_span{buf, sizeof(buf)});
    enc.add_int64(-10);
    auto data = enc.finish();
    require_bytes(data, {0x29});
}

TEST_CASE("wire: int64_t -100", "[wire_negative]") {
    uint8_t buf[16];
    encoder enc(byte_span{buf, sizeof(buf)});
    enc.add_int64(-100);
    auto data = enc.finish();
    require_bytes(data, {0x38, 0x63});
}

TEST_CASE("wire: int64_t -1000", "[wire_negative]") {
    uint8_t buf[16];
    encoder enc(byte_span{buf, sizeof(buf)});
    enc.add_int64(-1000);
    auto data = enc.finish();
    require_bytes(data, {0x39, 0x03, 0xE7});
}

// ── byte strings (major type 2) ──

TEST_CASE("wire: empty bytes", "[wire_bytes]") {
    uint8_t buf[16];
    encoder enc(byte_span{buf, sizeof(buf)});
    enc.add_bytes(const_byte_span{});
    auto data = enc.finish();
    require_bytes(data, {0x40});
}

TEST_CASE("wire: bytes length 1", "[wire_bytes]") {
    uint8_t buf[16];
    std::array<uint8_t, 1> raw = {{0x41}};
    encoder enc(byte_span{buf, sizeof(buf)});
    enc.add_bytes(const_byte_span{raw.data(), raw.size()});
    auto data = enc.finish();
    require_bytes(data, {0x41, 0x41});
}

TEST_CASE("wire: bytes length 3", "[wire_bytes]") {
    uint8_t buf[16];
    std::array<uint8_t, 3> raw = {{0x01, 0x02, 0x03}};
    encoder enc(byte_span{buf, sizeof(buf)});
    enc.add_bytes(const_byte_span{raw.data(), raw.size()});
    auto data = enc.finish();
    require_bytes(data, {0x43, 0x01, 0x02, 0x03});
}

// ── text strings (major type 3) ──

TEST_CASE("wire: empty text", "[wire_text]") {
    uint8_t buf[16];
    encoder enc(byte_span{buf, sizeof(buf)});
    enc.add_text("");
    auto data = enc.finish();
    require_bytes(data, {0x60});
}

TEST_CASE("wire: text 'a'", "[wire_text]") {
    uint8_t buf[16];
    encoder enc(byte_span{buf, sizeof(buf)});
    enc.add_text("a");
    auto data = enc.finish();
    require_bytes(data, {0x61, 0x61});
}

TEST_CASE("wire: text 'IETF'", "[wire_text]") {
    uint8_t buf[16];
    encoder enc(byte_span{buf, sizeof(buf)});
    enc.add_text("IETF");
    auto data = enc.finish();
    require_bytes(data, {0x64, 0x49, 0x45, 0x54, 0x46});
}

// ── arrays (major type 4) ──

TEST_CASE("wire: empty array", "[wire_array]") {
    uint8_t buf[16];
    encoder enc(byte_span{buf, sizeof(buf)});
    {
        auto a = enc.array();
    }
    auto data = enc.finish();
    require_bytes(data, {0x80});
}

TEST_CASE("wire: array [1, 2, 3]", "[wire_array]") {
    uint8_t buf[16];
    encoder enc(byte_span{buf, sizeof(buf)});
    {
        auto a = enc.array();
        a << int64_t(1) << int64_t(2) << int64_t(3);
    }
    auto data = enc.finish();
    require_bytes(data, {0x83, 0x01, 0x02, 0x03});
}

TEST_CASE("wire: nested arrays", "[wire_array]") {
    uint8_t buf[32];
    encoder enc(byte_span{buf, sizeof(buf)});
    {
        auto outer = enc.array();
        {
            auto inner = outer.add_array();
            inner << int64_t(1) << int64_t(2);
        }
        outer << int64_t(3);
    }
    auto data = enc.finish();
    require_bytes(data, {0x82, 0x82, 0x01, 0x02, 0x03});
}

// ── maps (major type 5) ──

TEST_CASE("wire: empty map", "[wire_map]") {
    uint8_t buf[16];
    encoder enc(byte_span{buf, sizeof(buf)});
    {
        auto m = enc.map();
    }
    auto data = enc.finish();
    require_bytes(data, {0xA0});
}

TEST_CASE("wire: map {1: 2, 3: 4}", "[wire_map]") {
    uint8_t buf[32];
    encoder enc(byte_span{buf, sizeof(buf)});
    enc.open_map();
    enc.add_int64(1);
    enc.add_int64(2);
    enc.add_int64(3);
    enc.add_int64(4);
    enc.close_map();
    auto data = enc.finish();
    require_bytes(data, {0xA2, 0x01, 0x02, 0x03, 0x04});
}

// ── tagged items ──

TEST_CASE("wire: tag 1 (epoch) int", "[wire_tagged]") {
    uint8_t buf[16];
    encoder enc(byte_span{buf, sizeof(buf)});
    enc.add_tag(1);
    enc.add_int64(1700000000);
    auto data = enc.finish();
    // Tag 1 = 0xC1, followed by int64: 1700000000 = 0x65 0x53 0xF1 0x00
    require_bytes(data, {0xC1, 0x1A, 0x65, 0x53, 0xF1, 0x00});
}

TEST_CASE("wire: tag 2 (pos bignum) bytes", "[wire_tagged]") {
    uint8_t buf[16];
    std::array<uint8_t, 3> raw = {{0x01, 0x02, 0x03}};
    encoder enc(byte_span{buf, sizeof(buf)});
    enc.add_tag(2);
    enc.add_bytes(const_byte_span{raw.data(), raw.size()});
    auto data = enc.finish();
    // Tag 2 = 0xC2, byte string length 3 = 0x43, then bytes
    require_bytes(data, {0xC2, 0x43, 0x01, 0x02, 0x03});
}

TEST_CASE("wire: tag 3 (neg bignum) bytes", "[wire_tagged]") {
    uint8_t buf[16];
    std::array<uint8_t, 1> raw = {{0x01}};
    encoder enc(byte_span{buf, sizeof(buf)});
    enc.add_tag(3);
    enc.add_bytes(const_byte_span{raw.data(), raw.size()});
    auto data = enc.finish();
    // Tag 3 = 0xC3, byte string length 1 = 0x41, then 0x01
    require_bytes(data, {0xC3, 0x41, 0x01});
}

TEST_CASE("wire: tag 32 (URI)", "[wire_tagged]") {
    uint8_t buf[32];
    encoder enc(byte_span{buf, sizeof(buf)});
    enc.add_tag(32);
    enc.add_text("https://example.com");
    auto data = enc.finish();
    // Tag 32 = 0xD8 0x20, text "https://example.com"
    std::vector<uint8_t> expected = {0xD8, 0x20};
    // text string of length 19
    expected.push_back(0x73); // major type 3, len 19
    const char* uri = "https://example.com";
    for (const char* p = uri; *p; ++p) expected.push_back((uint8_t)*p);
    require_bytes(data, expected);
}

TEST_CASE("wire: tag 37 (UUID) raw bytes", "[wire_tagged]") {
    uint8_t buf[32];
    std::array<uint8_t, 16> uuid = {{0x55, 0x04, 0x40, 0x12, 0x20, 0x00, 0x80, 0x00,
                                     0x00, 0x05, 0x00, 0x02, 0x1A, 0x2B, 0x3C, 0x4D}};
    encoder enc(byte_span{buf, sizeof(buf)});
    enc.add_tag(37);
    enc.add_bytes(const_byte_span{uuid.data(), uuid.size()});
    auto data = enc.finish();
    // Tag 37 = 0xD8 0x25, byte string length 16 = 0x50
    std::vector<uint8_t> expected = {0xD8, 0x25, 0x50};
    for (auto b : uuid) expected.push_back(b);
    require_bytes(data, expected);
}

TEST_CASE("wire: tag 100 (days epoch)", "[wire_tagged]") {
    uint8_t buf[16];
    encoder enc(byte_span{buf, sizeof(buf)});
    enc.add_tag(100);
    enc.add_int64(20000);
    auto data = enc.finish();
    // Tag 100 = 0xD8 0x64, int64 20000 = 0x19 0x4E 0x20
    require_bytes(data, {0xD8, 0x64, 0x19, 0x4E, 0x20});
}

TEST_CASE("wire: tag false (as_tag=false, plain value)", "[wire_tagged]") {
    uint8_t buf[16];
    encoder enc(byte_span{buf, sizeof(buf)});
    // add_decimal_fraction without tag → plain array
    enc.add_decimal_fraction(273, -2, false);
    auto data = enc.finish();
    // Plain array [exp10=-2, mantissa=273]: 0x82, 0x21, 0x19 0x01 0x11
    // -2 → 0x21, 273 → 0x19 0x01 0x11
    require_bytes(data, {0x82, 0x21, 0x19, 0x01, 0x11});
}

// ── floats (major type 7) ──

TEST_CASE("wire: float 0.0", "[wire_float]") {
    uint8_t buf[16];
    encoder enc(byte_span{buf, sizeof(buf)});
    enc.add_float(0.0f);
    auto data = enc.finish();
    // Preferred serialization: 16-bit float 0.0 = 0xF9 0x00 0x00
    require_bytes(data, {0xF9, 0x00, 0x00});
}

TEST_CASE("wire: float 1.0", "[wire_float]") {
    uint8_t buf[16];
    encoder enc(byte_span{buf, sizeof(buf)});
    enc.add_float(1.0f);
    auto data = enc.finish();
    // Preferred: 16-bit (0xF9) 1.0 = 0x3C 0x00
    require_bytes(data, {0xF9, 0x3C, 0x00});
}

TEST_CASE("wire: float -4.0", "[wire_float]") {
    uint8_t buf[16];
    encoder enc(byte_span{buf, sizeof(buf)});
    enc.add_float(-4.0f);
    auto data = enc.finish();
    // Preferred: 16-bit -4.0 = 0xF9 0xC4 0x00
    require_bytes(data, {0xF9, 0xC4, 0x00});
}

TEST_CASE("wire: double 1.1", "[wire_float]") {
    uint8_t buf[16];
    encoder enc(byte_span{buf, sizeof(buf)});
    enc.add_double(1.1);
    auto data = enc.finish();
    // 1.1 cannot be exactly represented → 64-bit double
    REQUIRE(data.size() == 9);
    REQUIRE(data[0] == 0xFB);
}

TEST_CASE("wire: float NaN (quiet)", "[wire_float]") {
    uint8_t buf[16];
    encoder enc(byte_span{buf, sizeof(buf)});
    float nan = std::numeric_limits<float>::quiet_NaN();
    enc.add_float(nan);
    auto data = enc.finish();
    // NaN preferred: 16-bit = 0xF9 0x7E 0x00
    require_bytes(data, {0xF9, 0x7E, 0x00});
}

TEST_CASE("wire: float Infinity", "[wire_float]") {
    uint8_t buf[16];
    encoder enc(byte_span{buf, sizeof(buf)});
    enc.add_float(std::numeric_limits<float>::infinity());
    auto data = enc.finish();
    // +Infinity: 16-bit = 0xF9 0x7C 0x00
    require_bytes(data, {0xF9, 0x7C, 0x00});
}

TEST_CASE("wire: float -Infinity", "[wire_float]") {
    uint8_t buf[16];
    encoder enc(byte_span{buf, sizeof(buf)});
    enc.add_float(-std::numeric_limits<float>::infinity());
    auto data = enc.finish();
    // -Infinity: 16-bit = 0xF9 0xFC 0x00
    require_bytes(data, {0xF9, 0xFC, 0x00});
}

TEST_CASE("wire: double_no_preferred 1.0", "[wire_float]") {
    uint8_t buf[16];
    encoder enc(byte_span{buf, sizeof(buf)});
    enc.add_double_no_preferred(1.0);
    auto data = enc.finish();
    // NoPreferred forces 8-byte double even for exact 1.0
    require_bytes(data, {0xFB, 0x3F, 0xF0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00});
}

// ── simple values (major type 7) ──

TEST_CASE("wire: false", "[wire_simple]") {
    uint8_t buf[16];
    encoder enc(byte_span{buf, sizeof(buf)});
    enc.add_bool(false);
    auto data = enc.finish();
    require_bytes(data, {0xF4});
}

TEST_CASE("wire: true", "[wire_simple]") {
    uint8_t buf[16];
    encoder enc(byte_span{buf, sizeof(buf)});
    enc.add_bool(true);
    auto data = enc.finish();
    require_bytes(data, {0xF5});
}

TEST_CASE("wire: null", "[wire_simple]") {
    uint8_t buf[16];
    encoder enc(byte_span{buf, sizeof(buf)});
    enc.add_null();
    auto data = enc.finish();
    require_bytes(data, {0xF6});
}

TEST_CASE("wire: undefined", "[wire_simple]") {
    uint8_t buf[16];
    encoder enc(byte_span{buf, sizeof(buf)});
    enc.add_undef();
    auto data = enc.finish();
    require_bytes(data, {0xF7});
}
