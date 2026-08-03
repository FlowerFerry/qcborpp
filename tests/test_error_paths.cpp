/*
 * tests/test_error_paths.cpp
 *
 * Error-path and exception-behaviour tests — not roundtrip correctness
 * but defensive coverage: what happens when the API is misused?
 *
 * Coverage:
 *   - close without open (encoder)
 *   - finish twice / finish with map/array still open
 *   - truncated/damaged input to decoder
 *   - item_proxy type conversion behaviour (non-throwing QCBOR spiffy leniency)
 *   - missing map key
 *   - array_scope::next() beyond end
 *   - decoder double map()/array()
 *   - deep nesting
 *   - encoder: close mismatch (close_map when array open, etc.)
 *   - decoder unconsumed items
 *   - raw QCBOR error-code mapping
 */

#include <qcborpp/qcborpp.hpp>
#include "qcbor/qcbor_encode.h"
#include "qcbor/qcbor_decode.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace qcborpp;

// ══════════════════════════════════════════════════════════════════════════
// Encoder: close without open
// ══════════════════════════════════════════════════════════════════════════

TEST_CASE("error: static close_map without open_map", "[error_paths]") {
    uint8_t buf[256];
    encoder enc(byte_span{buf, sizeof(buf)});
    REQUIRE_THROWS_AS(enc.close_map(), error);
}

TEST_CASE("error: static close_array without open_array", "[error_paths]") {
    uint8_t buf[256];
    encoder enc(byte_span{buf, sizeof(buf)});
    REQUIRE_THROWS_AS(enc.close_array(), error);
}

TEST_CASE("error: static close_array when map open", "[error_paths]") {
    uint8_t buf[256];
    encoder enc(byte_span{buf, sizeof(buf)});
    enc.open_map();
    REQUIRE_THROWS_AS(enc.close_array(), error);
}

TEST_CASE("error: static close_map when array open", "[error_paths]") {
    uint8_t buf[256];
    encoder enc(byte_span{buf, sizeof(buf)});
    enc.open_array();
    REQUIRE_THROWS_AS(enc.close_map(), error);
}

TEST_CASE("error: static too many close_array", "[error_paths]") {
    uint8_t buf[256];
    encoder enc(byte_span{buf, sizeof(buf)});
    enc.open_array(); enc.close_array();
    REQUIRE_THROWS_AS(enc.close_array(), error);
}

// ══════════════════════════════════════════════════════════════════════════
// Encoder: finish twice
// ══════════════════════════════════════════════════════════════════════════

TEST_CASE("error: static finish twice throws", "[error_paths]") {
    uint8_t buf[256];
    encoder enc(byte_span{buf, sizeof(buf)});
    enc.open_array(); enc.close_array();
    enc.finish();
    REQUIRE_THROWS_AS(enc.finish(), error);
}

// ══════════════════════════════════════════════════════════════════════════
// Encoder: finish with open container
// ══════════════════════════════════════════════════════════════════════════

TEST_CASE("error: static finish with map still open", "[error_paths]") {
    uint8_t buf[256];
    encoder enc(byte_span{buf, sizeof(buf)});
    enc.open_map();
    enc.add_int64(1);
    // missing close_map → finish() throws array_or_map_still_open
    REQUIRE_THROWS_AS(enc.finish(), error);
}

TEST_CASE("error: dynamic finish with array still open", "[error_paths]") {
    dynamic_encoder enc;
    enc.open_array();
    enc.add_int64(1);
    REQUIRE_THROWS_AS(enc.finish(), error);
}

// ══════════════════════════════════════════════════════════════════════════
// Encoder: double map() / double array()
// ══════════════════════════════════════════════════════════════════════════

TEST_CASE("error: static double map() throws", "[error_paths]") {
    uint8_t buf[256];
    encoder enc(byte_span{buf, sizeof(buf)});
    enc.map();
    REQUIRE_THROWS_AS(enc.map(), error);
}

TEST_CASE("error: static double array() throws", "[error_paths]") {
    uint8_t buf[256];
    encoder enc(byte_span{buf, sizeof(buf)});
    enc.array();
    REQUIRE_THROWS_AS(enc.array(), error);
}

TEST_CASE("error: dynamic double map() throws", "[error_paths]") {
    dynamic_encoder enc;
    enc.map();
    REQUIRE_THROWS_AS(enc.map(), error);
}

TEST_CASE("error: dynamic double array() throws", "[error_paths]") {
    dynamic_encoder enc;
    enc.array();
    REQUIRE_THROWS_AS(enc.array(), error);
}

// ══════════════════════════════════════════════════════════════════════════
// Encoder: deep nesting
// ══════════════════════════════════════════════════════════════════════════

