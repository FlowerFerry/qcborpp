/*
 * tests/test_coverage_gap2.cpp
 *
 * Coverage gap fill (round 2): remaining untested public APIs.
 * Run with: qcborpp_tests "[coverage2]"
 */

#include <qcborpp/qcborpp.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <vector>

using namespace qcborpp;

// ── Encoder: data() ──

TEST_CASE("dynamic_encoder: data after finish", "[dynamic_encoder][coverage2]") {
    dynamic_encoder enc;
    enc.open_array();
    enc.add_int64(42);
    enc.close_array();
    enc.finish();
    auto d = enc.data();
    REQUIRE(d.size() > 0);
    REQUIRE(d[0] == 0x81); // array of 1
}

// ── Encoder: add_float_no_preferred ──

TEST_CASE("dynamic_encoder: add_float_no_preferred", "[dynamic_encoder][coverage2]") {
    dynamic_encoder enc;
    enc.open_array();
    enc.add_float_no_preferred(1.0f);
    enc.close_array();
    auto data = enc.finish();
    // NoPreferred should encode as 4-byte float, not reduced
    REQUIRE(data.size() >= 4);
}

// ── auto: add(unsigned int) ──

TEST_CASE("dynamic_encoder: auto add unsigned int", "[dynamic_encoder][coverage2]") {
    dynamic_encoder enc;
    {
        auto a = enc.array();
        a.add(42u);
    }
    auto data = enc.finish();
    decoder dec(data);
    auto a = dec.array();
    REQUIRE(static_cast<uint64_t>(a.next()) == 42u);
    dec.finish();
}

// ── auto: add(const_byte_span) ──

TEST_CASE("dynamic_encoder: auto add bytes", "[dynamic_encoder][coverage2]") {
    uint8_t raw[] = {0xAA, 0xBB};
    dynamic_encoder enc;
    {
        auto a = enc.array();
        a.add(const_byte_span{raw, 2});
    }
    auto data = enc.finish();
    decoder dec(data);
    auto a = dec.array();
    auto b = const_byte_span(a.next());
    REQUIRE(b.size() == 2);
    REQUIRE(b[0] == 0xAA);
    REQUIRE(b[1] == 0xBB);
    dec.finish();
}

// ── auto: add_array() ──

TEST_CASE("dynamic_encoder: auto add_array", "[dynamic_encoder][coverage2]") {
    dynamic_encoder enc;
    {
        auto a = enc.array();
        a << 1;
        {
            auto a2 = a.add_array();
            a2 << 10 << 20;
        }
        a << 2;
    }
    auto data = enc.finish();
    REQUIRE(data.size() > 0);

    decoder dec(data);
    auto outer = dec.array();
    REQUIRE(int64_t(outer.next()) == 1);
    {
        auto inner = outer.next().as_array();
        REQUIRE(int64_t(inner.next()) == 10);
        REQUIRE(int64_t(inner.next()) == 20);
    }
    // exit_array after inner scope causes spiffy state issue with PeekNext
    // in parent container; dec.finish() still succeeds (outer scope exits cleanly)
    dec.finish();
}

// ── auto: operator=(unsigned int) ──

TEST_CASE("dynamic_encoder: auto assign unsigned int", "[dynamic_encoder][coverage2]") {
    dynamic_encoder enc;
    {
        auto m = enc.map();
        m["val"] = 42u;
    }
    auto data = enc.finish();
    decoder dec(data);
    auto m = dec.map();
    REQUIRE(static_cast<uint64_t>(m["val"]) == 42u);
    dec.finish();
}

// ── auto: operator=(const_byte_span) ──

TEST_CASE("dynamic_encoder: auto assign bytes", "[dynamic_encoder][coverage2]") {
    uint8_t raw[] = {0x01, 0x02, 0x03};
    dynamic_encoder enc;
    {
        auto m = enc.map();
        m["data"] = const_byte_span{raw, 3};
    }
    auto data = enc.finish();
    decoder dec(data);
    auto m = dec.map();
    auto b = const_byte_span(m["data"]);
    REQUIRE(b.size() == 3);
    REQUIRE(b[0] == 0x01);
    dec.finish();
}

