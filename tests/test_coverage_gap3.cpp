/*
 * tests/test_coverage_gap3.cpp
 *
 * Coverage gap fill (round 3): final batch of uncovered public APIs.
 * Run with: qcborpp_tests "[coverage3]"
 */

#include <qcborpp/qcborpp.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace qcborpp;

// ============================================================================
// dynamic_encoder: add_double_no_preferred
// ============================================================================

TEST_CASE("dynamic_encoder: add_double_no_preferred", "[dynamic_encoder][coverage3]") {
    dynamic_encoder enc;
    enc.open_array();
    enc.add_double_no_preferred(3.14159265358979);
    enc.close_array();
    auto data = enc.finish();
    REQUIRE(data.size() > 0);
}

// ============================================================================
// auto: map() and array() (nested inside map value)
// ============================================================================

TEST_CASE("auto: nested map via map()", "[dynamic_encoder][coverage3]") {
    dynamic_encoder enc;
    {
        auto m = enc.map();
        m["name"] = "root";
        {
            auto inner = m["child"].map();
            inner["age"] = 7;
        }
    }
    auto data = enc.finish();
    decoder dec(data);
    auto m = dec.map();
    REQUIRE(m["name"].as_string() == "root");
    auto child = m["child"].as_map();
    REQUIRE(int64_t(child["age"]) == 7);
    dec.finish();
}

TEST_CASE("auto: nested array via array()", "[dynamic_encoder][coverage3]") {
    dynamic_encoder enc;
    {
        auto m = enc.map();
        m["name"] = "list";
        {
            auto a = m["items"].array();
            a << 10 << 20 << 30;
        }
    }
    auto data = enc.finish();
    decoder dec(data);
    auto m = dec.map();
    auto a = m["items"].as_array();
    REQUIRE(int64_t(a.next()) == 10);
    REQUIRE(int64_t(a.next()) == 20);
    REQUIRE(int64_t(a.next()) == 30);
    dec.finish();
}

// ============================================================================
// auto: operator[](int)
// ============================================================================

TEST_CASE("auto: operator[] with int key", "[dynamic_encoder][coverage3]") {
    dynamic_encoder enc;
    {
        auto m = enc.map();
        int k = 42;
        m[k] = "answer";
    }
    auto data = enc.finish();
    decoder dec(data);
    auto m = dec.map();
    REQUIRE(m[42].as_string() == "answer");
    dec.finish();
}

// ============================================================================
// auto: operator<<(unsigned int)
// ============================================================================

TEST_CASE("auto: operator<< unsigned int", "[dynamic_encoder][coverage3]") {
    dynamic_encoder enc;
    {
        auto a = enc.array();
        a << 42u;
    }
    auto data = enc.finish();
    decoder dec(data);
    auto a = dec.array();
    REQUIRE(int64_t(a.next()) == 42);
    dec.finish();
}

// ============================================================================
// auto: operator<<(const_byte_span)
// ============================================================================

TEST_CASE("auto: operator<< const_byte_span", "[dynamic_encoder][coverage3]") {
    dynamic_encoder enc;
    uint8_t raw[] = {0x00, 0xFF, 0xAB};
    {
        auto a = enc.array();
        a << const_byte_span{raw, sizeof(raw)};
    }
    auto data = enc.finish();
    decoder dec(data);
    auto a = dec.array();
    auto bs = a.next().as_bytes();
    REQUIRE(bs.size() == 3);
    REQUIRE(bs[0] == 0x00);
    REQUIRE(bs[1] == 0xFF);
    REQUIRE(bs[2] == 0xAB);
    dec.finish();
}

// ============================================================================
// auto: add_map()
// ============================================================================

TEST_CASE("auto: add_map nested in array", "[dynamic_encoder][coverage3]") {
    dynamic_encoder enc;
    {
        auto a = enc.array();
        a << 1;
        {
            auto m = a.add_map();
            m["k"] = "v";
        }
        a << 2;
    }
    auto data = enc.finish();
    decoder dec(data);
    auto a = dec.array();
    REQUIRE(int64_t(a.next()) == 1);
    {
        auto m = a.next().as_map();
        REQUIRE(m["k"].as_string() == "v");
    }
    REQUIRE(int64_t(a.next()) == 2);
    dec.finish();
}

// ============================================================================
// decoder: raw_ctx() (const and non-const)
// ============================================================================

TEST_CASE("decoder: raw_ctx", "[decoder][coverage3]") {
    dynamic_encoder enc;
    enc.open_array();
    enc.add_int64(1);
    enc.close_array();
    auto data = enc.finish();

    decoder dec(data);
    auto* ctx = dec.raw_ctx();
    REQUIRE(ctx != nullptr);

    const decoder& cdec = dec;
    const auto* cctx = cdec.raw_ctx();
    REQUIRE(cctx != nullptr);

    dec.finish();
}

// ============================================================================
// map_scope: operator[](int)
// ============================================================================

TEST_CASE("map_scope: operator[] with int key", "[decoder][coverage3]") {
    dynamic_encoder enc;
    {
        auto m = enc.map();
        m[100] = "hundred";
    }
    auto data = enc.finish();
    decoder dec(data);
    auto m = dec.map();
    int key = 100;
    REQUIRE(m[key].as_string() == "hundred");
    dec.finish();
}

// ============================================================================
// item_proxy: operator const_byte_span() (implicit conversion)
// ============================================================================

TEST_CASE("item_proxy: implicit const_byte_span", "[decoder][coverage3]") {
    uint8_t raw[] = {0x01, 0x02, 0x03, 0x04};
    dynamic_encoder enc;
    {
        auto m = enc.map();
        const_byte_span bs{raw, sizeof(raw)};
        m["data"] = bs;
    }
    auto encoded = enc.finish();

    decoder dec(encoded);
    auto m = dec.map();
    // Use implicit conversion via static_cast<const_byte_span>
    const_byte_span result = static_cast<const_byte_span>(m["data"]);
    REQUIRE(result.size() == sizeof(raw));
    REQUIRE(result[0] == 0x01);
    REQUIRE(result[3] == 0x04);
    dec.finish();
}

// ============================================================================
// item_proxy: operator[](int)
// ============================================================================

TEST_CASE("item_proxy: operator[] with int subscript", "[decoder][coverage3]") {
    dynamic_encoder enc;
    {
        auto m = enc.map();
        {
            auto inner = m["outer"].map();
            inner[99] = "ninety-nine";
        }
    }
    auto data = enc.finish();
    decoder dec(data);
    auto m = dec.map();
    auto outer = m["outer"].as_map();
    int key = 99;
    REQUIRE(outer[key].as_string() == "ninety-nine");
    dec.finish();
}
