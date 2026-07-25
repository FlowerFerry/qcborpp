/*
 * tests/test_cross_validation.cpp
 *
 * Cross-validation: QCBOR C library ↔ qcborpp encode/decode.
 * Verifies correctness through a 4-way matrix:
 *   1. QCBOR C encode → qcborpp decode (value match)
 *   2. qcborpp encoder (static) encode → QCBOR C decode (value match)
 *   3. qcborpp dynamic_encoder encode → QCBOR C decode (value match)
 *   4. qcborpp encoder encode → re-encode → binary compare (zero-copy fidelity)
 *
 * Run with: qcborpp_tests "[cross_validation]"
 */

#include <qcborpp/qcborpp.hpp>

// Direct QCBOR C API access (already linked via qcbor target)
#include "qcbor/qcbor_encode.h"
#include "qcbor/qcbor_decode.h"
#include "qcbor/qcbor_spiffy_decode.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <vector>
#include <cstring>
#include <cmath>

using namespace qcborpp;

// ══════════════════════════════════════════════════════════════════════════
// Helpers
// ══════════════════════════════════════════════════════════════════════════

namespace {

/* Encode data with raw QCBOR C, return the encoded bytes. */
std::vector<uint8_t> qcbor_encode_helper(bool as_map,
    const std::function<void(QCBOREncodeContext*)>& populate)
{
    // Phase 1: calculate size
    QCBOREncodeContext calc_ctx;
    QCBOREncode_Init(&calc_ctx, UsefulBuf{nullptr, SIZE_MAX});
    populate(&calc_ctx);
    size_t needed;
    QCBORError err = QCBOREncode_FinishGetSize(&calc_ctx, &needed);
    if (err != QCBOR_SUCCESS)
        throw qcborpp::error(static_cast<qcborpp::errc>(err));

    // Phase 2: actual encode
    std::vector<uint8_t> buf(needed);
    QCBOREncodeContext real_ctx;
    QCBOREncode_Init(&real_ctx, UsefulBuf{buf.data(), buf.size()});
    populate(&real_ctx);
    UsefulBufC result;
    err = QCBOREncode_Finish(&real_ctx, &result);
    if (err != QCBOR_SUCCESS)
        throw qcborpp::error(static_cast<qcborpp::errc>(err));
    buf.resize(result.len);
    return buf;
}

/* Run QCBOR C decoder over bytes, invoke callback per item. */
void qcbor_decode_walk(const_byte_span data,
    const std::function<void(const QCBORItem&, int depth)>& cb)
{
    QCBORDecodeContext ctx;
    QCBORDecode_Init(&ctx, UsefulBufC{data.data(), data.size()}, QCBOR_DECODE_MODE_NORMAL);

    QCBORItem item;
    int depth = 0;
    while (true) {
        QCBORError err = QCBORDecode_GetNext(&ctx, &item);
        if (err == QCBOR_ERR_NO_MORE_ITEMS) break;
        if (err != QCBOR_SUCCESS)
            throw qcborpp::error(static_cast<qcborpp::errc>(err));

        cb(item, depth);

        // Track depth from uNextNestLevel
        depth = item.uNextNestLevel;
    }
    QCBORDecode_Finish(&ctx);
}

} // anonymous namespace

// ══════════════════════════════════════════════════════════════════════════
// Direction 1: QCBOR C encode → qcborpp decode
// ══════════════════════════════════════════════════════════════════════════

TEST_CASE("cross_val: qcbor-c→qcborpp int64", "[cross_validation]") {
    auto bytes = qcbor_encode_helper(false, [](QCBOREncodeContext* c) {
        QCBOREncode_OpenArray(c);
        QCBOREncode_AddInt64(c, -123);
        QCBOREncode_AddInt64(c, 0);
        QCBOREncode_AddInt64(c, 456789);
        QCBOREncode_CloseArray(c);
    });

    decoder dec(const_byte_span{bytes.data(), bytes.size()});
    auto a = dec.array();
    REQUIRE(int64_t(a.next()) == -123);
    REQUIRE(int64_t(a.next()) == 0);
    REQUIRE(int64_t(a.next()) == 456789);
    dec.finish();
}

