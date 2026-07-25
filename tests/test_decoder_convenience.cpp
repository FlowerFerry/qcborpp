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
