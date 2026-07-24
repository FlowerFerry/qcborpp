/*
 * tests/test_encoder_primitives.cpp
 *
 * Tests for encoding primitive types: int64, uint64, text, bytes,
 * double, float, bool, null, undef, simple.
 */

#include <qcborpp/qcborpp.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace qcborpp;

TEST_CASE("dynamic_encoder: reserve and capacity", "[dynamic_encoder]") {
    dynamic_encoder enc;
    REQUIRE(enc.capacity() > 0);

    enc.reserve(1024);
    REQUIRE(enc.capacity() >= 1024);
}

TEST_CASE("dynamic_encoder: construct with reserve", "[dynamic_encoder]") {
    dynamic_encoder enc(512);
    REQUIRE(enc.capacity() >= 512);
}

TEST_CASE("dynamic_encoder: add_int64 positive zero negative", "[dynamic_encoder][primitives]") {
    dynamic_encoder enc;
    enc.open_array();

    enc.add_int64(0);
    enc.add_int64(42);
    enc.add_int64(-128);
    enc.add_int64(1000000);
    enc.add_int64(INT64_MAX);
    enc.add_int64(INT64_MIN);

    enc.close_array();
    auto data = enc.finish();
    REQUIRE(data.size() > 0);

    // Decode back
    decoder dec(data);
    auto arr = dec.array();
    REQUIRE(!arr.done());
    REQUIRE(int64_t(arr.next()) == 0);
    REQUIRE(int64_t(arr.next()) == 42);
    REQUIRE(int64_t(arr.next()) == -128);
    REQUIRE(int64_t(arr.next()) == 1000000);
    REQUIRE(int64_t(arr.next()) == INT64_MAX);
    REQUIRE(int64_t(arr.next()) == INT64_MIN);
    dec.finish();
}

TEST_CASE("dynamic_encoder: add_uint64", "[dynamic_encoder][primitives]") {
    dynamic_encoder enc;
    enc.open_array();
    enc.add_uint64(0);
    enc.add_uint64(1);
    enc.add_uint64(UINT64_MAX);
    enc.close_array();
    auto data = enc.finish();

    decoder dec(data);
    auto arr = dec.array();
    REQUIRE(static_cast<uint64_t>(arr.next()) == 0);
    REQUIRE(static_cast<uint64_t>(arr.next()) == 1);
    REQUIRE(static_cast<uint64_t>(arr.next()) == UINT64_MAX);
    dec.finish();
}

TEST_CASE("dynamic_encoder: add_text empty and non-empty", "[dynamic_encoder][primitives]") {
    dynamic_encoder enc;
    enc.open_array();
    enc.add_text("");
    enc.add_text("hello");
    enc.add_text("world");
    enc.close_array();
    auto data = enc.finish();

    decoder dec(data);
    auto arr = dec.array();
    REQUIRE(std::string(arr.next()) == "");
    REQUIRE(std::string(arr.next()) == "hello");
    REQUIRE(std::string(arr.next()) == "world");
    dec.finish();
}

TEST_CASE("dynamic_encoder: add_text via const char*", "[dynamic_encoder][primitives]") {
    dynamic_encoder enc;
    enc.open_array();
    enc.add_text("hello");
    enc.close_array();
    auto data = enc.finish();

    decoder dec(data);
    auto arr = dec.array();
    REQUIRE(std::string_view(arr.next()) == "hello");
    dec.finish();
}

TEST_CASE("dynamic_encoder: add_bytes", "[dynamic_encoder][primitives]") {
    std::vector<uint8_t> raw = {0x00, 0x01, 0xFF, 0xFE};
    dynamic_encoder enc;
    enc.open_array();
    enc.add_bytes({raw.data(), raw.size()});
    enc.close_array();
    auto data = enc.finish();

    decoder dec(data);
    auto arr = dec.array();
    auto result = static_cast<const_byte_span>(arr.next());
    REQUIRE(result.size() == 4);
    REQUIRE(result[0] == 0x00);
    REQUIRE(result[1] == 0x01);
    REQUIRE(result[2] == 0xFF);
    REQUIRE(result[3] == 0xFE);
    dec.finish();
}