TEST_CASE("cross_val: qcbor-c→qcborpp uint64", "[cross_validation]") {
    auto bytes = qcbor_encode_helper(false, [](QCBOREncodeContext* c) {
        QCBOREncode_OpenArray(c);
        QCBOREncode_AddUInt64(c, 100);
        QCBOREncode_AddUInt64(c, 10000000000ULL);
        QCBOREncode_CloseArray(c);
    });

    decoder dec(const_byte_span{bytes.data(), bytes.size()});
    auto a = dec.array();
    REQUIRE(int64_t(a.next()) == 100);
    REQUIRE(int64_t(a.next()) == 10000000000ULL);
    dec.finish();
}

TEST_CASE("cross_val: qcbor-c→qcborpp text", "[cross_validation]") {
    auto bytes = qcbor_encode_helper(false, [](QCBOREncodeContext* c) {
        QCBOREncode_OpenArray(c);
        QCBOREncode_AddSZString(c, "hello");
        QCBOREncode_AddSZString(c, "world");
        QCBOREncode_CloseArray(c);
    });

    decoder dec(const_byte_span{bytes.data(), bytes.size()});
    auto a = dec.array();
    REQUIRE(std::string_view(a.next()) == "hello");
    REQUIRE(std::string_view(a.next()) == "world");
    dec.finish();
}

TEST_CASE("cross_val: qcbor-c→qcborpp bytes", "[cross_validation]") {
    uint8_t raw[] = {0x00, 0xFF, 0xAB};
    auto bytes = qcbor_encode_helper(false, [&](QCBOREncodeContext* c) {
        QCBOREncode_OpenArray(c);
        QCBOREncode_AddBytes(c, UsefulBufC{raw, sizeof(raw)});
        QCBOREncode_CloseArray(c);
    });

    decoder dec(const_byte_span{bytes.data(), bytes.size()});
    auto a = dec.array();
    auto bs = a.next().as_bytes();
    REQUIRE(bs.size() == 3);
    REQUIRE(bs[0] == 0x00);
    REQUIRE(bs[1] == 0xFF);
    REQUIRE(bs[2] == 0xAB);
    dec.finish();
}

TEST_CASE("cross_val: qcbor-c→qcborpp double", "[cross_validation]") {
    auto bytes = qcbor_encode_helper(false, [](QCBOREncodeContext* c) {
        QCBOREncode_OpenArray(c);
        QCBOREncode_AddDouble(c, 3.14);
        QCBOREncode_AddDouble(c, -1.5);
        QCBOREncode_CloseArray(c);
    });

    decoder dec(const_byte_span{bytes.data(), bytes.size()});
    auto a = dec.array();
    REQUIRE_THAT(double(a.next()), Catch::Matchers::WithinRel(3.14, 1e-9));
    REQUIRE_THAT(double(a.next()), Catch::Matchers::WithinRel(-1.5, 1e-9));
    dec.finish();
}

TEST_CASE("cross_val: qcbor-c→qcborpp bool", "[cross_validation]") {
    auto bytes = qcbor_encode_helper(false, [](QCBOREncodeContext* c) {
        QCBOREncode_OpenArray(c);
        QCBOREncode_AddBool(c, true);
        QCBOREncode_AddBool(c, false);
        QCBOREncode_CloseArray(c);
    });

    decoder dec(const_byte_span{bytes.data(), bytes.size()});
    auto a = dec.array();
    REQUIRE(bool(a.next()) == true);
    REQUIRE(bool(a.next()) == false);
    dec.finish();
}

TEST_CASE("cross_val: qcbor-c→qcborpp null", "[cross_validation]") {
    auto bytes = qcbor_encode_helper(false, [](QCBOREncodeContext* c) {
        QCBOREncode_OpenArray(c);
        QCBOREncode_AddNULL(c);
        QCBOREncode_CloseArray(c);
    });

    decoder dec(const_byte_span{bytes.data(), bytes.size()});
    auto a = dec.array();
    REQUIRE(a.next().is_null());
    dec.finish();
}

