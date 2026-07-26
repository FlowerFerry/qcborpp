/*
 * test_decoder_convenience.cpp — unit tests for convenience features:
 *   get_or(), contains(), size(), for_each()
 */
#include <catch2/catch_test_macros.hpp>
#include <qcborpp/qcborpp.hpp>
#include <cstdint>
using namespace qcborpp;

#define BUF byte_span{buf, sizeof(buf)}

// Helper: encode a flat map via builder and return the CBOR data
static const_byte_span encode_map(uint8_t* buf, size_t buf_size) {
    encoder enc(byte_span{buf, buf_size});
    {
        auto m = enc.map();
        m["name"]    = "Niels";
        m["count"]   = 42;
        m["active"]  = true;
        m["version"] = 1.5;
        m["comment"] = nullptr;
    }
    return enc.finish();
}

// ===== get_or =====

TEST_CASE("convenience: get_or — existing key returns value", "[convenience]") {
    uint8_t buf[256];
    auto data = encode_map(buf, sizeof(buf));
    decoder dec(data);
    auto m = dec.map();

    CHECK(m["count"].get_or(0) == 42);
    CHECK(m["name"].get_or(std::string_view{"fallback"}) == "Niels");
    CHECK(m["active"].get_or(false) == true);
    CHECK(std::abs(m["version"].get_or(0.0) - 1.5) < 0.001);
}

TEST_CASE("convenience: get_or — missing key returns default", "[convenience]") {
    uint8_t buf[256];
    auto data = encode_map(buf, sizeof(buf));
    decoder dec(data);
    auto m = dec.map();

    CHECK(m["nonexistent"].get_or(-1) == -1);
    CHECK(m["no_such_key"].get_or(std::string_view{"default"}) == "default");
    CHECK(m["missing_bool"].get_or(false) == false);
    CHECK(m["missing_double"].get_or(99.9) == 99.9);
}

TEST_CASE("convenience: get_or — type mismatch returns default", "[convenience]") {
    uint8_t buf[256];
    auto data = encode_map(buf, sizeof(buf));
    decoder dec(data);
    auto m = dec.map();

    // "name" is a string, not int
    CHECK(m["name"].get_or(-1) == -1);
    // "count" is int, not string
    CHECK(m["count"].get_or(std::string_view{"fallback"}) == "fallback");
}

// ===== contains =====

TEST_CASE("convenience: contains — existing key", "[convenience]") {
    uint8_t buf[256];
    auto data = encode_map(buf, sizeof(buf));
    decoder dec(data);
    auto m = dec.map();

    CHECK(m.contains("name"));
    CHECK(m.contains("count"));
    CHECK(m.contains("comment"));
}

TEST_CASE("convenience: contains — missing key", "[convenience]") {
    uint8_t buf[256];
    auto data = encode_map(buf, sizeof(buf));
    decoder dec(data);
    auto m = dec.map();

    CHECK_FALSE(m.contains("nonexistent"));
    CHECK_FALSE(m.contains(""));
    CHECK_FALSE(m.contains("nope"));
}

TEST_CASE("convenience: contains — does not consume cursor", "[convenience]") {
    uint8_t buf[256];
    auto data = encode_map(buf, sizeof(buf));
    decoder dec(data);
    auto m = dec.map();

    CHECK(m.contains("name"));
    // Still able to access after contains()
    CHECK(std::string_view{m["name"]} == "Niels");
}

// ── contains(int64_t) for integer-keyed maps ──

TEST_CASE("convenience: contains(int64_t) — existing key", "[convenience]") {
    dynamic_encoder enc;
    {
        auto m = enc.map();
        m[0]  = "zero";
        m[42] = "answer";
        m[99] = "ninety-nine";
    }
    auto data = enc.finish();
    decoder dec(data);
    auto m = dec.map();

    CHECK(m.contains(0));
    CHECK(m.contains(42));
    CHECK(m.contains(99));
}

TEST_CASE("convenience: contains(int64_t) — missing key", "[convenience]") {
    dynamic_encoder enc;
    {
        auto m = enc.map();
        m[1] = "one";
        m[2] = "two";
    }
    auto data = enc.finish();
    decoder dec(data);
    auto m = dec.map();

    CHECK_FALSE(m.contains(0));
    CHECK_FALSE(m.contains(999));
    CHECK_FALSE(m.contains(-1));
}

