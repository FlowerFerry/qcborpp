/*
 * tests/test_roundtrip.cpp
 *
 * End-to-end roundtrip tests: encode → decode → verify.
 */

#include <qcborpp/qcborpp.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <vector>
#include <string>
#include <cmath>

using namespace qcborpp;

TEST_CASE("roundtrip: simple scalars in map", "[roundtrip]") {
    dynamic_encoder enc;
    {
        auto m = enc.map();
        m["int_val"]    = -42;
        m["uint_val"]   = static_cast<uint64_t>(100);
        m["text_val"]   = "hello world";
        m["double_val"] = 3.14159265358979;
        m["bool_val"]   = true;
    }
    auto data = enc.finish();

    decoder dec(data);
    auto m = dec.map();
    REQUIRE(int64_t(m["int_val"]) == -42);
    REQUIRE(static_cast<uint64_t>(m["uint_val"]) == 100);
    REQUIRE(std::string_view(m["text_val"]) == "hello world");
    {
        double val = double(m["double_val"]);
        REQUIRE_THAT(val, Catch::Matchers::WithinAbs(3.14159265358979, 1e-12));
    }
    REQUIRE(bool(m["bool_val"]) == true);
    dec.finish();
}

TEST_CASE("roundtrip: large integers", "[roundtrip]") {
    dynamic_encoder enc;
    {
        auto a = enc.array();
        a << INT64_MAX << INT64_MIN;
    }
    auto data = enc.finish();

    decoder dec(data);
    auto a = dec.array();
    REQUIRE(int64_t(a.next()) == INT64_MAX);
    REQUIRE(int64_t(a.next()) == INT64_MIN);
    dec.finish();
}

TEST_CASE("roundtrip: text with special characters", "[roundtrip]") {
    std::string special = "hello\nworld\t\u2603";  // snowman emoji via UTF-8
    dynamic_encoder enc;
    {
        auto m = enc.map();
        m["text"] = special;
    }
    auto data = enc.finish();

    decoder dec(data);
    auto m = dec.map();
    REQUIRE(std::string_view(m["text"]) == special);
    dec.finish();
}

TEST_CASE("roundtrip: empty text", "[roundtrip]") {
    dynamic_encoder enc;
    {
        auto m = enc.map();
        m["empty"] = "";
    }
    auto data = enc.finish();

    decoder dec(data);
    auto m = dec.map();
    auto result = std::string_view(m["empty"]);
    REQUIRE(result.empty());
    dec.finish();
}

TEST_CASE("roundtrip: multiple levels of nesting", "[roundtrip]") {
    dynamic_encoder enc;
    {
        auto m = enc.map();
        {
            auto a = m["data"].array();
            {
                auto m2 = a.add_map();
                m2["name"] = "item1";
                m2["val"]  = 100;
            }
            {
                auto m2 = a.add_map();
                m2["name"] = "item2";
                m2["val"]  = 200;
            }
        }
    }
    auto data = enc.finish();

    decoder dec(data);
    auto m = dec.map();
    auto a = m["data"].as_array();

    {
        auto m2 = a.next().as_map();
        REQUIRE(std::string_view(m2["name"]) == "item1");
        REQUIRE(int64_t(m2["val"]) == 100);
    }

    {
        auto m3 = a.next().as_map();
        REQUIRE(std::string_view(m3["name"]) == "item2");
        REQUIRE(int64_t(m3["val"]) == 200);
    }

    dec.finish();
}

TEST_CASE("roundtrip: binary data", "[roundtrip]") {
    std::vector<uint8_t> original = {0, 1, 2, 3, 4, 5, 0xFF, 0xFE, 0xFD, 0x00};
    dynamic_encoder enc;
    {
        auto m = enc.map();
        m["raw"] = const_byte_span{original.data(), original.size()};
    }
    auto data = enc.finish();

    decoder dec(data);
    auto m = dec.map();
    auto result = const_byte_span(m["raw"]);
    REQUIRE(result.size() == original.size());
    for (size_t i = 0; i < original.size(); ++i)
        REQUIRE(result[i] == original[i]);
    dec.finish();
}

TEST_CASE("roundtrip: integer zero stays zero", "[roundtrip]") {
    dynamic_encoder enc;
    {
        auto m = enc.map();
        m["zero"] = 0;
    }
    auto data = enc.finish();

    decoder dec(data);
    auto m = dec.map();
    REQUIRE(int64_t(m["zero"]) == 0);
    dec.finish();
}

TEST_CASE("roundtrip: boolean roundtrip", "[roundtrip]") {
    dynamic_encoder enc;
    {
        auto m = enc.map();
        m["t"] = true;
        m["f"] = false;
    }
    auto data = enc.finish();

    decoder dec(data);
    auto m = dec.map();
    REQUIRE(bool(m["t"]));
    REQUIRE(!bool(m["f"]));
    dec.finish();
}

TEST_CASE("roundtrip: numeric edge values", "[roundtrip]") {
    dynamic_encoder enc;
    {
        auto a = enc.array();
        a << -1 << 0 << 1 << 23 << -24 << 24 << -25 << 255 << 256 << 65535 << 65536;
    }
    auto data = enc.finish();

    decoder dec(data);
    auto a = dec.array();
    REQUIRE(int64_t(a.next()) == -1);
    REQUIRE(int64_t(a.next()) == 0);
    REQUIRE(int64_t(a.next()) == 1);
    REQUIRE(int64_t(a.next()) == 23);
    REQUIRE(int64_t(a.next()) == -24);
    REQUIRE(int64_t(a.next()) == 24);
    REQUIRE(int64_t(a.next()) == -25);
    REQUIRE(int64_t(a.next()) == 255);
    REQUIRE(int64_t(a.next()) == 256);
    REQUIRE(int64_t(a.next()) == 65535);
    REQUIRE(int64_t(a.next()) == 65536);
    dec.finish();
}

TEST_CASE("roundtrip: dynamic_encoder reserve works", "[roundtrip]") {
    dynamic_encoder enc(1024);
    REQUIRE(enc.capacity() >= 1024);

    {
        auto m = enc.map();
        for (int i = 0; i < 20; ++i) {
            m[std::to_string(i)] = i * i;
        }
    }
    auto data = enc.finish();
    REQUIRE(data.size() > 0);

    decoder dec(data);
    auto m = dec.map();
    for (int i = 0; i < 20; ++i) {
        REQUIRE(int64_t(m[std::to_string(i)]) == i * i);
    }
    dec.finish();
}