TEST_CASE("cross_val: qcbor-c→qcborpp map", "[cross_validation]") {
    auto bytes = qcbor_encode_helper(false, [](QCBOREncodeContext* c) {
        QCBOREncode_OpenMap(c);
        QCBOREncode_AddSZString(c, "name");
        QCBOREncode_AddSZString(c, "Alice");
        QCBOREncode_AddSZString(c, "age");
        QCBOREncode_AddInt64(c, 30);
        QCBOREncode_CloseMap(c);
    });

    decoder dec(const_byte_span{bytes.data(), bytes.size()});
    auto m = dec.map();
    REQUIRE(std::string_view(m["name"]) == "Alice");
    REQUIRE(int64_t(m["age"]) == 30);
    dec.finish();
}

TEST_CASE("cross_val: qcbor-c→qcborpp nested map/array", "[cross_validation]") {
    auto bytes = qcbor_encode_helper(false, [](QCBOREncodeContext* c) {
        QCBOREncode_OpenMap(c);
        QCBOREncode_AddSZString(c, "items");
        QCBOREncode_OpenArray(c);
        QCBOREncode_AddInt64(c, 1);
        QCBOREncode_AddInt64(c, 2);
        QCBOREncode_AddInt64(c, 3);
        QCBOREncode_CloseArray(c);
        QCBOREncode_CloseMap(c);
    });

    decoder dec(const_byte_span{bytes.data(), bytes.size()});
    auto m = dec.map();
    auto arr = m["items"].as_array();
    REQUIRE(int64_t(arr.next()) == 1);
    REQUIRE(int64_t(arr.next()) == 2);
    REQUIRE(int64_t(arr.next()) == 3);
    dec.finish();
}

// ══════════════════════════════════════════════════════════════════════════
// Direction 2: qcborpp static encoder → QCBOR C decode
// ══════════════════════════════════════════════════════════════════════════

TEST_CASE("cross_val: qcborpp-static→qcbor-c int64", "[cross_validation]") {
    uint8_t buf[256];
    encoder enc(byte_span{buf, sizeof(buf)});
    enc.open_array();
    enc.add_int64(-123);
    enc.add_int64(0);
    enc.add_int64(456789);
    enc.close_array();
    auto data = enc.finish();

    std::vector<int64_t> values;
    qcbor_decode_walk(data, [&](const QCBORItem& item, int) {
        if (item.uDataType == QCBOR_TYPE_INT64)
            values.push_back(item.val.int64);
    });
    REQUIRE(values.size() == 3);
    REQUIRE(values[0] == -123);
    REQUIRE(values[1] == 0);
    REQUIRE(values[2] == 456789);
}

TEST_CASE("cross_val: qcborpp-static→qcbor-c uint64", "[cross_validation]") {
    uint8_t buf[256];
    encoder enc(byte_span{buf, sizeof(buf)});
    enc.open_array();
    enc.add_uint64(100);
    enc.add_uint64(10000000000ULL);
    enc.close_array();
    auto data = enc.finish();

    std::vector<uint64_t> values;
    qcbor_decode_walk(data, [&](const QCBORItem& item, int) {
        if (item.uDataType == QCBOR_TYPE_UINT64)
            values.push_back(item.val.uint64);
        else if (item.uDataType == QCBOR_TYPE_INT64)
            values.push_back(static_cast<uint64_t>(item.val.int64));
    });
    REQUIRE(values.size() == 2);
    REQUIRE(values[0] == 100);
    REQUIRE(values[1] == 10000000000ULL);
}

TEST_CASE("cross_val: qcborpp-static→qcbor-c text", "[cross_validation]") {
    uint8_t buf[256];
    encoder enc(byte_span{buf, sizeof(buf)});
    enc.open_array();
    enc.add_text("hello");
    enc.add_text("world");
    enc.close_array();
    auto data = enc.finish();

    std::vector<std::string> values;
    qcbor_decode_walk(data, [&](const QCBORItem& item, int) {
        if (item.uDataType == QCBOR_TYPE_TEXT_STRING) {
            values.emplace_back(
                static_cast<const char*>(item.val.string.ptr),
                item.val.string.len);
        }
    });
    REQUIRE(values.size() == 2);
    REQUIRE(values[0] == "hello");
    REQUIRE(values[1] == "world");
}

