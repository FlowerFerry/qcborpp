/*
 * tests/test_item_proxy_type.cpp
 *
 * Unit tests for item_proxy::type() and is_*() methods.
 * Run with: qcborpp_tests "[item_proxy][type]"
 */

#include <qcborpp/qcborpp.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace qcborpp;

// Value that requires uint64 encoding (>= 2^63)
static constexpr uint64_t kLargeUint64 = uint64_t(1) << 63;

// ============================================================================
// Cached path: scalar items in array (next() caches the decoded_item)
// ============================================================================

TEST_CASE("item_proxy: type() for cached scalars", "[item_proxy][type]") {
    dynamic_encoder enc;
    enc.open_array();
    enc.add_int64(-42);
    enc.add_uint64(kLargeUint64);
    enc.add_text("hello");
    enc.add_double(3.14);
    enc.add_bool(true);
    enc.add_bool(false);
    enc.add_null();
    enc.close_array();
    auto data = enc.finish();
    decoder dec(data);
    auto a = dec.array();

    auto item1 = a.next();
    REQUIRE(item1.is_int64());
    REQUIRE(!item1.is_uint64());
    REQUIRE(int64_t(item1) == -42);

    auto item2 = a.next();
    REQUIRE(item2.is_uint64());
    REQUIRE(!item2.is_int64());

    auto item3 = a.next();
    REQUIRE(item3.is_string());
    REQUIRE(!item3.is_bytes());
    REQUIRE(item3.as_string() == "hello");

    auto item4 = a.next();
    REQUIRE(item4.is_double());
    REQUIRE(!item4.is_float());
    REQUIRE(item4.as_double() == 3.14);

    auto item5 = a.next();
    REQUIRE(item5.is_bool_true());
    REQUIRE(item5.is_bool());
    REQUIRE(!item5.is_bool_false());

    auto item6 = a.next();
    REQUIRE(item6.is_bool_false());
    REQUIRE(item6.is_bool());

    auto item7 = a.next();
    REQUIRE(item7.is_null());

    dec.finish();
}

// ============================================================================
// Label-based path: map lookup via operator[]
// ============================================================================

TEST_CASE("item_proxy: type() for label-based map items", "[item_proxy][type]") {
    dynamic_encoder enc;
    std::vector<uint8_t> raw = {0xAA, 0xBB, 0xCC};
    {
        auto m = enc.map();
        m["ival"]  = -7;
        m["uval"]  = kLargeUint64;
        m["text"]  = "label-test";
        m["bval"]  = true;
        m["nval"]  = nullptr;
        m["dval"]  = 3.14;
        const_byte_span bs{raw.data(), raw.size()};
        m["bytes"] = bs;
    }
    auto data = enc.finish();
    decoder dec(data);
    auto m = dec.map();

    auto ival = m["ival"];
    REQUIRE(ival.is_int64());
    REQUIRE(!ival.is_container());
    REQUIRE(int64_t(ival) == -7);

    auto uval = m["uval"];
    REQUIRE(uval.is_uint64());

    auto text = m["text"];
    REQUIRE(text.is_string());
    REQUIRE(text.as_string() == "label-test");

    auto bval = m["bval"];
    REQUIRE(bval.is_bool_true());
    REQUIRE(bval.is_bool());

    auto nval = m["nval"];
    REQUIRE(nval.is_null());

    auto dval = m["dval"];
    REQUIRE(dval.is_double());

    auto bytes = m["bytes"];
    REQUIRE(bytes.is_bytes());
    REQUIRE(!bytes.is_string());

    dec.finish();
}

// ============================================================================
// Bare proxy: containers returned by array_scope::next()
// ============================================================================

TEST_CASE("item_proxy: type() for container proxies", "[item_proxy][type]") {
    dynamic_encoder enc;
    {
        auto a = enc.array();
        a << 1;
        {
            auto m = a.add_map();
            m["k"] = "v";
        }
        {
            auto a2 = a.add_array();
            a2 << 10 << 20;
        }
    }
    auto data = enc.finish();
    decoder dec(data);
    auto a = dec.array();

    // scalar item 1
    auto item1 = a.next();
    REQUIRE(item1.is_int64());
    REQUIRE(!item1.is_container());

    // container: map
    auto item2 = a.next();
    REQUIRE(item2.is_map());
    REQUIRE(item2.is_container());
    {
        auto m = item2.as_map();
        REQUIRE(m["k"].as_string() == "v");
    }

    // container: array
    auto item3 = a.next();
    REQUIRE(item3.is_array());
    REQUIRE(item3.is_container());
    {
        auto a2 = item3.as_array();
        REQUIRE(int64_t(a2.next()) == 10);
        REQUIRE(int64_t(a2.next()) == 20);
    }

    dec.finish();
}

// ============================================================================
// is_simple and is_undef
// ============================================================================

TEST_CASE("item_proxy: is_simple and is_undef", "[item_proxy][type]") {
    dynamic_encoder enc;
    enc.open_array();
    enc.add_undef();
    enc.add_simple(99);
    enc.close_array();
    auto data = enc.finish();
    decoder dec(data);
    auto a = dec.array();

    auto undef_item = a.next();
    REQUIRE(undef_item.is_undef());
    REQUIRE(!undef_item.is_null());

    auto simple_item = a.next();
    REQUIRE(simple_item.is_simple());

    dec.finish();
}

// ============================================================================
// type() returns correct cbor_type for each type
// ============================================================================

TEST_CASE("item_proxy: type() returns exact cbor_type", "[item_proxy][type]") {
    dynamic_encoder enc;
    enc.open_array();
    enc.add_int64(42);
    enc.add_uint64(kLargeUint64);
    enc.add_double(3.14);
    enc.add_text("str");
    enc.add_bool(true);
    enc.add_null();
    enc.close_array();
    auto data = enc.finish();
    decoder dec(data);
    auto a = dec.array();

    REQUIRE(a.next().type() == cbor_type::int64);
    REQUIRE(a.next().type() == cbor_type::uint64);
    REQUIRE(a.next().type() == cbor_type::double_v);
    REQUIRE(a.next().type() == cbor_type::text_string);
    REQUIRE(a.next().type() == cbor_type::true_v);
    REQUIRE(a.next().type() == cbor_type::null_v);

    dec.finish();
}
