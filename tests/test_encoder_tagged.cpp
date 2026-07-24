/*
 * tests/test_encoder_tagged.cpp
 *
 * Tests for encoding tagged semantic types: date, bignum, decimal,
 * bigfloat, uri, b64, regex, mime, uuid, encoded insertion.
 */

#include <qcborpp/qcborpp.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace qcborpp;

TEST_CASE("dynamic_encoder: add_date_epoch as tag", "[dynamic_encoder][tagged]") {
    dynamic_encoder enc;
    enc.open_array();
    enc.add_date_epoch(1477263730, true);
    enc.close_array();
    auto data = enc.finish();
    REQUIRE(data.size() > 0);
}

TEST_CASE("dynamic_encoder: add_date_epoch borrowed", "[dynamic_encoder][tagged]") {
    dynamic_encoder enc;
    enc.open_array();
    enc.add_date_epoch(500000000, false);
    enc.close_array();
    auto data = enc.finish();
    REQUIRE(data.size() > 0);
}

TEST_CASE("dynamic_encoder: add_days_epoch", "[dynamic_encoder][tagged]") {
    dynamic_encoder enc;
    enc.open_array();
    enc.add_days_epoch(19000, true);
    enc.close_array();
    auto data = enc.finish();
    REQUIRE(data.size() > 0);
}

TEST_CASE("dynamic_encoder: add_date_string", "[dynamic_encoder][tagged]") {
    dynamic_encoder enc;
    enc.open_array();
    enc.add_date_string("2024-01-15T10:30:00Z", true);
    enc.close_array();
    auto data = enc.finish();
    REQUIRE(data.size() > 0);
}

TEST_CASE("dynamic_encoder: add_days_string", "[dynamic_encoder][tagged]") {
    dynamic_encoder enc;
    enc.open_array();
    enc.add_days_string("2024-01-15", true);
    enc.close_array();
    auto data = enc.finish();
    REQUIRE(data.size() > 0);
}

TEST_CASE("dynamic_encoder: add_bignum_positive", "[dynamic_encoder][tagged]") {
    uint8_t big[] = {0x01, 0x00, 0x00, 0x00};
    dynamic_encoder enc;
    enc.open_array();
    enc.add_bignum_positive({big, 4}, true);
    enc.close_array();
    auto data = enc.finish();
    REQUIRE(data.size() > 0);
}

TEST_CASE("dynamic_encoder: add_bignum_negative", "[dynamic_encoder][tagged]") {
    uint8_t big[] = {0x01, 0x00, 0x00, 0x00};
    dynamic_encoder enc;
    enc.open_array();
    enc.add_bignum_negative({big, 4}, true);
    enc.close_array();
    auto data = enc.finish();
    REQUIRE(data.size() > 0);
}

TEST_CASE("dynamic_encoder: add_decimal_fraction", "[dynamic_encoder][tagged]") {
    dynamic_encoder enc;
    enc.open_array();
    enc.add_decimal_fraction(314159, 5, true);
    enc.close_array();
    auto data = enc.finish();
    REQUIRE(data.size() > 0);
}

TEST_CASE("dynamic_encoder: add_decimal_fraction bignum", "[dynamic_encoder][tagged]") {
    uint8_t mant[] = {0x01, 0x02};
    dynamic_encoder enc;
    enc.open_array();
    enc.add_decimal_fraction_bignum({mant, 2}, false, 3, true);
    enc.close_array();
    auto data = enc.finish();
    REQUIRE(data.size() > 0);
}

TEST_CASE("dynamic_encoder: add_bigfloat", "[dynamic_encoder][tagged]") {
    dynamic_encoder enc;
    enc.open_array();
    enc.add_bigfloat(100, 2, true);
    enc.close_array();
    auto data = enc.finish();
    REQUIRE(data.size() > 0);
}

TEST_CASE("dynamic_encoder: add_bigfloat bignum", "[dynamic_encoder][tagged]") {
    uint8_t mant[] = {0x01, 0x02};
    dynamic_encoder enc;
    enc.open_array();
    enc.add_bigfloat_bignum({mant, 2}, false, 3, true);
    enc.close_array();
    auto data = enc.finish();
    REQUIRE(data.size() > 0);
}

TEST_CASE("dynamic_encoder: add_uri", "[dynamic_encoder][tagged]") {
    dynamic_encoder enc;
    enc.open_array();
    enc.add_uri("https://example.com", true);
    enc.close_array();
    auto data = enc.finish();
    REQUIRE(data.size() > 0);
}

TEST_CASE("dynamic_encoder: add_b64_text", "[dynamic_encoder][tagged]") {
    dynamic_encoder enc;
    enc.open_array();
    enc.add_b64_text("SGVsbG8=", true);
    enc.close_array();
    auto data = enc.finish();
    REQUIRE(data.size() > 0);
}

TEST_CASE("dynamic_encoder: add_b64url_text", "[dynamic_encoder][tagged]") {
    dynamic_encoder enc;
    enc.open_array();
    enc.add_b64url_text("SGVsbG8", true);
    enc.close_array();
    auto data = enc.finish();
    REQUIRE(data.size() > 0);
}

TEST_CASE("dynamic_encoder: add_regex", "[dynamic_encoder][tagged]") {
    dynamic_encoder enc;
    enc.open_array();
    enc.add_regex("[a-z]+", true);
    enc.close_array();
    auto data = enc.finish();
    REQUIRE(data.size() > 0);
}

TEST_CASE("dynamic_encoder: add_mime_data", "[dynamic_encoder][tagged]") {
    dynamic_encoder enc;
    enc.open_array();
    enc.add_mime_data("text/plain; charset=utf-8", true);
    enc.close_array();
    auto data = enc.finish();
    REQUIRE(data.size() > 0);
}

TEST_CASE("dynamic_encoder: add_binary_uuid", "[dynamic_encoder][tagged]") {
    uint8_t uuid[] = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16};
    dynamic_encoder enc;
    enc.open_array();
    enc.add_binary_uuid({uuid, 16}, true);
    enc.close_array();
    auto data = enc.finish();
    REQUIRE(data.size() > 0);
}

TEST_CASE("dynamic_encoder: tagged without tag (borrowed)", "[dynamic_encoder][tagged]") {
    // Borrowed content: same data without the CBOR tag wrapper
    dynamic_encoder enc;
    enc.open_array();
    enc.add_uri("https://example.com", false);
    enc.close_array();
    auto data = enc.finish();
    REQUIRE(data.size() > 0);
}

TEST_CASE("dynamic_encoder: add_encoded", "[dynamic_encoder][tagged]") {
    // Encode something first, then insert the encoded bytes into another dynamic_encoder
    dynamic_encoder inner;
    inner.open_array();
    inner.add_int64(1);
    inner.add_int64(2);
    inner.close_array();
    auto inner_data = inner.finish();

    dynamic_encoder outer;
    outer.open_array();
    outer.add_int64(0);
    outer.add_encoded(inner_data);
    outer.add_int64(3);
    outer.close_array();
    auto data = outer.finish();
    REQUIRE(data.size() > 0);
}