TEST_CASE("cross_val: qcborpp-static→qcbor-c bytes", "[cross_validation]") {
    uint8_t buf[256];
    uint8_t raw[] = {0x00, 0xFF, 0xAB};
    encoder enc(byte_span{buf, sizeof(buf)});
    enc.open_array();
    enc.add_bytes(const_byte_span{raw, sizeof(raw)});
    enc.close_array();
    auto data = enc.finish();

    bool found = false;
    qcbor_decode_walk(data, [&](const QCBORItem& item, int) {
        if (item.uDataType == QCBOR_TYPE_BYTE_STRING) {
            REQUIRE(item.val.string.len == 3);
            REQUIRE(static_cast<const uint8_t*>(item.val.string.ptr)[0] == 0x00);
            REQUIRE(static_cast<const uint8_t*>(item.val.string.ptr)[1] == 0xFF);
            REQUIRE(static_cast<const uint8_t*>(item.val.string.ptr)[2] == 0xAB);
            found = true;
        }
    });
    REQUIRE(found);
}

TEST_CASE("cross_val: qcborpp-static→qcbor-c double", "[cross_validation]") {
    uint8_t buf[256];
    encoder enc(byte_span{buf, sizeof(buf)});
    enc.open_array();
    enc.add_double(3.14);
    enc.add_double(-1.5);
    enc.close_array();
    auto data = enc.finish();

    std::vector<double> values;
    qcbor_decode_walk(data, [&](const QCBORItem& item, int) {
        if (item.uDataType == QCBOR_TYPE_DOUBLE)
            values.push_back(item.val.dfnum);
    });
    REQUIRE(values.size() == 2);
    REQUIRE_THAT(values[0], Catch::Matchers::WithinRel(3.14, 1e-9));
    REQUIRE_THAT(values[1], Catch::Matchers::WithinRel(-1.5, 1e-9));
}

TEST_CASE("cross_val: qcborpp-static→qcbor-c bool", "[cross_validation]") {
    uint8_t buf[256];
    encoder enc(byte_span{buf, sizeof(buf)});
    enc.open_array();
    enc.add_bool(true);
    enc.add_bool(false);
    enc.close_array();
    auto data = enc.finish();

    std::vector<int> types; // QCBOR_TYPE_TRUE=21, QCBOR_TYPE_FALSE=20
    qcbor_decode_walk(data, [&](const QCBORItem& item, int) {
        if (item.uDataType == QCBOR_TYPE_TRUE || item.uDataType == QCBOR_TYPE_FALSE)
            types.push_back(item.uDataType);
    });
    REQUIRE(types.size() == 2);
    REQUIRE(types[0] == QCBOR_TYPE_TRUE);
    REQUIRE(types[1] == QCBOR_TYPE_FALSE);
}

TEST_CASE("cross_val: qcborpp-static→qcbor-c null", "[cross_validation]") {
    uint8_t buf[256];
    encoder enc(byte_span{buf, sizeof(buf)});
    enc.open_array();
    enc.add_null();
    enc.close_array();
    auto data = enc.finish();

    bool found = false;
    qcbor_decode_walk(data, [&](const QCBORItem& item, int) {
        if (item.uDataType == QCBOR_TYPE_NULL) found = true;
    });
    REQUIRE(found);
}

TEST_CASE("cross_val: qcborpp-static→qcbor-c map", "[cross_validation]") {
    uint8_t buf[512];
    encoder enc(byte_span{buf, sizeof(buf)});
    {
        auto m = enc.map();
        m["name"] = "Alice";
        m["age"]  = 30;
    }
    auto data = enc.finish();

    std::string name;
    int64_t age = -1;
    qcbor_decode_walk(data, [&](const QCBORItem& item, int) {
        if (item.uDataType == QCBOR_TYPE_TEXT_STRING &&
            item.label.string.len == 4 && std::memcmp(item.label.string.ptr, "name", 4) == 0) {
            name.assign(static_cast<const char*>(item.val.string.ptr), item.val.string.len);
        }
        if (item.uDataType == QCBOR_TYPE_INT64 &&
            item.label.string.len == 3 && std::memcmp(item.label.string.ptr, "age", 3) == 0) {
            age = item.val.int64;
        }
    });
    REQUIRE(name == "Alice");
    REQUIRE(age == 30);
}

