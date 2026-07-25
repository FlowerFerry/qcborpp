/*
 * tests/test_coverage_gap.cpp
 *
 * Coverage gap fill: exercises all previously untested public APIs.
 * Run with: qcborpp_tests "[coverage]"
 */

#include <qcborpp/qcborpp.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <vector>
#include <cstring>

using namespace qcborpp;

// ── Encoder: open_map / close_map (low-level, no RAII) ──

TEST_CASE("dynamic_encoder: open_map/close_map low-level", "[dynamic_encoder][coverage]") {
    dynamic_encoder enc;
    enc.open_map();
    enc.add_text("key");
    enc.add_int64(42);
    enc.close_map();
    auto data = enc.finish();
    REQUIRE(data.size() > 0);
}

// ── Encoder: add_undefined ──

TEST_CASE("dynamic_encoder: add_undefined", "[dynamic_encoder][coverage]") {
    dynamic_encoder enc;
    enc.open_array();
    enc.add_undef();
    enc.close_array();
    auto data = enc.finish();
    REQUIRE(data.size() == 2);  // 0x81 (array of 1) + 0xF7 (undefined)
}

// ── Encoder: add_simple ──

TEST_CASE("dynamic_encoder: add_simple", "[dynamic_encoder][coverage]") {
    dynamic_encoder enc;
    enc.open_array();
    enc.add_simple(20);  // false
    enc.add_simple(21);  // true
    enc.add_simple(22);  // null
    enc.close_array();
    auto data = enc.finish();
    REQUIRE(data.size() > 0);
}

// ── Encoder: auto move ──

TEST_CASE("dynamic_encoder: map_builder move", "[dynamic_encoder][coverage]") {
    dynamic_encoder enc;
    {
        auto m1 = enc.map();
        m1["a"] = 1;
        {
            auto m2 = std::move(m1);
            m2["b"] = 2;
        } // m2 destructor closes map
    }
    auto data = enc.finish();
    REQUIRE(data.size() > 0);

    decoder dec(data);
    auto m = dec.map();
    REQUIRE(int64_t(m["a"]) == 1);
    REQUIRE(int64_t(m["b"]) == 2);
    dec.finish();
}

// ── Encoder: array_builder move ──

TEST_CASE("dynamic_encoder: array_builder move", "[dynamic_encoder][coverage]") {
    dynamic_encoder enc;
    {
        auto a1 = enc.array();
        a1 << 10;
        {
            auto a2 = std::move(a1);
            a2 << 20 << 30;
        } // a2 destructor closes array
    }
    auto data = enc.finish();
    REQUIRE(data.size() > 0);

    decoder dec(data);
    auto a = dec.array();
    REQUIRE(int64_t(a.next()) == 10);
    REQUIRE(int64_t(a.next()) == 20);
    REQUIRE(int64_t(a.next()) == 30);
    dec.finish();
}

// ── Encoder: key_proxy move ──

TEST_CASE("dynamic_encoder: key_proxy move", "[dynamic_encoder][coverage]") {
    dynamic_encoder enc;
    {
        auto m = enc.map();
        auto kp = std::move(m["x"]);
        kp = 100;
    }
    auto data = enc.finish();
    REQUIRE(data.size() > 0);

    decoder dec(data);
    auto m = dec.map();
    REQUIRE(int64_t(m["x"]) == 100);
    dec.finish();
}

// ── Decoder: as_date_string ──

TEST_CASE("decoder: as_date_string", "[decoder][coverage]") {
    dynamic_encoder enc;
    enc.open_array();
    enc.add_date_string("2024-01-15T10:30:00Z", true);
    enc.close_array();
    auto data = enc.finish();

    decoder dec(data);
    auto a = dec.array();
    auto s = a.next().as_date_string();
    REQUIRE(s == "2024-01-15T10:30:00Z");
    dec.finish();
}

// ── Decoder: as_days_string ──

TEST_CASE("decoder: as_days_string", "[decoder][coverage]") {
    dynamic_encoder enc;
    enc.open_array();
    enc.add_days_string("2024-01-15", true);
    enc.close_array();
    auto data = enc.finish();

    decoder dec(data);
    auto a = dec.array();
    auto s = a.next().as_days_string(0);
    REQUIRE(s == "2024-01-15");
    dec.finish();
}