TEST_CASE("convenience: contains(int64_t) — mixed int/string keys", "[convenience]") {
    dynamic_encoder enc;
    {
        auto m = enc.map();
        m["name"] = "Niels";
        m[42]     = "answer";
    }
    auto data = enc.finish();
    decoder dec(data);
    auto m = dec.map();

    // string-keyed contains still works
    CHECK(m.contains("name"));
    // int-keyed contains works alongside string keys
    CHECK(m.contains(42));
    CHECK_FALSE(m.contains(43));
}

// ===== size =====

TEST_CASE("convenience: size — flat map", "[convenience]") {
    uint8_t buf[256];
    auto data = encode_map(buf, sizeof(buf));
    decoder dec(data);
    auto m = dec.map();

    CHECK(m.size() == 5);  // name, count, active, version, comment
}

TEST_CASE("convenience: size — empty map", "[convenience]") {
    uint8_t buf[256];
    encoder enc(BUF);
    enc.open_map();
    enc.close_map();
    auto data = enc.finish();

    decoder dec(data);
    auto m = dec.map();
    CHECK(m.size() == 0);
}

// ===== for_each =====

TEST_CASE("convenience: for_each — iterate all entries", "[convenience]") {
    uint8_t buf[256];
    auto data = encode_map(buf, sizeof(buf));
    decoder dec(data);
    auto m = dec.map();

    int count = 0;
    bool found_name = false, found_count = false;
    m.for_each([&](std::string_view key, decoded_item val) {
        ++count;
        if (key == "name")  { found_name = true;  CHECK(val.type == cbor_type::text_string); }
        if (key == "count") { found_count = true; CHECK(val.type == cbor_type::int64); }
    });

    CHECK(count == 5);
    CHECK(found_name);
    CHECK(found_count);
}

TEST_CASE("convenience: for_each — empty map", "[convenience]") {
    uint8_t buf[256];
    encoder enc(BUF);
    enc.open_map();
    enc.close_map();
    auto data = enc.finish();

    decoder dec(data);
    auto m = dec.map();
    int count = 0;
    m.for_each([&](std::string_view, decoded_item) { ++count; });
    CHECK(count == 0);
}

TEST_CASE("convenience: for_each — chaining returns map_scope ref", "[convenience]") {
    uint8_t buf[256];
    auto data = encode_map(buf, sizeof(buf));
    decoder dec(data);
    auto m = dec.map();

    auto& ref = m.for_each([](std::string_view, decoded_item) {});
    CHECK(&ref == &m);  // returns *this
}

// ===== integration: get_or + contains + size together =====

TEST_CASE("convenience: combined usage", "[convenience]") {
    uint8_t buf[256];
    auto data = encode_map(buf, sizeof(buf));
    decoder dec(data);
    auto m = dec.map();

    // Check existence first
    if (m.contains("count")) {
        CHECK(m["count"].get_or(0) == 42);
    }
    CHECK(m["flag"].get_or(true) == true);  // missing → default
    CHECK(m.size() == 5);
}

// ===== get_or — syntactic sugar on map_scope =====

TEST_CASE("convenience: get_or(map) — string key, existing", "[convenience]") {
    uint8_t buf[256];
    auto data = encode_map(buf, sizeof(buf));
    decoder dec(data);
    auto m = dec.map();

    CHECK(m.get_or("count", 0) == 42);
    CHECK(m.get_or("name", std::string_view{"fallback"}) == "Niels");
    CHECK(m.get_or("active", false) == true);
}

TEST_CASE("convenience: get_or(map) — string key, missing returns default", "[convenience]") {
    uint8_t buf[256];
    auto data = encode_map(buf, sizeof(buf));
    decoder dec(data);
    auto m = dec.map();

    CHECK(m.get_or("ghost", -1) == -1);
    CHECK(m.get_or("nope", std::string_view{"default"}) == "default");
    CHECK(m.get_or("no_double", 3.14) == 3.14);
}

TEST_CASE("convenience: get_or(map) — int literal deduces correctly", "[convenience]") {
    uint8_t buf[256];
    auto data = encode_map(buf, sizeof(buf));
    decoder dec(data);
    auto m = dec.map();

    // 0 is int, maps to int64_t via get_or(int)
    CHECK(m.get_or("count", 0) == 42);
    // missing → default int
    CHECK(m.get_or("missing", 99) == 99);
}