// ══════════════════════════════════════════════════════════════════════════
// Direction 3: qcborpp dynamic_encoder → QCBOR C decode
// ══════════════════════════════════════════════════════════════════════════

TEST_CASE("cross_val: qcborpp-dynamic→qcbor-c int64", "[cross_validation]") {
    dynamic_encoder enc;
    enc.open_array();
    enc.add_int64(-123);
    enc.add_int64(0);
    enc.add_int64(456789);
    enc.close_array();
    auto data = enc.finish();

    std::vector<int64_t> values;
    qcbor_decode_walk(data, [&](const QCBORItem& item, int) {
        if (item.uDataType == QCBOR_TYPE_INT64)
            values.push_back(item.val.int64);
    });
    REQUIRE(values.size() == 3);
    REQUIRE(values[0] == -123);
    REQUIRE(values[1] == 0);
    REQUIRE(values[2] == 456789);
}

TEST_CASE("cross_val: qcborpp-dynamic→qcbor-c text", "[cross_validation]") {
    dynamic_encoder enc;
    enc.open_array();
    enc.add_text("dynamic");
    enc.add_text("test");
    enc.close_array();
    auto data = enc.finish();

    std::vector<std::string> values;
    qcbor_decode_walk(data, [&](const QCBORItem& item, int) {
        if (item.uDataType == QCBOR_TYPE_TEXT_STRING) {
            values.emplace_back(
                static_cast<const char*>(item.val.string.ptr),
                item.val.string.len);
        }
    });
    REQUIRE(values.size() == 2);
    REQUIRE(values[0] == "dynamic");
    REQUIRE(values[1] == "test");
}

TEST_CASE("cross_val: qcborpp-dynamic→qcbor-c map", "[cross_validation]") {
    dynamic_encoder enc;
    {
        auto m = enc.map();
        m["key"]  = "value";
        m["num"]  = 42;
    }
    auto data = enc.finish();

    std::string val;
    int64_t num = -1;
    qcbor_decode_walk(data, [&](const QCBORItem& item, int) {
        if (item.uDataType == QCBOR_TYPE_TEXT_STRING &&
            item.label.string.len == 3 && std::memcmp(item.label.string.ptr, "key", 3) == 0) {
            val.assign(static_cast<const char*>(item.val.string.ptr), item.val.string.len);
        }
        if (item.uDataType == QCBOR_TYPE_INT64 &&
            item.label.string.len == 3 && std::memcmp(item.label.string.ptr, "num", 3) == 0) {
            num = item.val.int64;
        }
    });
    REQUIRE(val == "value");
    REQUIRE(num == 42);
}

TEST_CASE("cross_val: qcborpp-dynamic→qcbor-c double", "[cross_validation]") {
    dynamic_encoder enc;
    enc.open_array();
    enc.add_double(2.718);
    enc.close_array();
    auto data = enc.finish();

    double val = 0;
    qcbor_decode_walk(data, [&](const QCBORItem& item, int) {
        if (item.uDataType == QCBOR_TYPE_DOUBLE)
            val = item.val.dfnum;
    });
    REQUIRE_THAT(val, Catch::Matchers::WithinRel(2.718, 1e-9));
}

// ══════════════════════════════════════════════════════════════════════════
// Direction 4: qcborpp encoder → qcborpp encoder re-encode → binary compare
// ══════════════════════════════════════════════════════════════════════════

TEST_CASE("cross_val: qcborpp→qcborpp re-encode binary match", "[cross_validation]") {
    // Encode with static encoder
    uint8_t buf1[512];
    encoder enc1(byte_span{buf1, sizeof(buf1)});
    {
        auto m = enc1.map();
        m["int"]    = 42;
        m["text"]   = "hello";
        m["double"] = 3.14;
        m["bool"]   = true;
    }
    auto original = enc1.finish();

    // Decode with qcborpp decoder
    decoder dec(original);
    auto m = dec.map();
    int64_t   v_int    = int64_t(m["int"]);
    auto      v_text   = std::string(std::string_view(m["text"]));
    double    v_double = double(m["double"]);
    bool      v_bool   = bool(m["bool"]);
    dec.finish();

    // Re-encode with static encoder
    uint8_t buf2[512];
    encoder enc2(byte_span{buf2, sizeof(buf2)});
    {
        auto m2 = enc2.map();
        m2["int"]    = v_int;
        m2["text"]   = v_text;
        m2["double"] = v_double;
        m2["bool"]   = v_bool;
    }
    auto re_encoded = enc2.finish();

    // Binary compare
    REQUIRE(original.size() == re_encoded.size());
    REQUIRE(std::memcmp(original.data(), re_encoded.data(), original.size()) == 0);
}

