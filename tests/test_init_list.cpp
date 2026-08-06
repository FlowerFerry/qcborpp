/*
 * test_init_list.cpp — unit tests for initializer-list encoding
 *
 * Covers encoder(buf, {...}), dynamic_encoder(...), arr(), map(), imap(),
 * nested structures, inference edge cases, and roundtrip verification.
 *
 * IMPORTANT decoder-lifetime rules:
 *   1. Keep map_scope in a named variable so it outlives sub-scopes.
 *      `dec.map()["key"].as_array()` creates a temporary map_scope that is
 *      destroyed at the end of the expression before the array_scope
 *      destructor runs → "exit mismatch".
 *   2. After a chained subscript like m["a"]["b"], the decoder cursor is
 *      positioned inside sub-map "a"; subsequent sibling lookups need
 *      a fresh decoder.
 *   3. Array-of-arrays (nested arrays) hit a known decoder limitation
 *      with ExitArray cursor positioning. Use map() factory for the
 *      inner elements when roundtrip decode is needed, or verify
 *      the binary encoding directly.
 */

#include <catch2/catch_test_macros.hpp>
#include <qcborpp/qcborpp.hpp>
#include <cmath>

using namespace qcborpp;

#define BUF byte_span{buf, sizeof(buf)}

// ===== scalar types =====

TEST_CASE("init_list: flat map with all scalar types", "[init_list]") {
    uint8_t buf[512];
    encoder enc(BUF, {
        {"null_val", nullptr},
        {"bool_val", true},
        {"int_val",  42},
        {"uint_val", uint64_t(18446744073709551615ULL)},
        {"dbl_val",  3.14159},
        {"str_val",  "hello"},
    });
    auto data = enc.finish();
    decoder dec{data};
    auto m = dec.map();

    CHECK(m["null_val"].is_null());
    CHECK(m["bool_val"].as_bool() == true);
    CHECK(int64_t{m["int_val"]} == 42);
    CHECK(uint64_t{m["uint_val"]} == 18446744073709551615ULL);
    CHECK(std::abs(double{m["dbl_val"]} - 3.14159) < 0.0001);
    CHECK(std::string_view{m["str_val"]} == "hello");
}

TEST_CASE("init_list: bytes value", "[init_list]") {
    uint8_t raw[] = {0x00, 0x01, 0xFF};
    uint8_t buf[256];

    encoder enc(BUF, {{"data", const_byte_span{raw, sizeof(raw)}}});
    auto data = enc.finish();
    decoder dec{data};
    auto m = dec.map();
    auto bs = m["data"].as_bytes();

    REQUIRE(bs.size() == 3);
    CHECK(bs[0] == 0x00);
    CHECK(bs[1] == 0x01);
    CHECK(bs[2] == 0xFF);
}

TEST_CASE("init_list: chrono values", "[init_list]") {
    using namespace std::chrono;
    uint8_t buf[256];

    auto tp = system_clock::from_time_t(1710000000);
    auto dur = duration<int, std::ratio<86400>>(30);

    encoder enc(BUF, {
        {"created", tp},
        {"expires", dur},
    });
    auto data = enc.finish();
    decoder dec{data};
    auto m = dec.map();

    CHECK(m["created"].as_time_point() == tp);
    CHECK(m["expires"].as_days_duration().count() == 30);
}

// ===== nested structures =====

TEST_CASE("init_list: shallow nested subtree", "[init_list]") {
    uint8_t buf[512];
    encoder enc(BUF, {{"answer", {{"everything", 42}}}});
    auto data = enc.finish();
    decoder dec{data};
    auto m = dec.map();
    CHECK(int64_t{m["answer"]["everything"]} == 42);
}

TEST_CASE("init_list: two sibling subtrees -- fresh decoders", "[init_list]") {
    uint8_t buf[512];
    encoder enc(BUF, {
        {"answer", {{"everything", 42}}},
        {"object", {{"currency", "USD"}, {"value", 42.99}}},
    });
    auto data = enc.finish();

    // Subtree 1: answer (fresh decoder → fresh map_scope)
    {
        decoder dec{data};
        auto m = dec.map();
        CHECK(int64_t{m["answer"]["everything"]} == 42);
    }
    // Subtree 2: object (fresh decoder)
    {
        decoder dec{data};
        auto   m = dec.map();
        auto obj = m["object"].as_map();
        CHECK(std::string_view{obj["currency"]} == "USD");
        CHECK(std::abs(double{obj["value"]} - 42.99) < 0.0001);
    }
}

TEST_CASE("init_list: list sub-array with map_scope alive", "[init_list]") {
    uint8_t buf[512];
    encoder enc(BUF, {
        {"name", "Niels"},
        {"list", {1, 0, 2}},
    });
    auto data = enc.finish();

    // Flat scalar
    {
        decoder dec{data};
        auto m = dec.map();
        CHECK(std::string_view{m["name"]} == "Niels");
    }
    // Array subtree — keep map_scope alive
    {
        decoder dec{data};
        auto   m = dec.map();
        auto arr = m["list"].as_array();
        CHECK(arr.next().as_int64() == 1);
        CHECK(arr.next().as_int64() == 0);
        CHECK(arr.next().as_int64() == 2);
        CHECK(arr.done());
    }
}