// ===== try_get — std::optional access =====

TEST_CASE("convenience: try_get — existing key returns value", "[convenience]") {
    uint8_t buf[256];
    auto data = encode_map(buf, sizeof(buf));
    decoder dec(data);
    auto m = dec.map();

    auto opt1 = m["count"].try_get<int64_t>();
    CHECK(opt1.has_value());
    CHECK(*opt1 == 42);

    auto opt2 = m["name"].try_get<std::string_view>();
    CHECK(opt2.has_value());
    CHECK(*opt2 == "Niels");

    auto opt3 = m["active"].try_get<bool>();
    CHECK(opt3.has_value());
    CHECK(*opt3 == true);
}

TEST_CASE("convenience: try_get — missing key returns nullopt", "[convenience]") {
    uint8_t buf[256];
    auto data = encode_map(buf, sizeof(buf));
    decoder dec(data);
    auto m = dec.map();

    auto opt = m["nonexistent"].try_get<int64_t>();
    CHECK_FALSE(opt.has_value());
}

TEST_CASE("convenience: try_get — type mismatch returns nullopt", "[convenience]") {
    uint8_t buf[256];
    auto data = encode_map(buf, sizeof(buf));
    decoder dec(data);
    auto m = dec.map();

    // "name" is a string, not int64_t
    auto opt = m["name"].try_get<int64_t>();
    CHECK_FALSE(opt.has_value());
}

// ===== try_get on map_scope — direct map-level optional access =====

TEST_CASE("convenience: try_get(map) — existing key returns value", "[convenience]") {
    uint8_t buf[256];
    auto data = encode_map(buf, sizeof(buf));
    decoder dec(data);
    auto m = dec.map();

    auto v = m.try_get<int64_t>("count");
    REQUIRE(v.has_value());
    CHECK(*v == 42);
}

TEST_CASE("convenience: try_get(map) — missing key returns nullopt", "[convenience]") {
    uint8_t buf[256];
    auto data = encode_map(buf, sizeof(buf));
    decoder dec(data);
    auto m = dec.map();

    auto v = m.try_get<int64_t>("ghost");
    CHECK_FALSE(v.has_value());
}

TEST_CASE("convenience: try_get(map) — type mismatch returns nullopt", "[convenience]") {
    uint8_t buf[256];
    auto data = encode_map(buf, sizeof(buf));
    decoder dec(data);
    auto m = dec.map();

    // "name" is a string, not int64_t
    auto v = m.try_get<int64_t>("name");
    CHECK_FALSE(v.has_value());
}

// ===== get_* — cross-numeric convert =====

TEST_CASE("convenience: get_int64 — converts uint64", "[convenience]") {
    uint8_t buf[128];
    encoder enc(byte_span{buf, sizeof(buf)});
    {
        auto m = enc.map();
        m["uval"] = uint64_t(42);
        m["dval"] = 3.14;
    }
    decoder dec(enc.finish());
    auto m = dec.map();

    CHECK(m["uval"].get_int64() == 42);
    CHECK(m["dval"].get_int64() == 3);  // llround(3.14) = 3
}

TEST_CASE("convenience: get_int64 — rejects non-numeric", "[convenience]") {
    uint8_t buf[128];
    auto data = encode_map(buf, sizeof(buf));
    decoder dec(data);
    auto m = dec.map();

    CHECK(m["name"].get_or(int64_t(-1)) == -1);  // "name" is string → default
}

TEST_CASE("convenience: get_uint64 — converts int64", "[convenience]") {
    uint8_t buf[128];
    encoder enc(byte_span{buf, sizeof(buf)});
    {
        auto m = enc.map();
        m["ival"] = int64_t(100);
    }
    decoder dec(enc.finish());
    auto m = dec.map();

    CHECK(m["ival"].get_uint64() == 100);
}

TEST_CASE("convenience: get_double — converts int64 and uint64", "[convenience]") {
    uint8_t buf[128];
    encoder enc(byte_span{buf, sizeof(buf)});
    {
        auto m = enc.map();
        m["ival"] = int64_t(7);
        m["uval"] = uint64_t(99);
    }
    decoder dec(enc.finish());
    auto m = dec.map();

    CHECK(m["ival"].get_double() == 7.0);
    CHECK(m["uval"].get_double() == 99.0);
}