TEST_CASE("error: static deep nesting rejected", "[error_paths]") {
    // QCBOR_MAX_ARRAY_NESTING1 = 15, so 15 opens is OK,
    // but the 16th open exceeds the limit and must throw.
    uint8_t buf[16384];
    encoder enc(byte_span{buf, sizeof(buf)});
    for (int i = 0; i < 15; ++i) enc.open_array();
    bool caught = false;
    try {
        enc.open_array();
    } catch (const error& e) {
        REQUIRE(e.code() == errc::array_nesting_too_deep);
        caught = true;
    }
    REQUIRE(caught);
}

TEST_CASE("error: dynamic deep nesting rejected at finish", "[error_paths]") {
    // Dynamic encoder defers QCBOR calls to finish(); nesting overflow
    // surfaces only when finish() replays the recorded open_array() ops.
    dynamic_encoder denc;
    for (int i = 0; i < 15; ++i) denc.open_array();
    bool caught = false;
    try {
        denc.open_array();
        denc.finish();
    } catch (const error& e) {
        REQUIRE(e.code() == errc::array_nesting_too_deep);
        caught = true;
    }
    REQUIRE(caught);
}

// ══════════════════════════════════════════════════════════════════════════
// Decoder: empty / truncated / garbage input
// ══════════════════════════════════════════════════════════════════════════

TEST_CASE("error: decoder empty input", "[error_paths]") {
    uint8_t buf[1] = {};
    decoder d(const_byte_span{buf, 0});
    REQUIRE_THROWS_AS(d.map(), error);
}

TEST_CASE("error: decoder truncated int", "[error_paths]") {
    uint8_t buf[1] = {0x18};
    decoder dec(const_byte_span{buf, 1});
    REQUIRE_THROWS_AS(dec.array(), error);
}

TEST_CASE("error: decoder truncated string", "[error_paths]") {
    uint8_t buf[2] = {0x62, 0x41};
    decoder dec(const_byte_span{buf, 2});
    REQUIRE_THROWS_AS(dec.array(), error);
}

TEST_CASE("error: decoder garbage byte", "[error_paths]") {
    uint8_t buf[1] = {0xFF};
    decoder dec(const_byte_span{buf, 1});
    REQUIRE_THROWS_AS(dec.array(), error);
}

// ══════════════════════════════════════════════════════════════════════════
// Decoder: double map() / double array()
// ══════════════════════════════════════════════════════════════════════════

TEST_CASE("error: decoder double map() throws", "[error_paths]") {
    uint8_t buf[128];
    encoder enc(byte_span{buf, sizeof(buf)});
    {
        auto m = enc.map();
        m["a"] = 1;
    }
    auto data = enc.finish();

    decoder dec(data);
    dec.map();
    REQUIRE_THROWS_AS(dec.map(), error);
}

TEST_CASE("error: decoder double array() throws", "[error_paths]") {
    uint8_t buf[128];
    encoder enc(byte_span{buf, sizeof(buf)});
    enc.open_array(); enc.add_int64(1); enc.close_array();
    auto data = enc.finish();

    decoder dec(data);
    dec.array();
    REQUIRE_THROWS_AS(dec.array(), error);
}

// ══════════════════════════════════════════════════════════════════════════
// map_scope: missing key
// ══════════════════════════════════════════════════════════════════════════

TEST_CASE("error: map_scope missing key throws label_not_found", "[error_paths]") {
    uint8_t buf[128];
    encoder enc(byte_span{buf, sizeof(buf)});
    {
        auto m = enc.map();
        m["x"] = 1;
    }
    auto data = enc.finish();

    decoder dec(data);
    auto m = dec.map();
    REQUIRE_THROWS_AS(int64_t(m["missing"]), error);
}

// ══════════════════════════════════════════════════════════════════════════
// array_scope::next() beyond end
// ══════════════════════════════════════════════════════════════════════════

TEST_CASE("error: array_scope next beyond end", "[error_paths]") {
    uint8_t buf[128];
    encoder enc(byte_span{buf, sizeof(buf)});
    enc.open_array();
    enc.add_int64(1);
    enc.add_int64(2);
    enc.close_array();
    auto data = enc.finish();

    decoder dec(data);
    auto a = dec.array();
    REQUIRE(int64_t(a.next()) == 1);
    REQUIRE(int64_t(a.next()) == 2);
    REQUIRE_THROWS_AS(a.next(), error);
}

TEST_CASE("error: array_scope next on empty array", "[error_paths]") {
    uint8_t buf[128];
    encoder enc(byte_span{buf, sizeof(buf)});
    enc.open_array(); enc.close_array();
    auto data = enc.finish();

    decoder dec(data);
    auto a = dec.array();
    REQUIRE_THROWS_AS(a.next(), error);
}