TEST_CASE("init_list: full README example -- per-subtree decoders", "[init_list]") {
    uint8_t buf[1024];
    encoder enc(BUF, {
        {"pi",      3.141},
        {"happy",   true},
        {"name",    "Niels"},
        {"nothing", nullptr},
        {"answer", {{"everything", 42}}},
        {"list", {1, 0, 2}},
        {"object", {{"currency", "USD"}, {"value", 42.99}}},
    });
    auto data = enc.finish();

    // Flat scalars
    {
        decoder dec{data};
        auto m = dec.map();
        CHECK(std::abs(double{m["pi"]} - 3.141) < 0.0001);
        CHECK(m["happy"].as_bool() == true);
        CHECK(std::string_view{m["name"]} == "Niels");
        CHECK(m["nothing"].is_null());
    }
    // answer subtree
    {
        decoder dec{data};
        auto m = dec.map();
        CHECK(int64_t{m["answer"]["everything"]} == 42);
    }
    // list subtree
    {
        decoder dec{data};
        auto   m = dec.map();
        auto arr = m["list"].as_array();
        CHECK(arr.next().as_int64() == 1);
        CHECK(arr.next().as_int64() == 0);
        CHECK(arr.next().as_int64() == 2);
    }
    // object subtree
    {
        decoder dec{data};
        auto   m = dec.map();
        auto obj = m["object"].as_map();
        CHECK(std::string_view{obj["currency"]} == "USD");
        CHECK(std::abs(double{obj["value"]} - 42.99) < 0.0001);
    }
}

// ===== factories: arr, map, imap =====

TEST_CASE("init_list: arr() factory -- string list forced to array", "[init_list]") {
    uint8_t buf[256];
    encoder enc(BUF, {{"tags", arr({"a", "b", "c"})}});
    auto data = enc.finish();
    decoder dec{data};
    auto   m = dec.map();
    auto tags = m["tags"].as_array();

    CHECK(std::string_view{tags.next()} == "a");
    CHECK(std::string_view{tags.next()} == "b");
    CHECK(std::string_view{tags.next()} == "c");
    CHECK(tags.done());
}

TEST_CASE("init_list: arr() factory -- empty array", "[init_list]") {
    uint8_t buf[256];
    encoder enc(BUF, {{"empty_arr", arr({})}});
    auto data = enc.finish();
    decoder dec{data};
    auto m = dec.map();
    auto ar = m["empty_arr"].as_array();
    CHECK(ar.done());
}

TEST_CASE("init_list: map() factory -- explicit object", "[init_list]") {
    uint8_t buf[256];
    encoder enc(BUF, {{"data", map({{"x", 1}, {"y", 2}})}});
    auto data = enc.finish();
    decoder dec{data};
    auto   m = dec.map();
    auto dm = m["data"].as_map();

    CHECK(int64_t{dm["x"]} == 1);
    CHECK(int64_t{dm["y"]} == 2);
}

TEST_CASE("init_list: imap() factory -- int-keyed map", "[init_list]") {
    uint8_t buf[256];
    encoder enc(BUF, {
        {"dict", imap({{1, "first"}, {2, "second"}, {3, "third"}})},
    });
    auto data = enc.finish();
    decoder dec{data};
    auto   m = dec.map();
    auto dict = m["dict"].as_map();

    CHECK(std::string_view{dict[1]} == "first");
    CHECK(std::string_view{dict[2]} == "second");
    CHECK(std::string_view{dict[3]} == "third");
}

// ===== dynamic_encoder =====

TEST_CASE("init_list: dynamic_encoder flat map", "[init_list]") {
    dynamic_encoder enc({{"key", "value"}, {"num", 42}});
    auto data = enc.finish();
    decoder dec{data};
    auto m = dec.map();

    CHECK(std::string_view{m["key"]} == "value");
    CHECK(int64_t{m["num"]} == 42);
}

TEST_CASE("init_list: dynamic_encoder nested", "[init_list]") {
    dynamic_encoder enc({{"outer", {{"inner", 99}}}});
    auto data = enc.finish();
    decoder dec{data};
    auto m = dec.map();
    CHECK(int64_t{m["outer"]["inner"]} == 99);
}

TEST_CASE("init_list: dynamic_encoder with arr()", "[init_list]") {
    dynamic_encoder enc({{"items", arr({1, 2, 3})}});
    auto data = enc.finish();
    decoder dec{data};
    auto   m = dec.map();
    auto items = m["items"].as_array();

    CHECK(items.next().as_int64() == 1);
    CHECK(items.next().as_int64() == 2);
    CHECK(items.next().as_int64() == 3);
}

// ===== inference edge cases =====