TEST_CASE("convenience: as_int64 — still strict rejects uint64", "[convenience]") {
    uint8_t buf[128];
    encoder enc(byte_span{buf, sizeof(buf)});
    {
        auto m = enc.map();
        m["uval"] = UINT64_MAX;  // > INT64_MAX → QCBOR encodes as uint64
    }
    decoder dec(enc.finish());
    auto m = dec.map();

    CHECK_THROWS_AS(m["uval"].as_int64(), error);
}

// ===== get_or — cross-numeric convert =====

TEST_CASE("convenience: get_or int64 — converts uint64", "[convenience]") {
    uint8_t buf[128];
    encoder enc(byte_span{buf, sizeof(buf)});
    { auto m = enc.map(); m["val"] = uint64_t(77); }
    decoder dec(enc.finish());
    auto m = dec.map();

    CHECK(m["val"].get_or(int64_t(-1)) == 77);
}

TEST_CASE("convenience: get_or double — converts int64", "[convenience]") {
    uint8_t buf[128];
    encoder enc(byte_span{buf, sizeof(buf)});
    { auto m = enc.map(); m["val"] = int64_t(3); }
    decoder dec(enc.finish());
    auto m = dec.map();

    CHECK(m["val"].get_or(0.0) == 3.0);
}

// ===== try_get — cross-numeric convert =====

TEST_CASE("convenience: try_get int64_t — converts double", "[convenience]") {
    uint8_t buf[128];
    encoder enc(byte_span{buf, sizeof(buf)});
    { auto m = enc.map(); m["val"] = 3.14; }
    decoder dec(enc.finish());
    auto m = dec.map();

    auto v = m.try_get<int64_t>("val");
    REQUIRE(v.has_value());
    CHECK(*v == 3);
}

// ===== empty — map_scope emptiness check =====

TEST_CASE("convenience: empty — non-empty map returns false", "[convenience]") {
    uint8_t buf[256];
    auto data = encode_map(buf, sizeof(buf));  // count, name, active, version, comment
    decoder dec(data);
    auto m = dec.map();
    CHECK_FALSE(m.empty());
}

TEST_CASE("convenience: empty — truly empty map returns true", "[convenience]") {
    uint8_t buf[128];
    encoder enc(byte_span{buf, sizeof(buf)});
    { auto m = enc.map(); /* no entries */ }
    decoder dec(enc.finish());
    auto m = dec.map();
    CHECK(m.empty());
}

// ===== force_prefetch(false) — lazy decode path =====

TEST_CASE("force_prefetch(false): operator[] returns correct values", "[force_prefetch]") {
    dynamic_encoder enc;
    {
        auto m = enc.map();
        m["name"]   = "Niels";
        m["count"]  = 42;
        m["active"] = true;
        m["pi"]     = 3.14;
    }
    decoder dec(enc.finish());
    dec.set_force_prefetch(false);
    auto m = dec.map();

    // Each access hits the Spiffy lazy path (GetItemInMapSZ)
    CHECK(std::string_view(m["name"]) == "Niels");
    CHECK(m["count"].get_or(0) == 42);
    CHECK(m["active"].get_or(false) == true);
    CHECK(std::abs(m["pi"].get_or(0.0) - 3.14) < 0.001);
}

TEST_CASE("force_prefetch(false): contains triggers on-demand prefetch", "[force_prefetch]") {
    dynamic_encoder enc;
    {
        auto m = enc.map();
        m["a"] = 1;
        m["b"] = 2;
        m["c"] = 3;
    }
    decoder dec(enc.finish());
    dec.set_force_prefetch(false);
    auto m = dec.map();

    // contains() triggers prefetch → cache_ populated
    CHECK(m.contains("a"));
    CHECK(m.contains("b"));
    CHECK_FALSE(m.contains("zzz"));

    // After contains(), operator[] hits the now-populated cache
    CHECK(m["a"].get_or(0) == 1);
    CHECK(m["b"].get_or(0) == 2);
    CHECK(m["c"].get_or(0) == 3);
}

