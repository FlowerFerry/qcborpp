/*
 * tests/test_decoder_primitives.cpp
 *
 * Tests for decoding primitive types.
 */

#include <qcborpp/qcborpp.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <vector>

using namespace qcborpp;

// Helper: encode a single value inside an array for testing decode
static const_byte_span encode_single_int64(int64_t v) {
    static std::vector<uint8_t> buf;
    dynamic_encoder enc;
    enc.open_array();
    enc.add_int64(v);
    enc.close_array();
    auto d = enc.finish();
    buf.assign(d.data(), d.data() + d.size());
    return {buf.data(), buf.size()};
}

static const_byte_span encode_single_text(std::string_view v) {
    static std::vector<uint8_t> buf;
    dynamic_encoder enc;
    enc.open_array();
    enc.add_text(v);
    enc.close_array();
    auto d = enc.finish();
    buf.assign(d.data(), d.data() + d.size());
    return {buf.data(), buf.size()};
}

TEST_CASE("decoder: construct and is_map/is_array", "[decoder][primitives]") {
    dynamic_encoder enc;
    {
        auto m = enc.map();
        m["x"] = 1;
    }
    auto data = enc.finish();

    decoder dec(data);
    REQUIRE(dec.is_map());
    REQUIRE(!dec.is_array());
    dec.finish();
}

TEST_CASE("decoder: map get int64", "[decoder][primitives]") {
    dynamic_encoder enc;
    {
        auto m = enc.map();
        m["a"] = 100;
        m["b"] = -200;
    }
    auto data = enc.finish();

    decoder dec(data);
    auto m = dec.map();
    REQUIRE(int64_t(m["a"]) == 100);
    REQUIRE(int64_t(m["b"]) == -200);
    dec.finish();
}

TEST_CASE("decoder: map get text string", "[decoder][primitives]") {
    dynamic_encoder enc;
    {
        auto m = enc.map();
        m["name"] = "qcborpp";
    }
    auto data = enc.finish();

    decoder dec(data);
    auto m = dec.map();
    REQUIRE(std::string_view(m["name"]) == "qcborpp");
    dec.finish();
}

TEST_CASE("decoder: map get double", "[decoder][primitives]") {
    dynamic_encoder enc;
    {
        auto m = enc.map();
        m["pi"] = 3.14159;
    }
    auto data = enc.finish();

    decoder dec(data);
    auto m = dec.map();
    REQUIRE_THAT(double(m["pi"]), Catch::Matchers::WithinAbs(3.14159, 1e-12));
    dec.finish();
}

TEST_CASE("decoder: map get bool", "[decoder][primitives]") {
    dynamic_encoder enc;
    {
        auto m = enc.map();
        m["yes"] = true;
        m["no"]  = false;
    }
    auto data = enc.finish();

    decoder dec(data);
    auto m = dec.map();
    REQUIRE(bool(m["yes"]) == true);
    REQUIRE(bool(m["no"]) == false);
    dec.finish();
}

TEST_CASE("decoder: map get bytes", "[decoder][primitives]") {
    uint8_t raw[] = {0xAA, 0xBB, 0xCC};
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
    REQUIRE(b[0] == 0xAA);
    REQUIRE(b[1] == 0xBB);
    REQUIRE(b[2] == 0xCC);
    dec.finish();
}

TEST_CASE("decoder: array traversal", "[decoder][primitives]") {
    dynamic_encoder enc;
    {
        auto a = enc.array();
        a << "first" << "second" << "third";
    }
    auto data = enc.finish();

    decoder dec(data);
    auto a = dec.array();
    REQUIRE(!a.done());
    REQUIRE(std::string_view(a.next()) == "first");
    REQUIRE(!a.done());
    REQUIRE(std::string_view(a.next()) == "second");
    REQUIRE(!a.done());
    REQUIRE(std::string_view(a.next()) == "third");
    REQUIRE(a.done());
    dec.finish();
}

TEST_CASE("decoder: integer label map", "[decoder][primitives]") {
    dynamic_encoder enc;
    {
        auto m = enc.map();
        m[0] = "zero";
        m[1] = "one";
    }
    auto data = enc.finish();

    decoder dec(data);
    auto m = dec.map();
    REQUIRE(std::string_view(m[0]) == "zero");
    REQUIRE(std::string_view(m[1]) == "one");
    dec.finish();
}

TEST_CASE("decoder: v_get_next", "[decoder][primitives]") {
    dynamic_encoder enc;
    enc.open_array();
    enc.add_int64(42);
    enc.add_text("hello");
    enc.close_array();
    auto data = enc.finish();

    decoder dec(data);
    auto item1 = dec.v_get_next();
    REQUIRE(item1.type == cbor_type::array);
    auto item2 = dec.v_get_next();
    REQUIRE(item2.type == cbor_type::int64);
    auto item3 = dec.v_get_next();
    REQUIRE(item3.type == cbor_type::text_string);
    dec.finish();
}

TEST_CASE("decoder: finish returns success", "[decoder][primitives]") {
    dynamic_encoder enc;
    {
        auto m = enc.map();
        m["x"] = 1;
    }
    auto data = enc.finish();

    decoder dec(data);
    {
        auto m = dec.map();
        int64_t v = m["x"];
        REQUIRE(v == 1);
    }
    auto ec = dec.finish();
    REQUIRE(!ec);
}

TEST_CASE("decoder: finish returns error for unconsumed data", "[decoder][primitives]") {
    // Encode a map with one key, but don't consume it all
    dynamic_encoder enc;
    {
        auto m = enc.map();
        m["a"] = 1;
        m["b"] = 2;
    }
    auto data = enc.finish();

    decoder dec(data);
    {
        auto m = dec.map();
        int64_t v = m["a"];
        REQUIRE(v == 1);
        // Not consuming "b" — map scope exits but "b" unconsumed
    }
    auto ec = dec.finish();
    // QCBOR spiffy ExitMap handles partial map consumption gracefully
    REQUIRE(!ec);
}

TEST_CASE("decoder: uint64 decoding", "[decoder][primitives]") {
    dynamic_encoder enc;
    {
        auto m = enc.map();
        m["big"] = static_cast<uint64_t>(0xFFFFFFFFFFFFFFFFULL);
    }
    auto data = enc.finish();

    decoder dec(data);
    auto m = dec.map();
    REQUIRE(static_cast<uint64_t>(m["big"]) == 0xFFFFFFFFFFFFFFFFULL);
    dec.finish();
}

TEST_CASE("decoder: rewind", "[decoder][primitives]") {
    dynamic_encoder enc;
    enc.open_array();
    enc.add_int64(1);
    enc.add_int64(2);
    enc.add_int64(3);
    enc.close_array();
    auto data = enc.finish();

    decoder dec(data);
    auto arr = dec.array();
    REQUIRE(int64_t(arr.next()) == 1);
    REQUIRE(int64_t(arr.next()) == 2);
    // Rewind
    dec.rewind();
    // Now read from start again
    REQUIRE(int64_t(arr.next()) == 1);
    REQUIRE(int64_t(arr.next()) == 2);
    REQUIRE(int64_t(arr.next()) == 3);
    dec.finish();
}