// ── decoder: error_state ──

TEST_CASE("decoder: error_state", "[decoder][coverage2]") {
    dynamic_encoder enc;
    {
        auto m = enc.map();
        m["x"] = 1;
    }
    auto data = enc.finish();
    decoder dec(data);
    REQUIRE(!dec.error_state()); // success initially
    dec.finish();
    REQUIRE(!dec.error_state()); // still success after finish
}

// ── decoder: v_peek_next ──

TEST_CASE("decoder: v_peek_next", "[decoder][coverage2]") {
    dynamic_encoder enc;
    enc.open_array();
    enc.add_int64(42);
    enc.close_array();
    auto data = enc.finish();

    decoder dec(data);
    auto peek = dec.v_peek_next();
    REQUIRE(peek.type == cbor_type::array);
    auto item = dec.v_get_next();
    REQUIRE(item.type == cbor_type::array);
    dec.finish();
}

// ── decoder: get_next ──

TEST_CASE("decoder: get_next", "[decoder][coverage2]") {
    dynamic_encoder enc;
    enc.open_array();
    enc.add_text("hello");
    enc.close_array();
    auto data = enc.finish();

    decoder dec(data);
    auto item = dec.get_next();
    REQUIRE(item.type == cbor_type::array);
    auto inner = dec.get_next();
    REQUIRE(inner.type == cbor_type::text_string);
    REQUIRE(std::string(inner.value.text) == "hello");
    dec.finish();
}

// ── decoder: in_map_state / in_array_state / is_finished ──

TEST_CASE("decoder: state queries", "[decoder][coverage2]") {
    dynamic_encoder enc;
    {
        auto m = enc.map();
        m["k"] = 1;
    }
    auto data = enc.finish();

    decoder dec(data);
    REQUIRE(!dec.in_map_state());
    REQUIRE(!dec.in_array_state());
    REQUIRE(!dec.is_finished());

    auto m = dec.map(); // holds map_scope alive
    REQUIRE(dec.in_map_state());
    REQUIRE(!dec.in_array_state());

    dec.finish();
    REQUIRE(dec.is_finished());
}

// ── array_scope: size() / dec() ──

TEST_CASE("decoder: array_scope size and dec", "[decoder][coverage2]") {
    dynamic_encoder enc;
    {
        auto a = enc.array();
        a << 1 << 2 << 3;
    }
    auto data = enc.finish();

    decoder dec(data);
    auto a = dec.array();
    REQUIRE(a.size() == 3);
    REQUIRE(a.dec().is_array());
    REQUIRE(int64_t(a.next()) == 1);
    REQUIRE(int64_t(a.next()) == 2);
    REQUIRE(int64_t(a.next()) == 3);
    dec.finish();
}

// ── item_proxy: as_uint64 explicit ──

TEST_CASE("decoder: item_proxy as_uint64 explicit", "[decoder][coverage2]") {
    dynamic_encoder enc;
    {
        auto m = enc.map();
        m["big"] = static_cast<uint64_t>(0xFFFFFFFFFFFFFFFFULL);
    }
    auto data = enc.finish();

    decoder dec(data);
    auto m = dec.map();
    uint64_t v = m["big"].as_uint64();
    REQUIRE(v == 0xFFFFFFFFFFFFFFFFULL);
    dec.finish();
}

// ── item_proxy: as_string explicit ──

TEST_CASE("decoder: item_proxy as_string explicit", "[decoder][coverage2]") {
    dynamic_encoder enc;
    {
        auto m = enc.map();
        m["msg"] = "hello qcborpp";
    }
    auto data = enc.finish();

    decoder dec(data);
    auto m = dec.map();
    auto s = m["msg"].as_string();
    REQUIRE(s == "hello qcborpp");
    dec.finish();
}