// ── Decoder: as_b64 ──

TEST_CASE("decoder: as_b64", "[decoder][coverage]") {
    dynamic_encoder enc;
    enc.open_array();
    enc.add_b64_text("SGVsbG8=", true);
    enc.close_array();
    auto data = enc.finish();

    decoder dec(data);
    auto a = dec.array();
    auto s = a.next().as_b64_text();
    REQUIRE(s == "SGVsbG8=");
    dec.finish();
}

// ── Decoder: as_b64url ──

TEST_CASE("decoder: as_b64url", "[decoder][coverage]") {
    dynamic_encoder enc;
    enc.open_array();
    enc.add_b64url_text("SGVsbG8", true);
    enc.close_array();
    auto data = enc.finish();

    decoder dec(data);
    auto a = dec.array();
    auto s = a.next().as_b64url(0);
    REQUIRE(s == "SGVsbG8");
    dec.finish();
}

// ── Decoder: as_regex ──

TEST_CASE("decoder: as_regex", "[decoder][coverage]") {
    dynamic_encoder enc;
    enc.open_array();
    enc.add_regex("[a-z]+", true);
    enc.close_array();
    auto data = enc.finish();

    decoder dec(data);
    auto a = dec.array();
    auto s = a.next().as_regex();
    REQUIRE(s == "[a-z]+");
    dec.finish();
}

// ── Decoder: as_mime ──

TEST_CASE("decoder: as_mime text", "[decoder][coverage]") {
    dynamic_encoder enc;
    enc.open_array();
    enc.add_mime_data("text/plain; charset=utf-8", true);
    enc.close_array();
    auto data = enc.finish();

    decoder dec(data);
    auto a = dec.array();
    bool is_bin = false;
    auto s = a.next().as_mime_data(&is_bin);
    REQUIRE(s == "text/plain; charset=utf-8");
    REQUIRE(!is_bin);
    dec.finish();
}

// ── Decoder: set_mem_pool ──

TEST_CASE("decoder: set_mem_pool", "[decoder][coverage]") {
    uint8_t pool[256];
    dynamic_encoder enc;
    enc.add_text("hello");
    auto data = enc.finish();

    decoder dec(data);
    auto ec = dec.set_mem_pool({pool, sizeof(pool)});
    REQUIRE(!ec);
    dec.finish();
}

// ── Decoder: get_items_in_map ──

TEST_CASE("decoder: get_items_in_map", "[decoder][coverage]") {
    dynamic_encoder enc;
    {
        auto m = enc.map();
        m["a"] = 1;
        m["b"] = 2;
    }
    auto data = enc.finish();

    decoder dec(data);
    // get_items_in_map handles entering/exiting internally
    decoder::item_spec spec;
    spec.label_str = "a";
    spec.type = cbor_type::any;
    std::vector<decoder::item_spec> specs = {spec};
    std::vector<decoded_item> out;
    auto ec = dec.get_items_in_map(specs, out);
    REQUIRE(!ec);
    REQUIRE(out.size() == 1);
    REQUIRE(out[0].type == cbor_type::int64);
    REQUIRE(out[0].value.int64_val == 1);
}

// ── Decoder: map_scope move ──

TEST_CASE("decoder: map_scope move", "[decoder][coverage]") {
    dynamic_encoder enc;
    {
        auto m = enc.map();
        m["key"] = "val";
    }
    auto data = enc.finish();

    decoder dec(data);
    auto m1 = dec.map();
    auto m2 = std::move(m1);
    REQUIRE(std::string_view(m2["key"]) == "val");
    dec.finish();
}

// ── Decoder: array_scope move ──

TEST_CASE("decoder: array_scope move", "[decoder][coverage]") {
    dynamic_encoder enc;
    {
        auto a = enc.array();
        a << 99;
    }
    auto data = enc.finish();

    decoder dec(data);
    auto a1 = dec.array();
    auto a2 = std::move(a1);
    REQUIRE(int64_t(a2.next()) == 99);
    dec.finish();
}