TEST_CASE("dynamic_encoder: add_double and add_float", "[dynamic_encoder][primitives]") {
    dynamic_encoder enc;
    enc.open_array();
    enc.add_double(3.14159265358979);
    enc.add_float(2.71828f);
    enc.close_array();
    auto data = enc.finish();

    decoder dec(data);
    auto arr = dec.array();
    REQUIRE_THAT(static_cast<double>(arr.next()), Catch::Matchers::WithinAbs(3.14159265358979, 1e-12));
    REQUIRE_THAT(static_cast<double>(arr.next()), Catch::Matchers::WithinAbs(2.71828, 1e-6));
    dec.finish();
}

TEST_CASE("dynamic_encoder: add_double_no_preferred", "[dynamic_encoder][primitives]") {
    dynamic_encoder enc;
    enc.open_array();
    enc.add_double_no_preferred(1.0);
    enc.close_array();
    auto data = enc.finish();
    // With NoPreferred, even 1.0 should be encoded as 8-byte double.
    // 1 byte array header + 1 byte header + 8 byte double = 10
    REQUIRE(data.size() >= 9);
}

TEST_CASE("dynamic_encoder: add_bool", "[dynamic_encoder][primitives]") {
    dynamic_encoder enc;
    enc.open_array();
    enc.add_bool(true);
    enc.add_bool(false);
    enc.close_array();
    auto data = enc.finish();

    decoder dec(data);
    auto arr = dec.array();
    REQUIRE(static_cast<bool>(arr.next()) == true);
    REQUIRE(static_cast<bool>(arr.next()) == false);
    dec.finish();
}

TEST_CASE("dynamic_encoder: add_null and add_undef", "[dynamic_encoder][primitives]") {
    dynamic_encoder enc;
    enc.open_array();
    enc.add_null();
    enc.add_undef();
    enc.close_array();
    auto data = enc.finish();
    // Both are simple values, check it decodes
    decoder dec(data);
    auto arr = dec.array();
    // Null and undef can't be auto-converted; just check it's valid CBOR
    REQUIRE(!arr.done());
    arr.next();
    REQUIRE(!arr.done());
    arr.next();
    dec.finish();
}

TEST_CASE("dynamic_encoder: add_tag", "[dynamic_encoder][primitives]") {
    dynamic_encoder enc;
    enc.add_tag(24);  // CBOR_TAG_CBOR (wrapped CBOR)
    enc.add_text("hello");
    auto data = enc.finish();
    REQUIRE(data.size() > 0);
}

TEST_CASE("dynamic_encoder: finish twice throws", "[dynamic_encoder][primitives]") {
    dynamic_encoder enc;
    enc.open_array();
    enc.close_array();
    enc.finish();
    REQUIRE_THROWS_AS(enc.finish(), error);
}

TEST_CASE("dynamic_encoder: add_int64 range covers int32 boundaries", "[dynamic_encoder][primitives]") {
    dynamic_encoder enc;
    enc.open_array();
    enc.add_int64(0);
    enc.add_int64(23);          // single-byte positive
    enc.add_int64(24);          // two-byte positive
    enc.add_int64(255);         // two-byte positive
    enc.add_int64(256);         // two-byte positive
    enc.add_int64(65535);       // two-byte positive
    enc.add_int64(65536);       // four-byte positive
    enc.add_int64(-1);          // single-byte negative
    enc.add_int64(-24);         // single-byte negative
    enc.add_int64(-25);         // two-byte negative
    enc.add_int64(-65536);      // four-byte negative
    enc.close_array();
    auto data = enc.finish();

    decoder dec(data);
    auto arr = dec.array();
    REQUIRE(int64_t(arr.next()) == 0);
    REQUIRE(int64_t(arr.next()) == 23);
    REQUIRE(int64_t(arr.next()) == 24);
    REQUIRE(int64_t(arr.next()) == 255);
    REQUIRE(int64_t(arr.next()) == 256);
    REQUIRE(int64_t(arr.next()) == 65535);
    REQUIRE(int64_t(arr.next()) == 65536);
    REQUIRE(int64_t(arr.next()) == -1);
    REQUIRE(int64_t(arr.next()) == -24);
    REQUIRE(int64_t(arr.next()) == -25);
    REQUIRE(int64_t(arr.next()) == -65536);
    dec.finish();
}
