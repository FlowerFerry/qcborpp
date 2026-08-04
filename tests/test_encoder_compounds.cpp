/*
 * tests/test_encoder_compounds.cpp
 *
 * Tests for encoding compound types: map, array, auto,
 * auto, auto, nested structures.
 */

#include <qcborpp/qcborpp.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace qcborpp;

TEST_CASE("dynamic_encoder: empty map", "[dynamic_encoder][compounds]") {
    dynamic_encoder enc;
    {
        auto m = enc.map();
    }
    auto data = enc.finish();
    REQUIRE(data.size() > 0);

    decoder dec(data);
    REQUIRE(dec.is_map());
    auto m = dec.map();
    (void)m;
    dec.finish();
}

TEST_CASE("dynamic_encoder: empty array", "[dynamic_encoder][compounds]") {
    dynamic_encoder enc;
    {
        auto a = enc.array();
    }
    auto data = enc.finish();
    REQUIRE(data.size() > 0);

    decoder dec(data);
    REQUIRE(dec.is_array());
    auto a = dec.array();
    REQUIRE(a.done());
    dec.finish();
}

TEST_CASE("dynamic_encoder: map with scalar values", "[dynamic_encoder][compounds]") {
    dynamic_encoder enc;
    {
        auto m = enc.map();
        m["int_pos"]   = 42;
        m["int_neg"]   = -10;
        m["text"]      = "hello";
        m["double"]    = 3.14;
        m["boolean"]   = true;
        m["null_val"]  = nullptr;
    }
    auto data = enc.finish();

    decoder dec(data);
    auto m = dec.map();
    REQUIRE(int64_t(m["int_pos"]) == 42);
    REQUIRE(int64_t(m["int_neg"]) == -10);
    REQUIRE(std::string_view(m["text"]) == "hello");
    REQUIRE_THAT(double(m["double"]), Catch::Matchers::WithinAbs(3.14, 1e-5));
    REQUIRE(bool(m["boolean"]) == true);
    REQUIRE(m["null_val"].is_null());
    dec.finish();
}

TEST_CASE("dynamic_encoder: nested map inside map", "[dynamic_encoder][compounds]") {
    dynamic_encoder enc;
    {
        auto m = enc.map();
        m["outer"] = 1;
        {
            auto inner = m["config"].map();
            inner["host"] = "localhost";
            inner["port"] = 8080;
        }
        m["after"] = 2;
    }
    auto data = enc.finish();

    decoder dec(data);
    auto m = dec.map();
    REQUIRE(int64_t(m["outer"]) == 1);
    {
        auto inner = m["config"].as_map();
        REQUIRE(std::string_view(inner["host"]) == "localhost");
        REQUIRE(int64_t(inner["port"]) == 8080);
    }
    REQUIRE(int64_t(m["after"]) == 2);
    dec.finish();
}

TEST_CASE("dynamic_encoder: array of scalars", "[dynamic_encoder][compounds]") {
    dynamic_encoder enc;
    {
        auto a = enc.array();
        a << 1 << 2 << 3 << 4 << 5;
    }
    auto data = enc.finish();

    decoder dec(data);
    auto a = dec.array();
    REQUIRE(int64_t(a.next()) == 1);
    REQUIRE(int64_t(a.next()) == 2);
    REQUIRE(int64_t(a.next()) == 3);
    REQUIRE(int64_t(a.next()) == 4);
    REQUIRE(int64_t(a.next()) == 5);
    dec.finish();
}

TEST_CASE("dynamic_encoder: array add methods", "[dynamic_encoder][compounds]") {
    dynamic_encoder enc;
    {
        auto a = enc.array();
        a.add(1);
        a.add(std::string_view("text"));
        a.add(3.5);
        a.add(true);
        a.add(nullptr);
    }
    auto data = enc.finish();

    decoder dec(data);
    auto a = dec.array();
    REQUIRE(int64_t(a.next()) == 1);
    REQUIRE(std::string_view(a.next()) == "text");
    REQUIRE_THAT(double(a.next()), Catch::Matchers::WithinAbs(3.5, 1e-6));
    REQUIRE(bool(a.next()) == true);
    REQUIRE(a.next().is_null());
    dec.finish();
}

TEST_CASE("dynamic_encoder: nested array in map", "[dynamic_encoder][compounds]") {
    dynamic_encoder enc;
    {
        auto m = enc.map();
        m["name"] = "test";
        {
            auto a = m["values"].array();
            a << 10.0 << 20.0 << 30.0;
        }
    }
    auto data = enc.finish();

    decoder dec(data);
    auto m = dec.map();
    REQUIRE(std::string_view(m["name"]) == "test");
    auto a = m["values"].as_array();
    REQUIRE_THAT(double(a.next()), Catch::Matchers::WithinAbs(10.0, 1e-12));
    REQUIRE_THAT(double(a.next()), Catch::Matchers::WithinAbs(20.0, 1e-12));
    REQUIRE_THAT(double(a.next()), Catch::Matchers::WithinAbs(30.0, 1e-12));
    dec.finish();
}