TEST_CASE("force_prefetch(false): size triggers on-demand prefetch", "[force_prefetch]") {
    dynamic_encoder enc;
    {
        auto m = enc.map();
        m["x"] = 10;
        m["y"] = 20;
        m["z"] = 30;
    }
    decoder dec(enc.finish());
    dec.set_force_prefetch(false);
    auto m = dec.map();

    CHECK(m.size() == 3);
    // After size(), operator[] hits cache
    CHECK(m["x"].get_or(0) == 10);
}

TEST_CASE("force_prefetch(false): get_or works without prefetch", "[force_prefetch]") {
    dynamic_encoder enc;
    {
        auto m = enc.map();
        m["value"] = 99;
        m["text"]  = "hello";
    }
    decoder dec(enc.finish());
    dec.set_force_prefetch(false);
    auto m = dec.map();

    // get_or uses Spiffy GetItemInMapSZ — no prefetch needed
    CHECK(m["value"].get_or(0) == 99);
    CHECK(m["text"].get_or(std::string_view{"x"}) == "hello");
    CHECK(m["missing"].get_or(-1) == -1);
}

TEST_CASE("force_prefetch(false): try_get works without prefetch", "[force_prefetch]") {
    dynamic_encoder enc;
    {
        auto m = enc.map();
        m["val"] = 77;
    }
    decoder dec(enc.finish());
    dec.set_force_prefetch(false);
    auto m = dec.map();

    auto v1 = m["val"].try_get<int64_t>();
    REQUIRE(v1.has_value());
    CHECK(*v1 == 77);

    auto v2 = m["ghost"].try_get<int64_t>();
    CHECK_FALSE(v2.has_value());
}

TEST_CASE("force_prefetch(false): nested map via as_map()", "[force_prefetch]") {
    dynamic_encoder enc;
    {
        auto outer = enc.map();
        outer["top"] = "level1";
        {
            auto inner = outer["inner"].map();
            inner["deep"] = "level2";
        }
    }
    decoder dec(enc.finish());
    dec.set_force_prefetch(false);
    auto m = dec.map();

    // Outer map: lazy access
    CHECK(std::string_view(m["top"]) == "level1");

    // Nested map: as_map() on label-enabled proxy (force_prefetch_=false → no auto-prefetch)
    auto inner = m["inner"].as_map();
    CHECK(std::string_view(inner["deep"]) == "level2");
}

TEST_CASE("force_prefetch(false): for_each triggers prefetch", "[force_prefetch]") {
    dynamic_encoder enc;
    {
        auto m = enc.map();
        m["k1"] = 1;
        m["k2"] = 2;
    }
    decoder dec(enc.finish());
    dec.set_force_prefetch(false);
    auto m = dec.map();

    int count = 0;
    m.for_each([&](std::string_view, decoded_item) { ++count; });
    CHECK(count == 2);
}

TEST_CASE("force_prefetch(false): for_each_int triggers prefetch", "[force_prefetch]") {
    dynamic_encoder enc;
    {
        auto m = enc.map();
        m[10] = "ten";
        m[20] = "twenty";
    }
    decoder dec(enc.finish());
    dec.set_force_prefetch(false);
    auto m = dec.map();

    int count = 0;
    m.for_each_int([&](int64_t, decoded_item) { ++count; });
    CHECK(count == 2);
}

TEST_CASE("force_prefetch(false): mixed string and int keys", "[force_prefetch]") {
    dynamic_encoder enc;
    {
        auto m = enc.map();
        m["name"] = "test";
        m[42]     = "answer";
    }
    decoder dec(enc.finish());
    dec.set_force_prefetch(false);
    auto m = dec.map();

    CHECK(m.contains("name"));
    CHECK(m.contains(42));
    CHECK(std::string_view(m["name"]) == "test");
    CHECK(m.size() == 2);
}

// Verify default is still force_prefetch=true
TEST_CASE("force_prefetch: default is true", "[force_prefetch]") {
    dynamic_encoder enc;
    {
        auto m = enc.map();
        for (int i = 0; i < 20; ++i)
            m["k" + std::to_string(i)] = i;
    }
    decoder dec(enc.finish());
    CHECK(dec.force_prefetch() == true);

    // Default path (with prefetch) works as before
    auto m = dec.map();
    CHECK(m["k5"].get_or(-1) == 5);
    CHECK(m.size() == 20);
}
