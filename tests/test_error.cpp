/*
 * tests/test_error.cpp
 *
 * Tests for error handling, exceptions, and error_code mapping.
 */

#include <qcborpp/qcborpp.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace qcborpp;

TEST_CASE("error: qcborpp_category name", "[error]") {
    REQUIRE(std::string(qcborpp_category().name()) == "qcborpp");
}

TEST_CASE("error: make_error_code", "[error]") {
    auto ec = make_error_code(errc::success);
    REQUIRE(!ec);
    REQUIRE(ec.value() == 0);
    REQUIRE(ec.category() == qcborpp_category());
}

TEST_CASE("error: error exception message", "[error]") {
    error e(errc::buffer_too_small);
    REQUIRE(e.code() == errc::buffer_too_small);
    REQUIRE(std::string(e.what()).find("buffer too small") != std::string::npos);
}

TEST_CASE("error: all error codes have non-empty messages", "[error]") {
    for (uint8_t i = 0; i <= 78; ++i) {
        auto ec = make_error_code(static_cast<errc>(i));
        auto msg = ec.message();
        REQUIRE(!msg.empty());
    }
}

TEST_CASE("error: check_qcbor_err throws on non-zero", "[error]") {
    REQUIRE_THROWS_AS(check_qcbor_err(1), error);
    REQUIRE_NOTHROW(check_qcbor_err(0));
}

TEST_CASE("error: finish twice throws error", "[error]") {
    dynamic_encoder enc;
    enc.open_array();
    enc.close_array();
    enc.finish();
    REQUIRE_THROWS_AS(enc.finish(), error);
}

TEST_CASE("error: double map() throws", "[error]") {
    dynamic_encoder enc;
    enc.map();
    REQUIRE_THROWS_AS(enc.map(), error);
}

TEST_CASE("error: double array() throws", "[error]") {
    dynamic_encoder enc;
    enc.array();
    REQUIRE_THROWS_AS(enc.array(), error);
}

TEST_CASE("error: decoder finish with unconsumed returns error_code", "[error]") {
    dynamic_encoder enc;
    {
        auto m = enc.map();
        m["a"] = 1;
        m["b"] = 2;
    }
    auto data = enc.finish();

    decoder dec(data);
    auto m = dec.map();
    int64_t v = m["a"];
    REQUIRE(v == 1);
    auto ec = dec.finish();
    REQUIRE(ec != std::error_code{});
}

TEST_CASE("error: is_error_code_enum specialization", "[error]") {
    // This is a compile-time check - if it compiles, it works.
    std::error_code ec = errc::label_not_found;
    REQUIRE(ec == make_error_code(errc::label_not_found));
}

TEST_CASE("error: static_cast from QCBOR error to errc", "[error]") {
    // Verify mapping is correct
    REQUIRE(static_cast<int>(errc::success) == 0);
    REQUIRE(static_cast<int>(errc::buffer_too_small) == 1);
    REQUIRE(static_cast<int>(errc::hit_end) == 31);
    REQUIRE(static_cast<int>(errc::no_more_items) == 67);
    REQUIRE(static_cast<int>(errc::unexpected_type) == 61);
}