TEST_CASE("dynamic_encoder: implicit nested key (chained operator[])", "[dynamic_encoder][compounds]") {
    dynamic_encoder enc;
    {
        auto m = enc.map();
        {
            auto http = m["http"].map();
            http["host"] = "example.com";
            http["port"] = 443;
        }
    }
    auto data = enc.finish();

    decoder dec(data);
    auto m = dec.map();
    {
        auto http = m["http"].as_map();
        REQUIRE(std::string_view(http["host"]) == "example.com");
        REQUIRE(int64_t(http["port"]) == 443);
    }
    dec.finish();
}

TEST_CASE("dynamic_encoder: integer key labels", "[dynamic_encoder][compounds]") {
    dynamic_encoder enc;
    {
        auto m = enc.map();
        m[1] = "one";
        m[2] = "two";
        m[3] = "three";
    }
    auto data = enc.finish();

    decoder dec(data);
    auto m = dec.map();
    REQUIRE(std::string_view(m[1]) == "one");
    REQUIRE(std::string_view(m[2]) == "two");
    REQUIRE(std::string_view(m[3]) == "three");
    dec.finish();
}

TEST_CASE("dynamic_encoder: is_map / is_array", "[dynamic_encoder][compounds]") {
    dynamic_encoder enc_map;
    enc_map.map();
    REQUIRE(enc_map.is_map());
    REQUIRE(!enc_map.is_array());
    enc_map.finish();

    dynamic_encoder enc_arr;
    enc_arr.array();
    REQUIRE(enc_arr.is_array());
    REQUIRE(!enc_arr.is_map());
    enc_arr.finish();
}

TEST_CASE("dynamic_encoder: map double-open throws", "[dynamic_encoder][compounds]") {
    dynamic_encoder enc;
    enc.map();
    REQUIRE_THROWS_AS(enc.map(), error);
    REQUIRE_THROWS_AS(enc.array(), error);
}

TEST_CASE("dynamic_encoder: array double-open throws", "[dynamic_encoder][compounds]") {
    dynamic_encoder enc;
    enc.array();
    REQUIRE_THROWS_AS(enc.array(), error);
    REQUIRE_THROWS_AS(enc.map(), error);
}

TEST_CASE("dynamic_encoder: deep nesting", "[dynamic_encoder][compounds]") {
    dynamic_encoder enc;
    {
        auto m = enc.map();
        {
            auto a = m["level1"].array();
            {
                auto m2 = a.add_map();
                {
                    auto a2 = m2["level3"].array();
                    a2 << "a" << "b" << "c";
                }
            }
        }
    }
    auto data = enc.finish();
    REQUIRE(data.size() > 0);

    decoder dec(data);
    auto m = dec.map();
    {
        auto a = m["level1"].as_array();
        {
            auto m2 = a.next().as_map();
            {
                auto a2 = m2["level3"].as_array();
                REQUIRE(std::string_view(a2.next()) == "a");
                REQUIRE(std::string_view(a2.next()) == "b");
                REQUIRE(std::string_view(a2.next()) == "c");
            }
        }
    }
    dec.finish();
}

TEST_CASE("dynamic_encoder: map with uint64 values", "[dynamic_encoder][compounds]") {
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

TEST_CASE("dynamic_encoder: bytes in map", "[dynamic_encoder][compounds]") {
    uint8_t raw[] = {0xDE, 0xAD, 0xBE, 0xEF};
    dynamic_encoder enc;
    {
        auto m = enc.map();
        m["magic"] = const_byte_span{raw, 4};
    }
    auto data = enc.finish();

    decoder dec(data);
    auto m = dec.map();
    auto b = const_byte_span(m["magic"]);
    REQUIRE(b.size() == 4);
    REQUIRE(b[0] == 0xDE);
    REQUIRE(b[3] == 0xEF);
    dec.finish();
}

// ===== merge — map_builder runtime merge =====

TEST_CASE("encoder: merge single key-value pair (chaining)", "[encoder][merge]") {
    uint8_t buf[256];
    encoder enc(byte_span{buf, sizeof(buf)});
    {
        auto m = enc.map();
        m["base"] = "original";
        m.merge("count", 42).merge("name", std::string_view{"Niels"}).merge("active", true);
    }
    auto data = enc.finish();

    decoder dec(data);
    auto m = dec.map();
    CHECK(std::string_view{m["base"]} == "original");
    CHECK(m["count"].get_or(0) == 42);
    CHECK(std::string_view{m["name"]} == "Niels");
    CHECK(m["active"].get_or(false) == true);
}

TEST_CASE("encoder: merge dynamic_encoder", "[dynamic_encoder][merge]") {
    dynamic_encoder enc;
    {
        auto m = enc.map();
        m.merge("version", 1.0);
        m.merge("flag", false);
    }
    auto data = enc.finish();

    decoder dec(data);
    auto m = dec.map();
    CHECK(std::abs(m["version"].get_or(0.0) - 1.0) < 0.001);
    CHECK(m["flag"].get_or(true) == false);
}