// ══════════════════════════════════════════════════════════════════════════
// Encoder builder: using encoder after builder close
// ══════════════════════════════════════════════════════════════════════════

TEST_CASE("error: map_builder encoder map() after builder closes", "[error_paths]") {
    uint8_t buf[256];
    encoder enc(byte_span{buf, sizeof(buf)});
    {
        auto m = enc.map();
        m["a"] = 1;
    }
    // Builder closed, encoder's context is back to neutral.
    // Calling map() again should fail (top_set_ is true).
    REQUIRE_THROWS_AS(enc.map(), error);
}

// ══════════════════════════════════════════════════════════════════════════
// Encoder: close_array on already-finished encoder
// ══════════════════════════════════════════════════════════════════════════

TEST_CASE("error: static close_array after finish throws", "[error_paths]") {
    uint8_t buf[256];
    encoder enc(byte_span{buf, sizeof(buf)});
    enc.open_array();
    enc.add_int64(1);
    enc.close_array();
    enc.finish();
    REQUIRE_THROWS_AS(enc.close_array(), error);
}

// ══════════════════════════════════════════════════════════════════════════
// item_proxy: type query behaviour (QCBOR spiffy is lenient)
// ══════════════════════════════════════════════════════════════════════════
//
// QCBOR's spiffy decode functions often succeed on mismatched types
// (returning 0 or empty string).  These tests document the actual
// behaviour — they are NOT roundtrip guarantees.

TEST_CASE("error: item_proxy int is_int64 is_map is_array", "[error_paths]") {
    uint8_t buf[128];
    encoder enc(byte_span{buf, sizeof(buf)});
    enc.open_array(); enc.add_int64(42); enc.close_array();
    auto data = enc.finish();

    decoder dec(data);
    auto a = dec.array();
    auto item = a.next();
    REQUIRE(item.is_int64());
    REQUIRE(!item.is_map());
    REQUIRE(!item.is_array());
    REQUIRE(!item.is_null());
    REQUIRE(!item.is_string());
    REQUIRE(!item.is_bytes());
}

TEST_CASE("error: item_proxy map is_map is_array is_int64", "[error_paths]") {
    uint8_t buf[256];
    encoder enc(byte_span{buf, sizeof(buf)});
    {
        auto m = enc.map();
        m["x"] = 1;
    }
    auto data = enc.finish();

    decoder dec(data);
    auto m = dec.map();
    auto item = m["x"];
    REQUIRE(item.is_int64());
}

// ══════════════════════════════════════════════════════════════════════════
// Error code verification from raw QCBOR
// ══════════════════════════════════════════════════════════════════════════

TEST_CASE("error: raw QCBOR error code → qcborpp::errc", "[error_paths]") {
    // Verify the static_cast mapping by checking a few known values
    REQUIRE(static_cast<int>(errc::success) == 0);
    REQUIRE(static_cast<int>(errc::buffer_too_small) == 1);
    REQUIRE(static_cast<int>(errc::close_mismatch) == 5);
    REQUIRE(static_cast<int>(errc::too_many_closes) == 7);
    REQUIRE(static_cast<int>(errc::array_or_map_still_open) == 8);
    REQUIRE(static_cast<int>(errc::hit_end) == 31);
    REQUIRE(static_cast<int>(errc::unexpected_type) == 61);
    REQUIRE(static_cast<int>(errc::no_more_items) == 67);
    REQUIRE(static_cast<int>(errc::label_not_found) == 68);
}

TEST_CASE("error: success error_code comparison", "[error_paths]") {
    auto ec = make_error_code(errc::success);
    REQUIRE(!ec); // success is falsy
    REQUIRE(ec.value() == 0);
    REQUIRE(ec.category() == qcborpp_category());
}

// ══════════════════════════════════════════════════════════════════════════
// Decoder finish error_code for extra bytes at top level
// ══════════════════════════════════════════════════════════════════════════

TEST_CASE("error: decoder extra bytes after root item", "[error_paths]") {
    // Two top-level integers back-to-back: 0x01 (= 1) followed by 0x02 (= 2).
    // QCBOR rejects extra bytes after consuming the root item.
    uint8_t buf[] = {0x01, 0x02};
    decoder dec(const_byte_span{buf, sizeof(buf)});
    {
        auto item = dec.get_next();
        REQUIRE(item.type == cbor_type::int64);
        REQUIRE(item.value.int64_val == 1);
    }
    auto ec = dec.finish();
    REQUIRE(ec);
    REQUIRE(ec == make_error_code(errc::extra_bytes));
}