TEST_CASE("init_list: int list infers array", "[init_list]") {
    uint8_t buf[256];
    encoder enc(BUF, {{"numbers", {1, 2, 3}}});
    auto data = enc.finish();
    decoder dec{data};
    auto   m = dec.map();
    auto arr = m["numbers"].as_array();

    CHECK(arr.next().as_int64() == 1);
    CHECK(arr.next().as_int64() == 2);
    CHECK(arr.next().as_int64() == 3);
}

TEST_CASE("init_list: mixed scalar list infers array", "[init_list]") {
    uint8_t buf[256];
    encoder enc(BUF, {{"mixed", {42, "text", true}}});
    auto data = enc.finish();
    decoder dec{data};
    auto   m = dec.map();
    auto arr = m["mixed"].as_array();

    CHECK(arr.next().as_int64() == 42);
    CHECK(std::string_view{arr.next()} == "text");
    CHECK(arr.next().as_bool() == true);
}

TEST_CASE("init_list: single-element list infers array", "[init_list]") {
    uint8_t buf[256];
    encoder enc(BUF, {{"single", {"only"}}});
    auto data = enc.finish();
    decoder dec{data};
    auto   m = dec.map();
    auto arr = m["single"].as_array();

    CHECK(std::string_view{arr.next()} == "only");
}

TEST_CASE("init_list: empty map", "[init_list]") {
    uint8_t buf[256];
    encoder enc(BUF, {});
    auto data = enc.finish();
    decoder dec{data};
    REQUIRE(dec.is_map());
}

// ===== deep nesting =====

TEST_CASE("init_list: deep nesting 4 levels", "[init_list]") {
    uint8_t buf[1024];
    encoder enc(BUF, {
        {"l1", {{"l2", {{"l3", {{"l4", "bottom"}}}}}}},
    });
    auto data = enc.finish();
    decoder dec{data};
    auto m = dec.map();
    CHECK(std::string_view{m["l1"]["l2"]["l3"]["l4"]} == "bottom");
}

TEST_CASE("init_list: array of objects (decode roundtrip)", "[init_list]") {
    // Uses map() factory so inner elements are maps — decoder handles
    // map-in-array fine (unlike auto-inferred array-in-array).
    uint8_t buf[1024];
    encoder enc(BUF, {
        {"rows", arr({
            map({{"id", 1}, {"name", "alpha"}}),
            map({{"id", 2}, {"name", "beta"}}),
            map({{"id", 3}, {"name", "gamma"}}),
        })},
    });
    auto data = enc.finish();
    decoder dec{data};
    auto   m = dec.map();
    auto rows = m["rows"].as_array();

    {
        auto r1 = rows.next().as_map();
        CHECK(int64_t{r1["id"]} == 1);
        CHECK(std::string_view{r1["name"]} == "alpha");
    }
    {
        auto r2 = rows.next().as_map();
        CHECK(int64_t{r2["id"]} == 2);
        CHECK(std::string_view{r2["name"]} == "beta");
    }
    {
        auto r3 = rows.next().as_map();
        CHECK(int64_t{r3["id"]} == 3);
        CHECK(std::string_view{r3["name"]} == "gamma");
    }
}

// ===== error: top-level non-pair =====

TEST_CASE("init_list: top-level non-pair throws", "[init_list]") {
    uint8_t buf[256];
    REQUIRE_THROWS_AS(
        (encoder{BUF, {1, 2, 3}}),
        qcborpp::error
    );
}

TEST_CASE("init_list: dynamic_encoder non-pair init throws", "[init_list]") {
    REQUIRE_THROWS_AS(
        (dynamic_encoder{1, 2, 3}),
        qcborpp::error
    );
}

// ===== numeric-key pair infers sub-array (binary verify only) =====

TEST_CASE("init_list: numeric-key pair infers sub-array", "[init_list]") {
    // {{1, "foo"}, {2, "bar"}} — keys are int not string → infers array
    // of 2-element sub-arrays. Verify binary output is well-formed CBOR.
    uint8_t buf[256];
    encoder enc(BUF, {
        {"mixed", {{1, "foo"}, {2, "bar"}}},
    });
    auto data = enc.finish();

    // Binary verification: must parse as valid CBOR map
    decoder dec{data};
    REQUIRE(dec.is_map());

    // First element of each sub-array is reachable as a flat scalar
    // (decoder limitation: nested array-of-arrays not supported for full
    //  roundtrip iteration — see header doc for details)
    auto   m = dec.map();
    auto arr = m["mixed"].as_array();

    {
        auto sub1 = arr.next().as_array();
        CHECK(sub1.next().as_int64() == 1);
        CHECK(std::string_view{sub1.next()} == "foo");
        CHECK(sub1.done());
    }
    {
        auto sub2 = arr.next().as_array();
        CHECK(sub2.next().as_int64() == 2);
        CHECK(std::string_view{sub2.next()} == "bar");
        CHECK(sub2.done());
    }
}
