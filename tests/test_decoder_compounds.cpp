/*
 * tests/test_decoder_compounds.cpp
 *
 * Tests for decoding compound types: nested maps, arrays,
 * as_map, as_array, map_scope, array_scope.
 */

#include <qcborpp/qcborpp.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace qcborpp;

TEST_CASE("decoder: nested map in map", "[decoder][compounds]") {
    dynamic_encoder enc;
    {
        auto m = enc.map();
        m["outer"] = "top";
        {
            auto inner = m["inner"].map();
            inner["key"] = "value";
        }
    }
    auto data = enc.finish();

    decoder dec(data);
    auto m = dec.map();
    REQUIRE(std::string_view(m["outer"]) == "top");
    auto inner = m["inner"].as_map();
    REQUIRE(std::string_view(inner["key"]) == "value");
    dec.finish();
}

TEST_CASE("decoder: array in map", "[decoder][compounds]") {
    dynamic_encoder enc;
    {
        auto m = enc.map();
        m["count"] = 3;
        {
            auto a = m["items"].array();
            a << 10 << 20 << 30;
        }
    }
    auto data = enc.finish();

    decoder dec(data);
    auto m = dec.map();
    REQUIRE(int64_t(m["count"]) == 3);
    auto a = m["items"].as_array();
    REQUIRE(int64_t(a.next()) == 10);
    REQUIRE(int64_t(a.next()) == 20);
    REQUIRE(int64_t(a.next()) == 30);
    dec.finish();
}

TEST_CASE("decoder: chained operator[] on nested maps", "[decoder][compounds]") {
    dynamic_encoder enc;
    {
        auto m = enc.map();
        m["a"]["b"]["c"] = "deep";
    }
    auto data = enc.finish();

    decoder dec(data);
    auto m = dec.map();
    REQUIRE(std::string_view(m["a"]["b"]["c"]) == "deep");
    dec.finish();
}

TEST_CASE("decoder: empty map", "[decoder][compounds]") {
    dynamic_encoder enc;
    { enc.map(); }
    auto data = enc.finish();

    decoder dec(data);
    {
        auto m = dec.map();
        // RAII: map_scope destructor calls exit_map
    }
    dec.finish();
}

TEST_CASE("decoder: empty array", "[decoder][compounds]") {
    dynamic_encoder enc;
    { enc.array(); }
    auto data = enc.finish();

    decoder dec(data);
    auto a = dec.array();
    REQUIRE(a.done());
    dec.finish();
}

TEST_CASE("decoder: array done detection", "[decoder][compounds]") {
    dynamic_encoder enc;
    {
        auto a = enc.array();
        a << 1;
    }
    auto data = enc.finish();

    decoder dec(data);
    auto a = dec.array();
    REQUIRE(!a.done());
    int64_t v = a.next();
    REQUIRE(v == 1);
    REQUIRE(a.done());
    dec.finish();
}

TEST_CASE("decoder: as_bool explicit", "[decoder][compounds]") {
    dynamic_encoder enc;
    {
        auto m = enc.map();
        m["a"] = true;
        m["b"] = false;
    }
    auto data = enc.finish();

    decoder dec(data);
    auto m = dec.map();
    REQUIRE(m["a"].as_bool() == true);
    REQUIRE(m["b"].as_bool() == false);
    dec.finish();
}

TEST_CASE("decoder: as_double explicit", "[decoder][compounds]") {
    dynamic_encoder enc;
    {
        auto m = enc.map();
        m["v"] = 2.5;
    }
    auto data = enc.finish();

    decoder dec(data);
    auto m = dec.map();
    REQUIRE_THAT(m["v"].as_double(), Catch::Matchers::WithinAbs(2.5, 1e-12));
    dec.finish();
}

TEST_CASE("decoder: as_bytes explicit", "[decoder][compounds]") {
    uint8_t raw[] = {0x01, 0x02, 0x03};
    dynamic_encoder enc;
    {
        auto m = enc.map();
        m["d"] = const_byte_span{raw, 3};
    }
    auto data = enc.finish();

    decoder dec(data);
    auto m = dec.map();
    auto b = m["d"].as_bytes();
    REQUIRE(b.size() == 3);
    REQUIRE(b[0] == 0x01);
    dec.finish();
}

TEST_CASE("decoder: tagged getters via item_proxy", "[decoder][compounds]") {
    // Encode a URI tag
    dynamic_encoder enc;
    {
        auto m = enc.map();
        enc.add_uri("https://example.com", true);
    }
    // Not easily testable via map scope; test via direct dynamic_encoder
    enc.finish();

    dynamic_encoder enc2;
    enc2.open_array();
    enc2.add_uri("https://example.org", true);
    enc2.close_array();
    auto data = enc2.finish();

    decoder dec(data);
    auto a = dec.array();
    auto uri = a.next().as_uri(tag_requirement::optional_tag);  // optional tag
    REQUIRE(uri == "https://example.org");
    dec.finish();
}

TEST_CASE("decoder: as_uuid", "[decoder][compounds]") {
    uint8_t uuid[16] = {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15};
    dynamic_encoder enc;
    enc.open_array();
    enc.add_binary_uuid({uuid, 16}, true);
    enc.close_array();
    auto data = enc.finish();

    decoder dec(data);
    auto a = dec.array();
    // as_uuid with optional tag requirement
    auto result = a.next().as_uuid(tag_requirement::optional_tag);
    REQUIRE(result.size() == 16);
    REQUIRE(result[0] == 0);
    REQUIRE(result[15] == 15);
    dec.finish();
}

TEST_CASE("decoder: deep nested map traversal", "[decoder][compounds]") {
    dynamic_encoder enc;
    {
        auto m = enc.map();
        m["l1"] = "layer1";
        {
            auto m2 = m["l2"].map();
            m2["l2a"] = 100;
            {
                auto m3 = m2["l3"].map();
                m3["l3a"] = "bottom";
                m3["l3b"] = 200;
            }
        }
        m["l1b"] = 999;
    }
    auto data = enc.finish();

    decoder dec(data);
    auto m = dec.map();

    REQUIRE(std::string_view(m["l1"]) == "layer1");
    REQUIRE(int64_t(m["l1b"]) == 999);

    auto m2 = m["l2"].as_map();
    REQUIRE(int64_t(m2["l2a"]) == 100);

    auto m3 = m2["l3"].as_map();
    REQUIRE(std::string_view(m3["l3a"]) == "bottom");
    REQUIRE(int64_t(m3["l3b"]) == 200);

    dec.finish();
}