TEST_CASE("cross_val: qcborpp→qcborpp re-encode array match", "[cross_validation]") {
    uint8_t buf1[256];
    encoder enc1(byte_span{buf1, sizeof(buf1)});
    {
        auto a = enc1.array();
        a << 10 << "text" << true << nullptr;
    }
    auto original = enc1.finish();

    decoder dec(original);
    auto a = dec.array();
    int64_t v1 = int64_t(a.next());
    auto v2 = std::string(std::string_view(a.next()));
    bool v3 = bool(a.next());
    bool v4null = a.next().is_null();
    REQUIRE(v4null);
    dec.finish();

    uint8_t buf2[256];
    encoder enc2(byte_span{buf2, sizeof(buf2)});
    {
        auto a2 = enc2.array();
        a2 << v1 << v2 << v3 << nullptr;
    }
    auto re_encoded = enc2.finish();

    REQUIRE(original.size() == re_encoded.size());
    REQUIRE(std::memcmp(original.data(), re_encoded.data(), original.size()) == 0);
}

TEST_CASE("cross_val: qcborpp→qcborpp re-encode nested match", "[cross_validation]") {
    uint8_t buf1[512];
    encoder enc1(byte_span{buf1, sizeof(buf1)});
    {
        auto m = enc1.map();
        auto inner = m["nested"].map();
        inner["x"] = 1;
        inner["y"] = 2;
    }
    auto original = enc1.finish();

    decoder dec(original);
    auto m = dec.map();
    auto inner = m["nested"].as_map();
    int64_t x = int64_t(inner["x"]);
    int64_t y = int64_t(inner["y"]);
    dec.finish();

    uint8_t buf2[512];
    encoder enc2(byte_span{buf2, sizeof(buf2)});
    {
        auto m2 = enc2.map();
        auto inner2 = m2["nested"].map();
        inner2["x"] = x;
        inner2["y"] = y;
    }
    auto re_encoded = enc2.finish();

    REQUIRE(original.size() == re_encoded.size());
    REQUIRE(std::memcmp(original.data(), re_encoded.data(), original.size()) == 0);
}

TEST_CASE("cross_val: qcborpp→qcborpp re-encode tagged value match", "[cross_validation]") {
    uint8_t buf1[256];
    encoder enc1(byte_span{buf1, sizeof(buf1)});
    {
        auto m = enc1.map();
        enc1.add_tag(1);
        m["epoch"] = 1234567890;
    }
    auto original = enc1.finish();

    decoder dec(original);
    auto m = dec.map();
    int64_t epoch = int64_t(m["epoch"]);
    dec.finish();

    uint8_t buf2[256];
    encoder enc2(byte_span{buf2, sizeof(buf2)});
    {
        auto m2 = enc2.map();
        enc2.add_tag(1);
        m2["epoch"] = epoch;
    }
    auto re_encoded = enc2.finish();

    REQUIRE(original.size() == re_encoded.size());
    REQUIRE(std::memcmp(original.data(), re_encoded.data(), original.size()) == 0);
}

// ══════════════════════════════════════════════════════════════════════════
// Known binary vectors (RFC 8949 Appendix A)
// ══════════════════════════════════════════════════════════════════════════

TEST_CASE("cross_val: rfc8949 known binary - int 0", "[cross_validation]") {
    // 0 is encoded as 0x00
    uint8_t buf[256];
    encoder enc(byte_span{buf, sizeof(buf)});
    enc.add_int64(0);
    auto data = enc.finish();
    REQUIRE(data.size() == 1);
    REQUIRE(data[0] == 0x00);
}

