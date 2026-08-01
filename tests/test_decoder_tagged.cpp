/*
 * test_decoder_tagged.cpp — unit tests for tagged-type decode paths:
 *   as_bignum(), as_decimal_fraction(), as_bigfloat(), is_tag(),
 *   array_scope::size(), item_proxy::operator[](subkey), and
 *   force_prefetch(false) with convenience methods.
 *
 * These fill coverage gaps identified 2026-07-27:
 *   - as_bignum() had declaration but no implementation (now implemented)
 *   - as_decimal_fraction() / as_bigfloat() same
 *   - is_tag() never called
 *   - array_scope::size() never called
 */
#include <catch2/catch_test_macros.hpp>
#include <qcborpp/qcborpp.hpp>
#include <cstdint>
#include <cmath>

using namespace qcborpp;

// ═══════════════════════════════════════════════════════════════════
// Bignum decode
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("decoder-tagged: as_bignum positive", "[tagged]") {
    dynamic_encoder enc;
    uint8_t big[] = {0x01, 0x02, 0x03, 0x04};
    enc.open_map();
    enc.add_text("bn");
    enc.add_bignum_positive(const_byte_span{big, 4});
    enc.close_map();
    auto data = enc.finish();

    decoder dec(data);
    auto m = dec.map();
    auto bytes = m["bn"].as_bignum();
    REQUIRE(bytes.size() == 4);
    CHECK(bytes.data()[0] == 0x01);
    CHECK(bytes.data()[3] == 0x04);
    CHECK(m["bn"].type() == cbor_type::pos_bignum);
    CHECK(m["bn"].is_tag());
}

TEST_CASE("decoder-tagged: as_bignum negative", "[tagged]") {
    dynamic_encoder enc;
    uint8_t big[] = {0xFF, 0xFE};
    enc.open_map();
    enc.add_text("bn");
    enc.add_bignum_negative(const_byte_span{big, 2});
    enc.close_map();
    auto data = enc.finish();

    decoder dec(data);
    auto m = dec.map();
    auto bytes = m["bn"].as_bignum();
    REQUIRE(bytes.size() == 2);
    CHECK(bytes.data()[0] == 0xFF);
    CHECK(m["bn"].type() == cbor_type::neg_bignum);
}

TEST_CASE("decoder-tagged: as_bignum — int-keyed map", "[tagged]") {
    dynamic_encoder enc;
    uint8_t big[] = {0xAA, 0xBB};
    enc.open_map();
    enc.add_int64(1);
    enc.add_bignum_positive(const_byte_span{big, 2});
    enc.close_map();
    auto data = enc.finish();

    decoder dec(data);
    auto m = dec.map();
    auto bytes = m[1].as_bignum();
    CHECK(bytes.size() == 2);
}

// ═══════════════════════════════════════════════════════════════════
// Decimal fraction decode
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("decoder-tagged: as_decimal_fraction integer mantissa", "[tagged]") {
    dynamic_encoder enc;
    enc.open_map();
    enc.add_text("val");
    enc.add_decimal_fraction(314159, -5);  // 3.14159
    enc.close_map();
    auto data = enc.finish();

    decoder dec(data);
    auto m = dec.map();
    auto df = m["val"].as_decimal_fraction();
    CHECK_FALSE(df.is_bignum());
    auto opt = df.as_integer();
    REQUIRE(opt.has_value());
    CHECK(*opt == 314159);
    CHECK(df.exponent == -5);
    CHECK(m["val"].is_tag());
}

TEST_CASE("decoder-tagged: as_decimal_fraction optional tag", "[tagged]") {
    // tag_requirement::optional_tag — accept plain array too
    dynamic_encoder enc;
    {
        auto m = enc.map();
        m["val"] = 1;  // plain int, not tagged — should fail with must_be_tag
    }
    auto data = enc.finish();

    decoder dec(data);
    auto m = dec.map();
    // With optional_tag, a plain int should still fail type check
    // (decimal fraction must be an array)
    CHECK_THROWS_AS(m["val"].as_decimal_fraction(tag_requirement::optional_tag), error);
}

TEST_CASE("decoder-tagged: as_decimal_fraction — int-keyed map", "[tagged]") {
    dynamic_encoder enc;
    enc.open_map();
    enc.add_int64(7);
    enc.add_decimal_fraction(100, -2);  // 1.00
    enc.close_map();
    auto data = enc.finish();

    decoder dec(data);
    auto m = dec.map();
    auto df = m[7].as_decimal_fraction();
    CHECK_FALSE(df.is_bignum());
    CHECK(df.as_integer().value() == 100);
    CHECK(df.exponent == -2);
}

// ═══════════════════════════════════════════════════════════════════
// Bigfloat decode
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("decoder-tagged: as_bigfloat", "[tagged]") {
    dynamic_encoder enc;
    enc.open_map();
    enc.add_text("bf");
    enc.add_bigfloat(13107, -13);  // ~1.6
    enc.close_map();
    auto data = enc.finish();

    decoder dec(data);
    auto m = dec.map();
    auto bf = m["bf"].as_bigfloat();
    CHECK_FALSE(bf.is_bignum());
    auto opt = bf.as_integer();
    REQUIRE(opt.has_value());
    CHECK(*opt == 13107);
    CHECK(bf.exponent == -13);
    CHECK(m["bf"].is_tag());
}

