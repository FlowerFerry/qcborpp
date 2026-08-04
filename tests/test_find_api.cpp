/*
 * tests/test_find_api.cpp
 *
 * Tests for map_scope::find() — separates "key not found" from
 * "type mismatch" as recommended by I-07.
 *
 * Tag: [find_api]
 */
#include <catch2/catch_test_macros.hpp>
#include <qcborpp/qcborpp.hpp>
#include <cstdint>
#include <optional>
using namespace qcborpp;

// Helper: encode a flat map
static const_byte_span encode_map(uint8_t* buf, size_t sz) {
    encoder enc(byte_span{buf, sz});
    {
        auto m = enc.map();
        m["name"]    = "Niels";
        m["count"]   = 42;
        // nested_map built via low-level API
        enc.add_text("nested_map");
        enc.open_map();
        enc.add_text("inner");  enc.add_int64(99);
        enc.close_map();
    }
    return enc.finish();
}

// ===== find(std::string_view) =====

TEST_CASE("find: existing string key returns proxy", "[find_api]") {
    uint8_t buf[256];
    auto data = encode_map(buf, sizeof(buf));
    decoder dec(data);
    {
        auto m = dec.map();
        auto opt = m.find("name");
        REQUIRE(opt.has_value());
        CHECK(std::string_view(opt->as_string()) == "Niels");
    }
    REQUIRE_FALSE(dec.finish());
}

TEST_CASE("find: existing int value returns proxy", "[find_api]") {
    uint8_t buf[256];
    auto data = encode_map(buf, sizeof(buf));
    decoder dec(data);
    {
        auto m = dec.map();
        auto opt = m.find("count");
        REQUIRE(opt.has_value());
        CHECK(opt->as_int64() == 42);
    }
    REQUIRE_FALSE(dec.finish());
}

TEST_CASE("find: missing key returns nullopt", "[find_api]") {
    uint8_t buf[256];
    auto data = encode_map(buf, sizeof(buf));
    decoder dec(data);
    {
        auto m = dec.map();
        auto opt = m.find("nonexistent");
        CHECK_FALSE(opt.has_value());
    }
    REQUIRE_FALSE(dec.finish());
}

TEST_CASE("find: does not throw on missing key", "[find_api]") {
    uint8_t buf[256];
    auto data = encode_map(buf, sizeof(buf));
    decoder dec(data);
    {
        auto m = dec.map();
        CHECK_NOTHROW(m.find("ghost"));
        CHECK_NOTHROW(m.find(""));
    }
    REQUIRE_FALSE(dec.finish());
}

TEST_CASE("find: separates key-missing from type-mismatch", "[find_api]") {
    // This test intentionally triggers a type error (as_int64 on a string),
    // which leaves an error in the QCBOR context — finish() would fail.
    uint8_t buf[256];
    auto data = encode_map(buf, sizeof(buf));
    decoder dec(data);
    auto m = dec.map();

    // find() tells us the key exists
    auto opt = m.find("name");
    REQUIRE(opt.has_value());
    // Now caller handles type: as_int64() on a string throws
    CHECK_THROWS_AS(opt->as_int64(), error);
    // as_string() works
    CHECK(opt->as_string() == "Niels");
}

TEST_CASE("find: nested map access", "[find_api]") {
    uint8_t buf[256];
    auto data = encode_map(buf, sizeof(buf));
    decoder dec(data);
    {
        auto m = dec.map();
        auto opt = m.find("nested_map");
        REQUIRE(opt.has_value());
        auto inner = opt->as_map();
        CHECK(inner["inner"].as_int64() == 99);
    }
    REQUIRE_FALSE(dec.finish());
}

// ===== find(int64_t) =====

TEST_CASE("find(int64_t): existing int key returns proxy", "[find_api]") {
    uint8_t buf[128];
    encoder enc(byte_span{buf, sizeof(buf)});
    {
        auto m = enc.map();
        m[42] = "answer";
        m[99] = "ninety-nine";
    }
    auto data = enc.finish();
    decoder dec(data);
    {
        auto m = dec.map();
        auto opt = m.find(int64_t(42));
        REQUIRE(opt.has_value());
        CHECK(std::string_view(opt->as_string()) == "answer");
    }
    REQUIRE_FALSE(dec.finish());
}

TEST_CASE("find(int64_t): missing int key returns nullopt", "[find_api]") {
    uint8_t buf[128];
    encoder enc(byte_span{buf, sizeof(buf)});
    {
        auto m = enc.map();
        m[1] = "one";
    }
    auto data = enc.finish();
    decoder dec(data);
    {
        auto m = dec.map();
        CHECK_FALSE(m.find(int64_t(0)).has_value());
        CHECK_FALSE(m.find(int64_t(999)).has_value());
        CHECK_FALSE(m.find(int64_t(-1)).has_value());
    }
    REQUIRE_FALSE(dec.finish());
}

TEST_CASE("find(int64_t): mixed string and int keys", "[find_api]") {
    uint8_t buf[128];
    encoder enc(byte_span{buf, sizeof(buf)});
    {
        auto m = enc.map();
        m["name"] = "test";
        m[42]     = 100;
    }
    auto data = enc.finish();
    decoder dec(data);
    {
        auto m = dec.map();
        // string-keyed find works
        auto s_opt = m.find("name");
        REQUIRE(s_opt.has_value());
        CHECK(s_opt->as_string() == "test");
        // int-keyed find works
        auto i_opt = m.find(int64_t(42));
        REQUIRE(i_opt.has_value());
        CHECK(i_opt->as_int64() == 100);
        // string key not found via int find
        CHECK_FALSE(m.find(int64_t(0)).has_value());
    }
    REQUIRE_FALSE(dec.finish());
}

TEST_CASE("find: prefetch triggered automatically", "[find_api]") {
    uint8_t buf[256];
    auto data = encode_map(buf, sizeof(buf));
    decoder dec(data);
    dec.set_force_prefetch(false);
    {
        auto m = dec.map();
        // No explicit prefetch — find() triggers it
        auto opt = m.find("count");
        REQUIRE(opt.has_value());
        CHECK(opt->as_int64() == 42);
        // Second find hits cache
        auto opt2 = m.find("name");
        REQUIRE(opt2.has_value());
        CHECK(opt2->as_string() == "Niels");
    }
    REQUIRE_FALSE(dec.finish());
}