TEST_CASE("cross_val: rfc8949 known binary - int 1", "[cross_validation]") {
    // 1 is encoded as 0x01
    uint8_t buf[256];
    encoder enc(byte_span{buf, sizeof(buf)});
    enc.add_int64(1);
    auto data = enc.finish();
    REQUIRE(data.size() == 1);
    REQUIRE(data[0] == 0x01);
}

TEST_CASE("cross_val: rfc8949 known binary - int 23", "[cross_validation]") {
    // 23 is encoded as 0x17
    uint8_t buf[256];
    encoder enc(byte_span{buf, sizeof(buf)});
    enc.add_int64(23);
    auto data = enc.finish();
    REQUIRE(data.size() == 1);
    REQUIRE(data[0] == 0x17);
}

TEST_CASE("cross_val: rfc8949 known binary - int 24", "[cross_validation]") {
    // 24 is encoded as 0x18 0x18
    uint8_t buf[256];
    encoder enc(byte_span{buf, sizeof(buf)});
    enc.add_int64(24);
    auto data = enc.finish();
    REQUIRE(data.size() == 2);
    REQUIRE(data[0] == 0x18);
    REQUIRE(data[1] == 0x18);
}

TEST_CASE("cross_val: rfc8949 known binary - text 'a'", "[cross_validation]") {
    // "a" → 0x61 0x61
    uint8_t buf[256];
    encoder enc(byte_span{buf, sizeof(buf)});
    enc.add_text("a");
    auto data = enc.finish();
    REQUIRE(data.size() == 2);
    REQUIRE(data[0] == 0x61);
    REQUIRE(data[1] == 'a');
}

TEST_CASE("cross_val: rfc8949 known binary - array [1,2,3]", "[cross_validation]") {
    // [1, 2, 3] → 0x83 0x01 0x02 0x03
    uint8_t buf[256];
    encoder enc(byte_span{buf, sizeof(buf)});
    enc.open_array();
    enc.add_int64(1);
    enc.add_int64(2);
    enc.add_int64(3);
    enc.close_array();
    auto data = enc.finish();
    REQUIRE(data.size() == 4);
    REQUIRE(data[0] == 0x83);
    REQUIRE(data[1] == 0x01);
    REQUIRE(data[2] == 0x02);
    REQUIRE(data[3] == 0x03);
}

TEST_CASE("cross_val: rfc8949 known binary - false", "[cross_validation]") {
    // false → 0xf4
    uint8_t buf[256];
    encoder enc(byte_span{buf, sizeof(buf)});
    enc.add_bool(false);
    auto data = enc.finish();
    REQUIRE(data.size() == 1);
    REQUIRE(data[0] == 0xf4);
}

TEST_CASE("cross_val: rfc8949 known binary - true", "[cross_validation]") {
    // true → 0xf5
    uint8_t buf[256];
    encoder enc(byte_span{buf, sizeof(buf)});
    enc.add_bool(true);
    auto data = enc.finish();
    REQUIRE(data.size() == 1);
    REQUIRE(data[0] == 0xf5);
}

TEST_CASE("cross_val: rfc8949 known binary - null", "[cross_validation]") {
    // null → 0xf6
    uint8_t buf[256];
    encoder enc(byte_span{buf, sizeof(buf)});
    enc.add_null();
    auto data = enc.finish();
    REQUIRE(data.size() == 1);
    REQUIRE(data[0] == 0xf6);
}

TEST_CASE("cross_val: rfc8949 known binary - dynamic_encoder byte match", "[cross_validation]") {
    // Verify dynamic_encoder produces the same bytes as static encoder
    uint8_t buf[256];
    encoder enc(byte_span{buf, sizeof(buf)});
    {
        auto m = enc.map();
        m["x"] = 1;
        m["y"] = 2;
    }
    auto static_data = enc.finish();

    dynamic_encoder denc;
    {
        auto m = denc.map();
        m["x"] = 1;
        m["y"] = 2;
    }
    auto dynamic_data = denc.finish();

    REQUIRE(static_data.size() == dynamic_data.size());
    REQUIRE(std::memcmp(static_data.data(), dynamic_data.data(), static_data.size()) == 0);
}