// ═══════════════════════════════════════════════════════════════════
// is_tag() on various types
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("decoder-tagged: is_tag on tagged vs untagged", "[tagged]") {
    dynamic_encoder enc;
    enc.open_map();
    enc.add_text("plain");
    enc.add_int64(42);
    enc.add_text("tagged");
    enc.add_date_epoch(1710000000);  // tag 1
    enc.close_map();
    auto data = enc.finish();

    decoder dec(data);
    auto m = dec.map();
    CHECK_FALSE(m["plain"].is_tag());
    CHECK(m["tagged"].is_tag());
}

// ═══════════════════════════════════════════════════════════════════
// array_scope::size()
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("decoder-tagged: array_scope size", "[tagged]") {
    dynamic_encoder enc;
    enc.open_array();
    for (int i = 0; i < 5; ++i)
        enc.add_int64(i);
    enc.close_array();
    auto data = enc.finish();

    decoder dec(data);
    auto a = dec.array();
    CHECK(a.size() == 5);
}

// ═══════════════════════════════════════════════════════════════════
// force_prefetch(false) with convenience methods used together
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("decoder-tagged: force_prefetch false get_or try_get", "[tagged]") {
    dynamic_encoder enc;
    {
        auto m = enc.map();
        m["count"] = 42;
        m["name"]  = "test";
    }
    auto data = enc.finish();

    decoder dec(data);
    dec.set_force_prefetch(false);
    auto m = dec.map();

    // get_or: label lookup + Spiffy resolve
    CHECK(m["count"].get_or(0) == 42);
    CHECK(m["name"].get_or(std::string_view{"x"}) == "test");
    CHECK(m["missing"].get_or(-1) == -1);

    // try_get: auto-rewind keeps each lookup independent
    auto v1 = m["count"].try_get<int64_t>();
    REQUIRE(v1.has_value());
    CHECK(*v1 == 42);

    auto v2 = m["ghost"].try_get<int64_t>();
    CHECK_FALSE(v2.has_value());

    // After a failed lookup, rewind still works
    CHECK(m["name"].get_or(std::string_view{"y"}) == "test");
}

// ═══════════════════════════════════════════════════════════════════
// map_auto_rewind — verify random-access semantics
// ═══════════════════════════════════════════════════════════════════

TEST_CASE("decoder-tagged: auto_rewind default keeps lookups independent", "[tagged]") {
    dynamic_encoder enc;
    {
        auto m = enc.map();
        m["a"] = 1;  m["b"] = 2;  m["c"] = 3;  m["d"] = 4;
    }
    auto data = enc.finish();

    decoder dec(data);
    dec.set_force_prefetch(false);
    auto m = dec.map();

    CHECK(m["a"].get_or(0) == 1);
    CHECK(m["c"].get_or(0) == 3);
    CHECK(m["b"].get_or(0) == 2);
    CHECK(m["d"].get_or(0) == 4);

    // Failed lookup does not poison later lookups
    CHECK(m["zzz"].get_or(-1) == -1);
    CHECK(m["a"].get_or(0) == 1);
    CHECK(m["b"].get_or(0) == 2);
}

TEST_CASE("decoder-tagged: auto_rewind off error poison", "[tagged]") {
    dynamic_encoder enc;
    {
        auto m = enc.map();
        m["a"] = 1;  m["b"] = 2;
    }
    auto data = enc.finish();

    decoder dec(data);
    dec.set_force_prefetch(false);
    dec.set_map_auto_rewind(false);
    auto m = dec.map();

    CHECK(m["a"].get_or(0) == 1);
    CHECK(m["b"].get_or(0) == 2);

    // Failed lookup leaves uLastError non-zero — poisons next lookups
    CHECK(m["zzz"].get_or(-1) == -1);
    CHECK(m["a"].get_or(-2) == -2);  // would be 1 with auto_rewind=true
}

TEST_CASE("decoder-tagged: auto_rewind after failed as_map", "[tagged]") {
    // {1:{a:1}, 2:{b:2}}
    dynamic_encoder enc;
    {
        auto m = enc.map();
        { auto inner = m[1].map(); inner["a"] = 1; }
        { auto inner = m[2].map(); inner["b"] = 2; }
    }
    auto data = enc.finish();

    decoder dec(data);
    dec.set_force_prefetch(false);
    auto m = dec.map();

    // Enter first sub-map
    {
        auto inner = m[1].as_map();
        CHECK(inner["a"].get_or(0) == 1);
    }
    // Enter second sub-map — auto_rewind ensures we can find key 2
    {
        auto inner = m[2].as_map();
        CHECK(inner["b"].get_or(0) == 2);
    }
    // Back to the first — still works with auto_rewind
    {
        auto inner = m[1].as_map();
        CHECK(inner["a"].get_or(0) == 1);
    }
}

TEST_CASE("decoder-tagged: auto_rewind off as_map error poison", "[tagged]") {
    dynamic_encoder enc;
    {
        auto m = enc.map();
        { auto inner = m[1].map(); inner["a"] = 1; }
        { auto inner = m[2].map(); inner["b"] = 2; }
    }
    auto data = enc.finish();

    decoder dec(data);
    dec.set_force_prefetch(false);
    dec.set_map_auto_rewind(false);
    auto m = dec.map();

    // Failed as_map leaves uLastError non-zero
    try { m[99].as_map(); } catch (...) {}
    // Next lookup sees the stale error
    CHECK(m[1].get_or(-3) == -3);  // would succeed with auto_rewind=true
}
