/*
 * tests/test_decoder_finish.cpp
 *
 * Tests for decoder::finish() and decoder::finish_or_throw().
 * Covers success, extra bytes, and empty-input error cases.
 *
 * Tag: [decoder_finish]
 */
#include <catch2/catch_test_macros.hpp>
#include <qcborpp/qcborpp.hpp>
#include <cstdint>
using namespace qcborpp;

#define BUF byte_span{buf, sizeof(buf)}

// ===== finish() — success path =====

TEST_CASE("finish: success after consuming root int via map", "[decoder_finish]") {
    uint8_t buf[64];
    encoder enc(BUF);
    {
        auto m = enc.map();
        m["val"] = 42;
    }
    auto data = enc.finish();

    decoder dec(data);
    {
        auto m = dec.map();
        CHECK(m["val"].get_or(0) == 42);
    }
    REQUIRE_FALSE(dec.finish());
}

TEST_CASE("finish: empty input produces no error on finish", "[decoder_finish]") {
    // QCBOR treats empty input as no-error (nothing to check).
    decoder dec(const_byte_span{nullptr, 0});
    auto ec = dec.finish();
    CHECK_FALSE(ec); // QCBOR_SUCCESS
}

TEST_CASE("finish_or_throw: empty input does not throw", "[decoder_finish]") {
    decoder dec(const_byte_span{nullptr, 0});
    CHECK_NOTHROW(dec.finish_or_throw());
}

// ===== finish() — extra bytes =====

TEST_CASE("finish: extra bytes after root int via manual wire", "[decoder_finish]") {
    // Manual wire: two complete CBOR items in sequence: 0x01, 0x01
    uint8_t raw[] = {0x01, 0x01};
    decoder dec(const_byte_span{raw, sizeof(raw)});
    {
        auto item = dec.get_next();
        CHECK(item.type == cbor_type::int64);
        CHECK(item.value.int64_val == 1);
    }
    auto ec = dec.finish();
    CHECK(ec);
}

TEST_CASE("finish: extra bytes after root map", "[decoder_finish]") {
    // Wire: A1 61 61 01    ({"a": 1})
    // Followed by extra byte: 02
    uint8_t raw[] = {0xA1, 0x61, 0x61, 0x01, 0x02};
    decoder dec(const_byte_span{raw, sizeof(raw)});
    {
        auto m = dec.map();
        CHECK(m["a"].get_or(0) == 1);
    }
    auto ec = dec.finish();
    CHECK(ec);
}

// ===== finish_or_throw() — success =====

TEST_CASE("finish_or_throw: success with map", "[decoder_finish]") {
    uint8_t buf[64];
    encoder enc(BUF);
    {
        auto m = enc.map();
        m["key"] = "value";
    }
    auto data = enc.finish();

    decoder dec(data);
    {
        auto m = dec.map();
        CHECK(std::string_view{m["key"]} == "value");
    }
    CHECK_NOTHROW(dec.finish_or_throw());
}

TEST_CASE("finish_or_throw: success with array content", "[decoder_finish]") {
    uint8_t buf[64];
    encoder enc(BUF);
    enc.open_array();
    enc.add_int64(1);
    enc.add_int64(2);
    enc.add_int64(3);
    enc.close_array();
    auto data = enc.finish();

    decoder dec(data);
    {
        auto arr = dec.array();
        auto item = dec.get_next();
        CHECK(item.type == cbor_type::int64);
        item = dec.get_next();
        CHECK(item.type == cbor_type::int64);
        item = dec.get_next();
        CHECK(item.type == cbor_type::int64);
    }
    CHECK_NOTHROW(dec.finish_or_throw());
}

// ===== finish_or_throw() — throws on error =====

TEST_CASE("finish_or_throw: throws on extra bytes", "[decoder_finish]") {
    uint8_t raw[] = {0x01, 0x01};
    decoder dec(const_byte_span{raw, sizeof(raw)});
    {
        auto item = dec.get_next();
        CHECK(item.type == cbor_type::int64);
    }
    CHECK_THROWS_AS(dec.finish_or_throw(), error);
}

// ===== finish() returns specific errc =====

TEST_CASE("finish: error_code value on extra bytes", "[decoder_finish]") {
    uint8_t raw[] = {0x01, 0x01};
    decoder dec(const_byte_span{raw, sizeof(raw)});
    {
        auto item = dec.get_next();
        CHECK(item.type == cbor_type::int64);
    }
    auto ec = dec.finish();
    REQUIRE(ec);
    CHECK(ec == errc::extra_bytes);
}
