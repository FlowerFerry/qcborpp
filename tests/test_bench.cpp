/*
 * tests/test_bench.cpp
 *
 * Performance benchmarks: qcborpp vs raw QCBOR C.
 *
 * Uses manual timing (std::chrono) — no external benchmark framework
 * dependency.  Each scenario runs N iterations and reports average
 * microseconds per operation.
 *
 * Scenarios:
 *   encode_flat_map   — 100 KV pairs, mixed types
 *   encode_array_1k   — 1000 int64 values
 *   encode_nested     — map→array→map, 3 levels
 *   encode_strings    — 200 short text strings
 *   decode_flat_map   — 100 KV pairs from CBOR bytes
 *   decode_array_1k   — 1000 int64 from CBOR bytes
 *   decode_nested     — 3-level nested structure
 *   roundtrip_map     — encode+decode 100 KV pairs
 *   roundtrip_array   — encode+decode 1000 int64
 *
 * Each scenario compares:
 *   - Raw QCBOR C API        (baseline)
 *   - qcborpp::encoder        (static buffer)
 *   - qcborpp::dynamic_encoder (deferred encoding)
 *   - qcborpp encoder + map_builder (when applicable)
 */

#include <qcborpp/qcborpp.hpp>
#include "qcbor/qcbor_encode.h"
#include "qcbor/qcbor_decode.h"

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <vector>
#include <string>
#include <cstring>
#include <cstdio>
#include <cmath>

using namespace qcborpp;

// ══════════════════════════════════════════════════════════════════════════
// Timing helper
// ══════════════════════════════════════════════════════════════════════════

using Clock = std::chrono::high_resolution_clock;

struct bench_result {
    const char* name;
    double      us_per_op;
    size_t      iterations;
};

inline bench_result measure(const char* name, size_t iters,
                            const std::function<void()>& fn) {
    auto start = Clock::now();
    for (size_t i = 0; i < iters; ++i) fn();
    auto end = Clock::now();
    auto us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
    return {name, double(us) / double(iters), iters};
}

// ══════════════════════════════════════════════════════════════════════════
// Scenario: encode a flat map with 100 KV pairs (mixed types)
// ══════════════════════════════════════════════════════════════════════════

static void qcbor_c_encode_flat_map(QCBOREncodeContext* ctx) {
    QCBOREncode_OpenMap(ctx);
    for (int i = 0; i < 50; ++i) {
        char key[16];
        snprintf(key, sizeof(key), "int_key_%d", i);
        QCBOREncode_AddSZString(ctx, key);
        QCBOREncode_AddInt64(ctx, i * 100);

        snprintf(key, sizeof(key), "str_key_%d", i);
        QCBOREncode_AddSZString(ctx, key);
        QCBOREncode_AddSZString(ctx, "a moderately sized string value for benchmarking");
    }
    QCBOREncode_CloseMap(ctx);
}

template<typename Enc>
static void bench_encode_flat_map(Enc& enc) {
    auto m = enc.map();
    for (int i = 0; i < 50; ++i) {
        char key[16];
        snprintf(key, sizeof(key), "int_key_%d", i);
        m[key] = i * 100;
        snprintf(key, sizeof(key), "str_key_%d", i);
        m[key] = "a moderately sized string value for benchmarking";
    }
}

TEST_CASE("bench: encode flat map 100 KV", "[bench][encode]") {
    constexpr size_t ITERS = 200;

    // ── Raw QCBOR C ──
    auto r1 = measure("QCBOR C raw", ITERS, []() {
        QCBOREncodeContext ctx;
        QCBOREncode_Init(&ctx, UsefulBuf{nullptr, SIZE_MAX});
        qcbor_c_encode_flat_map(&ctx);
        UsefulBufC out;
        QCBOREncode_Finish(&ctx, &out);
    });

    // ── qcborpp static encoder ──
    auto r2 = measure("qcborpp static", ITERS, []() {
        uint8_t buf[16384];
        encoder enc(byte_span{buf, sizeof(buf)});
        bench_encode_flat_map(enc);
        enc.finish();
    });

    // ── qcborpp dynamic_encoder ──
    auto r3 = measure("qcborpp dynamic", ITERS, []() {
        dynamic_encoder enc;
        bench_encode_flat_map(enc);
        enc.finish();
    });

    WARN(r1.name << " : " << r1.us_per_op << " us/op (" << r1.iterations << " iters)");
    WARN(r2.name << " : " << r2.us_per_op << " us/op (" << r2.iterations << " iters)");
    WARN(r3.name << " : " << r3.us_per_op << " us/op (" << r3.iterations << " iters)");

    // Static encoder should be within ~2x of raw QCBOR C (same underlying calls)
    // dynamic_encoder has two-pass overhead — expect slower
    CHECK(true); // informational only
}

// ══════════════════════════════════════════════════════════════════════════
// Scenario: encode a flat array of 1000 int64 values
// ══════════════════════════════════════════════════════════════════════════

TEST_CASE("bench: encode array 1000 int64", "[bench][encode]") {
    constexpr size_t ITERS = 500;

    auto r1 = measure("QCBOR C raw", ITERS, []() {
        QCBOREncodeContext ctx;
        QCBOREncode_Init(&ctx, UsefulBuf{nullptr, SIZE_MAX});
        QCBOREncode_OpenArray(&ctx);
        for (int i = 0; i < 1000; ++i)
            QCBOREncode_AddInt64(&ctx, i * 7);
        QCBOREncode_CloseArray(&ctx);
        UsefulBufC out;
        QCBOREncode_Finish(&ctx, &out);
    });

    auto r2 = measure("qcborpp static", ITERS, []() {
        uint8_t buf[16384];
        encoder enc(byte_span{buf, sizeof(buf)});
        enc.open_array();
        for (int i = 0; i < 1000; ++i)
            enc.add_int64(i * 7);
        enc.close_array();
        enc.finish();
    });

    auto r3 = measure("qcborpp dynamic", ITERS, []() {
        dynamic_encoder enc;
        enc.open_array();
        for (int i = 0; i < 1000; ++i)
            enc.add_int64(i * 7);
        enc.close_array();
        enc.finish();
    });

    WARN(r1.name << " : " << r1.us_per_op << " us/op");
    WARN(r2.name << " : " << r2.us_per_op << " us/op");
    WARN(r3.name << " : " << r3.us_per_op << " us/op");
    CHECK(true);
}

// ══════════════════════════════════════════════════════════════════════════
// Scenario: encode nested map→array→map (3 levels, 50 items total)
// ══════════════════════════════════════════════════════════════════════════

TEST_CASE("bench: encode nested 3-level", "[bench][encode]") {
    constexpr size_t ITERS = 500;

    auto r1 = measure("QCBOR C raw", ITERS, []() {
        QCBOREncodeContext ctx;
        QCBOREncode_Init(&ctx, UsefulBuf{nullptr, SIZE_MAX});
        QCBOREncode_OpenMap(&ctx);                       // L0
        for (int i = 0; i < 5; ++i) {
            char key[8];
            snprintf(key, sizeof(key), "g%d", i);
            QCBOREncode_AddSZString(&ctx, key);
            QCBOREncode_OpenArray(&ctx);                 // L1
            for (int j = 0; j < 5; ++j) {
                QCBOREncode_OpenMap(&ctx);               // L2
                QCBOREncode_AddSZString(&ctx, "x");
                QCBOREncode_AddInt64(&ctx, j);
                QCBOREncode_AddSZString(&ctx, "y");
                QCBOREncode_AddInt64(&ctx, i);
                QCBOREncode_CloseMap(&ctx);              // L2
            }
            QCBOREncode_CloseArray(&ctx);                // L1
        }
        QCBOREncode_CloseMap(&ctx);                      // L0
        UsefulBufC out;
        QCBOREncode_Finish(&ctx, &out);
    });

    auto r2 = measure("qcborpp static", ITERS, []() {
        uint8_t buf[8192];
        encoder enc(byte_span{buf, sizeof(buf)});
        {
            auto m = enc.map();
            for (int i = 0; i < 5; ++i) {
                char key[8];
                snprintf(key, sizeof(key), "g%d", i);
                auto a = m[key].array();
                for (int j = 0; j < 5; ++j) {
                    auto inner = a.add_map();
                    inner["x"] = j;
                    inner["y"] = i;
                }
            }
        }
        enc.finish();
    });

    auto r3 = measure("qcborpp dynamic", ITERS, []() {
        dynamic_encoder enc;
        {
            auto m = enc.map();
            for (int i = 0; i < 5; ++i) {
                char key[8];
                snprintf(key, sizeof(key), "g%d", i);
                auto a = m[key].array();
                for (int j = 0; j < 5; ++j) {
                    auto inner = a.add_map();
                    inner["x"] = j;
                    inner["y"] = i;
                }
            }
        }
        enc.finish();
    });

    WARN(r1.name << " : " << r1.us_per_op << " us/op");
    WARN(r2.name << " : " << r2.us_per_op << " us/op");
    WARN(r3.name << " : " << r3.us_per_op << " us/op");
    CHECK(true);
}

// ══════════════════════════════════════════════════════════════════════════
// Scenario: encode 200 short text strings
// ══════════════════════════════════════════════════════════════════════════

TEST_CASE("bench: encode 200 short strings", "[bench][encode]") {
    constexpr size_t ITERS = 300;

    auto r1 = measure("QCBOR C raw", ITERS, []() {
        QCBOREncodeContext ctx;
        QCBOREncode_Init(&ctx, UsefulBuf{nullptr, SIZE_MAX});
        QCBOREncode_OpenArray(&ctx);
        for (int i = 0; i < 200; ++i)
            QCBOREncode_AddSZString(&ctx, "benchmark_string_42");
        QCBOREncode_CloseArray(&ctx);
        UsefulBufC out;
        QCBOREncode_Finish(&ctx, &out);
    });

    auto r2 = measure("qcborpp static", ITERS, []() {
        uint8_t buf[8192];
        encoder enc(byte_span{buf, sizeof(buf)});
        enc.open_array();
        for (int i = 0; i < 200; ++i)
            enc.add_text("benchmark_string_42");
        enc.close_array();
        enc.finish();
    });

    auto r3 = measure("qcborpp dynamic", ITERS, []() {
        dynamic_encoder enc;
        enc.open_array();
        for (int i = 0; i < 200; ++i)
            enc.add_text("benchmark_string_42");
        enc.close_array();
        enc.finish();
    });

    WARN(r1.name << " : " << r1.us_per_op << " us/op");
    WARN(r2.name << " : " << r2.us_per_op << " us/op");
    WARN(r3.name << " : " << r3.us_per_op << " us/op");
    CHECK(true);
}

// ══════════════════════════════════════════════════════════════════════════
// Scenario: decode a flat map with 100 KV pairs
// ══════════════════════════════════════════════════════════════════════════

TEST_CASE("bench: decode flat map 100 KV", "[bench][decode]") {
    // Produce CBOR bytes once
    std::vector<uint8_t> bytes;
    {
        QCBOREncodeContext ctx;
        QCBOREncode_Init(&ctx, UsefulBuf{nullptr, SIZE_MAX});
        qcbor_c_encode_flat_map(&ctx);
        size_t needed;
        QCBOREncode_FinishGetSize(&ctx, &needed);
        bytes.resize(needed);
        QCBOREncode_Init(&ctx, UsefulBuf{bytes.data(), bytes.size()});
        qcbor_c_encode_flat_map(&ctx);
        UsefulBufC out;
        QCBOREncode_Finish(&ctx, &out);
        bytes.resize(out.len);
    }

    constexpr size_t ITERS = 200;

    // ── Raw QCBOR C decode ──
    auto r1 = measure("QCBOR C raw", ITERS, [&]() {
        QCBORDecodeContext ctx;
        QCBORDecode_Init(&ctx, UsefulBufC{bytes.data(), bytes.size()},
                         QCBOR_DECODE_MODE_NORMAL);
        QCBORDecode_EnterMap(&ctx, nullptr);
        int64_t sum = 0;
        for (int i = 0; i < 50; ++i) {
            QCBORItem item;
            QCBORDecode_GetNext(&ctx, &item); // int key
            QCBORDecode_GetNext(&ctx, &item); // int value
            sum += item.val.int64;
            QCBORDecode_GetNext(&ctx, &item); // str key (skip)
            QCBORDecode_GetNext(&ctx, &item); // str value (skip)
        }
        QCBORDecode_ExitMap(&ctx);
        QCBORDecode_Finish(&ctx);
        // prevent optimization
        volatile int64_t v = sum; (void)v;
    });

    // ── qcborpp decoder ──
    auto r2 = measure("qcborpp decoder", ITERS, [&]() {
        decoder dec(const_byte_span{bytes.data(), bytes.size()});
        auto m = dec.map();
        int64_t sum = 0;
        for (int i = 0; i < 50; ++i) {
            char key[16];
            snprintf(key, sizeof(key), "int_key_%d", i);
            sum += int64_t(m[key]);
        }
        dec.finish();
        volatile int64_t v = sum; (void)v;
    });

    WARN(r1.name << " : " << r1.us_per_op << " us/op");
    WARN(r2.name << " : " << r2.us_per_op << " us/op");

    // ── qcborpp get_items() batch ──
    auto r3 = measure("qcborpp get_items", ITERS, [&]() {
        decoder dec(const_byte_span{bytes.data(), bytes.size()});
        auto m = dec.map();
        std::vector<decoder::item_spec> specs;
        specs.reserve(50);
        for (int i = 0; i < 50; ++i) {
            decoder::item_spec s;
            char key[16];
            snprintf(key, sizeof(key), "int_key_%d", i);
            s.label_str = key;
            s.type = cbor_type::int64;
            specs.push_back(s);
        }
        std::vector<decoded_item> out;
        m.get_items(specs, out);
        int64_t sum = 0;
        for (auto& item : out) sum += item.value.int64_val;
        dec.finish();
        volatile int64_t v = sum; (void)v;
    });

    // ── qcborpp prefetch + cached operator[] ──
    auto r4 = measure("qcborpp prefetch+[]", ITERS, [&]() {
        decoder dec(const_byte_span{bytes.data(), bytes.size()});
        auto m = dec.map();
        m.prefetch();
        int64_t sum = 0;
        for (int i = 0; i < 50; ++i) {
            char key[16];
            snprintf(key, sizeof(key), "int_key_%d", i);
            sum += int64_t(m[key]);
        }
        dec.finish();
        volatile int64_t v = sum; (void)v;
    });

    WARN(r3.name << " : " << r3.us_per_op << " us/op");
    WARN(r4.name << " : " << r4.us_per_op << " us/op");
    CHECK(true);
}

// ══════════════════════════════════════════════════════════════════════════
// Scenario: decode an array of 1000 int64 values
// ══════════════════════════════════════════════════════════════════════════

TEST_CASE("bench: decode array 1000 int64", "[bench][decode]") {
    std::vector<uint8_t> bytes;
    {
        QCBOREncodeContext ctx;
        QCBOREncode_Init(&ctx, UsefulBuf{nullptr, SIZE_MAX});
        QCBOREncode_OpenArray(&ctx);
        for (int i = 0; i < 1000; ++i)
            QCBOREncode_AddInt64(&ctx, i * 3);
        QCBOREncode_CloseArray(&ctx);
        size_t needed;
        QCBOREncode_FinishGetSize(&ctx, &needed);
        bytes.resize(needed);
        QCBOREncode_Init(&ctx, UsefulBuf{bytes.data(), bytes.size()});
        QCBOREncode_OpenArray(&ctx);
        for (int i = 0; i < 1000; ++i)
            QCBOREncode_AddInt64(&ctx, i * 3);
        QCBOREncode_CloseArray(&ctx);
        UsefulBufC out;
        QCBOREncode_Finish(&ctx, &out);
        bytes.resize(out.len);
    }

    constexpr size_t ITERS = 500;

    auto r1 = measure("QCBOR C raw", ITERS, [&]() {
        QCBORDecodeContext ctx;
        QCBORDecode_Init(&ctx, UsefulBufC{bytes.data(), bytes.size()},
                         QCBOR_DECODE_MODE_NORMAL);
        QCBORDecode_EnterArray(&ctx, nullptr);
        int64_t sum = 0;
        for (int i = 0; i < 1000; ++i) {
            QCBORItem item;
            QCBORDecode_GetNext(&ctx, &item);
            sum += item.val.int64;
        }
        QCBORDecode_ExitArray(&ctx);
        QCBORDecode_Finish(&ctx);
        volatile int64_t v = sum; (void)v;
    });

    auto r2 = measure("qcborpp decoder", ITERS, [&]() {
        decoder dec(const_byte_span{bytes.data(), bytes.size()});
        auto a = dec.array();
        int64_t sum = 0;
        for (int i = 0; i < 1000; ++i)
            sum += int64_t(a.next());
        dec.finish();
        volatile int64_t v = sum; (void)v;
    });

    WARN(r1.name << " : " << r1.us_per_op << " us/op");
    WARN(r2.name << " : " << r2.us_per_op << " us/op");
    CHECK(true);
}

// ══════════════════════════════════════════════════════════════════════════
// Scenario: roundtrip (encode + decode) — 100 KV map
// ══════════════════════════════════════════════════════════════════════════

TEST_CASE("bench: roundtrip flat map 100 KV", "[bench][roundtrip]") {
    constexpr size_t ITERS = 100;

    auto r1 = measure("QCBOR C encode+decode", ITERS, []() {
        // encode
        QCBOREncodeContext ectx;
        QCBOREncode_Init(&ectx, UsefulBuf{nullptr, SIZE_MAX});
        qcbor_c_encode_flat_map(&ectx);
        size_t needed;
        QCBOREncode_FinishGetSize(&ectx, &needed);
        std::vector<uint8_t> buf(needed);
        QCBOREncode_Init(&ectx, UsefulBuf{buf.data(), buf.size()});
        qcbor_c_encode_flat_map(&ectx);
        UsefulBufC out;
        QCBOREncode_Finish(&ectx, &out);

        // decode
        QCBORDecodeContext dctx;
        QCBORDecode_Init(&dctx, UsefulBufC{buf.data(), buf.size()},
                         QCBOR_DECODE_MODE_NORMAL);
        QCBORDecode_EnterMap(&dctx, nullptr);
        int64_t sum = 0;
        for (int i = 0; i < 50; ++i) {
            QCBORItem item;
            QCBORDecode_GetNext(&dctx, &item);
            QCBORDecode_GetNext(&dctx, &item);
            sum += item.val.int64;
            QCBORDecode_GetNext(&dctx, &item);
            QCBORDecode_GetNext(&dctx, &item);
        }
        QCBORDecode_ExitMap(&dctx);
        QCBORDecode_Finish(&dctx);
        volatile int64_t v = sum; (void)v;
    });

    auto r2 = measure("qcborpp static encode+decode", ITERS, []() {
        uint8_t buf[16384];
        encoder enc(byte_span{buf, sizeof(buf)});
        bench_encode_flat_map(enc);
        auto data = enc.finish();

        decoder dec(data);
        auto m = dec.map();
        int64_t sum = 0;
        for (int i = 0; i < 50; ++i) {
            char key[16];
            snprintf(key, sizeof(key), "int_key_%d", i);
            sum += int64_t(m[key]);
        }
        dec.finish();
        volatile int64_t v = sum; (void)v;
    });

    auto r3 = measure("qcborpp dynamic encode+decode", ITERS, []() {
        dynamic_encoder enc;
        bench_encode_flat_map(enc);
        auto data = enc.finish();

        decoder dec(data);
        auto m = dec.map();
        int64_t sum = 0;
        for (int i = 0; i < 50; ++i) {
            char key[16];
            snprintf(key, sizeof(key), "int_key_%d", i);
            sum += int64_t(m[key]);
        }
        dec.finish();
        volatile int64_t v = sum; (void)v;
    });

    WARN(r1.name << " : " << r1.us_per_op << " us/op");
    WARN(r2.name << " : " << r2.us_per_op << " us/op");
    WARN(r3.name << " : " << r3.us_per_op << " us/op");
    CHECK(true);
}

// ══════════════════════════════════════════════════════════════════════════
// Scenario: roundtrip array 1000 int64
// ══════════════════════════════════════════════════════════════════════════

TEST_CASE("bench: roundtrip array 1000 int64", "[bench][roundtrip]") {
    constexpr size_t ITERS = 300;

    auto r1 = measure("QCBOR C encode+decode", ITERS, []() {
        QCBOREncodeContext ectx;
        QCBOREncode_Init(&ectx, UsefulBuf{nullptr, SIZE_MAX});
        QCBOREncode_OpenArray(&ectx);
        for (int i = 0; i < 1000; ++i)
            QCBOREncode_AddInt64(&ectx, i * 3);
        QCBOREncode_CloseArray(&ectx);
        size_t needed;
        QCBOREncode_FinishGetSize(&ectx, &needed);
        std::vector<uint8_t> buf(needed);
        QCBOREncode_Init(&ectx, UsefulBuf{buf.data(), buf.size()});
        QCBOREncode_OpenArray(&ectx);
        for (int i = 0; i < 1000; ++i)
            QCBOREncode_AddInt64(&ectx, i * 3);
        QCBOREncode_CloseArray(&ectx);
        UsefulBufC out;
        QCBOREncode_Finish(&ectx, &out);
        buf.resize(out.len);

        QCBORDecodeContext dctx;
        QCBORDecode_Init(&dctx, UsefulBufC{buf.data(), buf.size()},
                         QCBOR_DECODE_MODE_NORMAL);
        QCBORDecode_EnterArray(&dctx, nullptr);
        int64_t sum = 0;
        for (int i = 0; i < 1000; ++i) {
            QCBORItem item;
            QCBORDecode_GetNext(&dctx, &item);
            sum += item.val.int64;
        }
        QCBORDecode_ExitArray(&dctx);
        QCBORDecode_Finish(&dctx);
        volatile int64_t v = sum; (void)v;
    });

    auto r2 = measure("qcborpp static encode+decode", ITERS, []() {
        uint8_t buf[16384];
        encoder enc(byte_span{buf, sizeof(buf)});
        enc.open_array();
        for (int i = 0; i < 1000; ++i)
            enc.add_int64(i * 3);
        enc.close_array();
        auto data = enc.finish();

        decoder dec(data);
        auto a = dec.array();
        int64_t sum = 0;
        for (int i = 0; i < 1000; ++i)
            sum += int64_t(a.next());
        dec.finish();
        volatile int64_t v = sum; (void)v;
    });

    auto r3 = measure("qcborpp dynamic encode+decode", ITERS, []() {
        dynamic_encoder enc;
        enc.open_array();
        for (int i = 0; i < 1000; ++i)
            enc.add_int64(i * 3);
        enc.close_array();
        auto data = enc.finish();

        decoder dec(data);
        auto a = dec.array();
        int64_t sum = 0;
        for (int i = 0; i < 1000; ++i)
            sum += int64_t(a.next());
        dec.finish();
        volatile int64_t v = sum; (void)v;
    });

    WARN(r1.name << " : " << r1.us_per_op << " us/op");
    WARN(r2.name << " : " << r2.us_per_op << " us/op");
    WARN(r3.name << " : " << r3.us_per_op << " us/op");
    CHECK(true);
}

// ══════════════════════════════════════════════════════════════════════════
// Scenario: encode 100 float values (preferred — slow path)
// ══════════════════════════════════════════════════════════════════════════

TEST_CASE("bench: encode 100 floats", "[bench][encode]") {
    constexpr size_t ITERS = 500;

    auto r1 = measure("QCBOR C raw", ITERS, []() {
        QCBOREncodeContext ctx;
        QCBOREncode_Init(&ctx, UsefulBuf{nullptr, SIZE_MAX});
        QCBOREncode_OpenArray(&ctx);
        for (int i = 0; i < 100; ++i)
            QCBOREncode_AddFloat(&ctx, static_cast<float>(i) * 1.1f);
        QCBOREncode_CloseArray(&ctx);
        UsefulBufC out;
        QCBOREncode_Finish(&ctx, &out);
    });

    auto r2 = measure("qcborpp static", ITERS, []() {
        uint8_t buf[4096];
        encoder enc(byte_span{buf, sizeof(buf)});
        enc.open_array();
        for (int i = 0; i < 100; ++i)
            enc.add_float(static_cast<float>(i) * 1.1f);
        enc.close_array();
        enc.finish();
    });

    auto r3 = measure("qcborpp dynamic", ITERS, []() {
        dynamic_encoder enc;
        enc.open_array();
        for (int i = 0; i < 100; ++i)
            enc.add_float(static_cast<float>(i) * 1.1f);
        enc.close_array();
        enc.finish();
    });

    WARN(r1.name << " : " << r1.us_per_op << " us/op");
    WARN(r2.name << " : " << r2.us_per_op << " us/op");
    WARN(r3.name << " : " << r3.us_per_op << " us/op");
    CHECK(true);
}

// ══════════════════════════════════════════════════════════════════════════
// Scenario: encode 100 doubles no-preferred (fast-path, fixed 9 bytes)
// ══════════════════════════════════════════════════════════════════════════

TEST_CASE("bench: encode 100 doubles no-preferred", "[bench][encode]") {
    constexpr size_t ITERS = 500;

    auto r1 = measure("QCBOR C raw", ITERS, []() {
        QCBOREncodeContext ctx;
        QCBOREncode_Init(&ctx, UsefulBuf{nullptr, SIZE_MAX});
        QCBOREncode_OpenArray(&ctx);
        for (int i = 0; i < 100; ++i)
            QCBOREncode_AddDoubleNoPreferred(&ctx, static_cast<double>(i) * 1.5);
        QCBOREncode_CloseArray(&ctx);
        UsefulBufC out;
        QCBOREncode_Finish(&ctx, &out);
    });

    auto r2 = measure("qcborpp static", ITERS, []() {
        uint8_t buf[4096];
        encoder enc(byte_span{buf, sizeof(buf)});
        enc.open_array();
        for (int i = 0; i < 100; ++i)
            enc.add_double_no_preferred(static_cast<double>(i) * 1.5);
        enc.close_array();
        enc.finish();
    });

    auto r3 = measure("qcborpp dynamic", ITERS, []() {
        dynamic_encoder enc;
        enc.open_array();
        for (int i = 0; i < 100; ++i)
            enc.add_double_no_preferred(static_cast<double>(i) * 1.5);
        enc.close_array();
        enc.finish();
    });

    WARN(r1.name << " : " << r1.us_per_op << " us/op");
    WARN(r2.name << " : " << r2.us_per_op << " us/op");
    WARN(r3.name << " : " << r3.us_per_op << " us/op");
    CHECK(true);
}

// ══════════════════════════════════════════════════════════════════════════
// Scenario: encode 1 KiB byte string x50
// ══════════════════════════════════════════════════════════════════════════

TEST_CASE("bench: encode 1KB bytes x50", "[bench][encode]") {
    constexpr size_t ITERS = 500;
    static const std::vector<uint8_t> kPayload(1024, 0xAB);

    auto r1 = measure("QCBOR C raw", ITERS, [&]() {
        QCBOREncodeContext ctx;
        QCBOREncode_Init(&ctx, UsefulBuf{nullptr, SIZE_MAX});
        QCBOREncode_OpenArray(&ctx);
        for (int i = 0; i < 50; ++i)
            QCBOREncode_AddBytes(&ctx, UsefulBufC{kPayload.data(), kPayload.size()});
        QCBOREncode_CloseArray(&ctx);
        UsefulBufC out;
        QCBOREncode_Finish(&ctx, &out);
    });

    auto r2 = measure("qcborpp static", ITERS, [&]() {
        uint8_t buf[65536];
        encoder enc(byte_span{buf, sizeof(buf)});
        enc.open_array();
        for (int i = 0; i < 50; ++i)
            enc.add_bytes(const_byte_span{kPayload.data(), kPayload.size()});
        enc.close_array();
        enc.finish();
    });

    auto r3 = measure("qcborpp dynamic", ITERS, [&]() {
        dynamic_encoder enc;
        enc.open_array();
        for (int i = 0; i < 50; ++i)
            enc.add_bytes(const_byte_span{kPayload.data(), kPayload.size()});
        enc.close_array();
        enc.finish();
    });

    auto r4 = measure("qcborpp dynamic ref", ITERS, [&]() {
        dynamic_encoder enc;
        enc.open_array();
        for (int i = 0; i < 50; ++i)
            enc.add_bytes_ref(const_byte_span{kPayload.data(), kPayload.size()});
        enc.close_array();
        enc.finish();
    });

    WARN(r1.name << " : " << r1.us_per_op << " us/op");
    WARN(r2.name << " : " << r2.us_per_op << " us/op");
    WARN(r3.name << " : " << r3.us_per_op << " us/op");
    WARN(r4.name << " : " << r4.us_per_op << " us/op");
    CHECK(true);
}

// ══════════════════════════════════════════════════════════════════════════
// Scenario: text_ref zero-copy vs add_text copy
// ══════════════════════════════════════════════════════════════════════════

TEST_CASE("bench: text_ref vs add_text", "[bench][encode]") {
    constexpr size_t ITERS = 1000;
    std::string long_str(256, 'x');

    auto r0 = measure("QCBOR C raw", ITERS, [&]() {
        QCBOREncodeContext ctx;
        QCBOREncode_Init(&ctx, UsefulBuf{nullptr, SIZE_MAX});
        QCBOREncode_OpenArray(&ctx);
        for (int i = 0; i < 200; ++i)
            QCBOREncode_AddSZString(&ctx, long_str.c_str());
        QCBOREncode_CloseArray(&ctx);
        UsefulBufC out;
        QCBOREncode_Finish(&ctx, &out);
    });

    auto r1 = measure("dynamic add_text (copy)", ITERS, [&]() {
        dynamic_encoder enc;
        enc.open_array();
        for (int i = 0; i < 200; ++i)
            enc.add_text(long_str);
        enc.close_array();
        enc.finish();
    });

    auto r2 = measure("dynamic add_text_ref (ref)", ITERS, [&]() {
        dynamic_encoder enc;
        enc.open_array();
        for (int i = 0; i < 200; ++i)
            enc.add_text_ref(long_str);
        enc.close_array();
        enc.finish();
    });

    WARN(r0.name << " : " << r0.us_per_op << " us/op");
    WARN(r1.name << " : " << r1.us_per_op << " us/op");
    WARN(r2.name << " : " << r2.us_per_op << " us/op");
    CHECK(true);
}

// ══════════════════════════════════════════════════════════════════════════
// Scenario: decode nested 3-level structure
// ══════════════════════════════════════════════════════════════════════════

TEST_CASE("bench: decode nested 3-level", "[bench][decode]") {
    std::vector<uint8_t> bytes;
    {
        QCBOREncodeContext ctx;
        QCBOREncode_Init(&ctx, UsefulBuf{nullptr, SIZE_MAX});
        QCBOREncode_OpenMap(&ctx);
        for (int i = 0; i < 5; ++i) {
            char key[8];
            snprintf(key, sizeof(key), "g%d", i);
            QCBOREncode_AddSZString(&ctx, key);
            QCBOREncode_OpenArray(&ctx);
            for (int j = 0; j < 5; ++j) {
                QCBOREncode_OpenMap(&ctx);
                QCBOREncode_AddSZString(&ctx, "x");
                QCBOREncode_AddInt64(&ctx, j);
                QCBOREncode_AddSZString(&ctx, "y");
                QCBOREncode_AddInt64(&ctx, i);
                QCBOREncode_CloseMap(&ctx);
            }
            QCBOREncode_CloseArray(&ctx);
        }
        QCBOREncode_CloseMap(&ctx);
        size_t needed;
        QCBOREncode_FinishGetSize(&ctx, &needed);
        bytes.resize(needed);
        QCBOREncode_Init(&ctx, UsefulBuf{bytes.data(), bytes.size()});
        QCBOREncode_OpenMap(&ctx);
        for (int i = 0; i < 5; ++i) {
            char key[8]; snprintf(key, sizeof(key), "g%d", i);
            QCBOREncode_AddSZString(&ctx, key);
            QCBOREncode_OpenArray(&ctx);
            for (int j = 0; j < 5; ++j) {
                QCBOREncode_OpenMap(&ctx);
                QCBOREncode_AddSZString(&ctx, "x"); QCBOREncode_AddInt64(&ctx, j);
                QCBOREncode_AddSZString(&ctx, "y"); QCBOREncode_AddInt64(&ctx, i);
                QCBOREncode_CloseMap(&ctx);
            }
            QCBOREncode_CloseArray(&ctx);
        }
        QCBOREncode_CloseMap(&ctx);
        UsefulBufC out;
        QCBOREncode_Finish(&ctx, &out);
        bytes.resize(out.len);
    }

    constexpr size_t ITERS = 1000;

    auto r1 = measure("QCBOR C raw", ITERS, [&]() {
        QCBORDecodeContext ctx;
        QCBORDecode_Init(&ctx, UsefulBufC{bytes.data(), bytes.size()},
                         QCBOR_DECODE_MODE_NORMAL);
        QCBORDecode_EnterMap(&ctx, nullptr);
        int64_t sum = 0;
        for (int i = 0; i < 5; ++i) {
            QCBORItem item;
            QCBORDecode_GetNext(&ctx, &item);
            QCBORDecode_EnterArray(&ctx, &item);
            for (int j = 0; j < 5; ++j) {
                QCBORDecode_EnterMap(&ctx, nullptr);
                QCBORDecode_GetNext(&ctx, &item);
                QCBORDecode_GetNext(&ctx, &item); sum += item.val.int64;
                QCBORDecode_GetNext(&ctx, &item);
                QCBORDecode_GetNext(&ctx, &item); sum += item.val.int64;
                QCBORDecode_ExitMap(&ctx);
            }
            QCBORDecode_ExitArray(&ctx);
        }
        QCBORDecode_ExitMap(&ctx);
        QCBORDecode_Finish(&ctx);
        volatile int64_t v = sum; (void)v;
    });

    auto r2 = measure("qcborpp decoder", ITERS, [&]() {
        decoder dec(const_byte_span{bytes.data(), bytes.size()});
        auto m = dec.map();
        int64_t sum = 0;
        for (int i = 0; i < 5; ++i) {
            char key[8];
            snprintf(key, sizeof(key), "g%d", i);
            auto a = m[key].as_array();
            while (!a.done()) {
                auto item = a.next();
                auto inner = item.as_map();
                sum += int64_t(inner["x"]);
                sum += int64_t(inner["y"]);
            }
        }
        dec.finish();
        volatile int64_t v = sum; (void)v;
    });

    WARN(r1.name << " : " << r1.us_per_op << " us/op");
    WARN(r2.name << " : " << r2.us_per_op << " us/op");
    CHECK(true);
}

// ══════════════════════════════════════════════════════════════════════════
// Scenario: decode for_each vs operator[]
// ══════════════════════════════════════════════════════════════════════════

TEST_CASE("bench: decode for_each 50 KV", "[bench][decode]") {
    std::vector<uint8_t> bytes;
    {
        QCBOREncodeContext ctx;
        QCBOREncode_Init(&ctx, UsefulBuf{nullptr, SIZE_MAX});
        QCBOREncode_OpenMap(&ctx);
        for (int i = 0; i < 50; ++i) {
            char key[16];
            snprintf(key, sizeof(key), "int_key_%d", i);
            QCBOREncode_AddSZString(&ctx, key);
            QCBOREncode_AddInt64(&ctx, i * 100);
        }
        QCBOREncode_CloseMap(&ctx);
        size_t needed;
        QCBOREncode_FinishGetSize(&ctx, &needed);
        bytes.resize(needed);
        QCBOREncode_Init(&ctx, UsefulBuf{bytes.data(), bytes.size()});
        QCBOREncode_OpenMap(&ctx);
        for (int i = 0; i < 50; ++i) {
            char key[16];
            snprintf(key, sizeof(key), "int_key_%d", i);
            QCBOREncode_AddSZString(&ctx, key);
            QCBOREncode_AddInt64(&ctx, i * 100);
        }
        QCBOREncode_CloseMap(&ctx);
        UsefulBufC out;
        QCBOREncode_Finish(&ctx, &out);
        bytes.resize(out.len);
    }

    constexpr size_t ITERS = 300;

    auto r0 = measure("QCBOR C raw", ITERS, [&]() {
        QCBORDecodeContext dctx;
        QCBORDecode_Init(&dctx, UsefulBufC{bytes.data(), bytes.size()},
                         QCBOR_DECODE_MODE_NORMAL);
        QCBORDecode_EnterMap(&dctx, nullptr);
        int64_t sum = 0;
        QCBORItem item;
        while (!QCBORDecode_GetNext(&dctx, &item)) {
            if (item.uDataType != QCBOR_TYPE_TEXT_STRING) break;
            QCBORDecode_GetNext(&dctx, &item);
            sum += item.val.int64;
        }
        QCBORDecode_ExitMap(&dctx);
        QCBORDecode_Finish(&dctx);
        volatile int64_t v = sum; (void)v;
    });

    auto r1 = measure("qcborpp operator[]", ITERS, [&]() {
        decoder dec(const_byte_span{bytes.data(), bytes.size()});
        auto m = dec.map();
        int64_t sum = 0;
        for (int i = 0; i < 50; ++i) {
            char key[16];
            snprintf(key, sizeof(key), "int_key_%d", i);
            sum += int64_t(m[key]);
        }
        dec.finish();
        volatile int64_t v = sum; (void)v;
    });

    auto r2 = measure("qcborpp for_each", ITERS, [&]() {
        decoder dec(const_byte_span{bytes.data(), bytes.size()});
        auto m = dec.map();
        int64_t sum = 0;
        m.for_each([&](std::string_view /*key*/, decoded_item val) {
            sum += val.value.int64_val;
        });
        dec.finish();
        volatile int64_t v = sum; (void)v;
    });

    WARN(r0.name << " : " << r0.us_per_op << " us/op");
    WARN(r1.name << " : " << r1.us_per_op << " us/op");
    WARN(r2.name << " : " << r2.us_per_op << " us/op");
    CHECK(true);
}

// ══════════════════════════════════════════════════════════════════════════
// Scenario: array_builder operator<< vs add()
// ══════════════════════════════════════════════════════════════════════════

TEST_CASE("bench: array_builder operator<< vs add", "[bench][encode]") {
    constexpr size_t ITERS = 500;

    auto r1 = measure("qcborpp static add()", ITERS, []() {
        uint8_t buf[16384];
        encoder enc(byte_span{buf, sizeof(buf)});
        {
            auto arr = enc.array();
            for (int i = 0; i < 200; ++i) {
                arr.add(int64_t(i));
                arr.add(std::string_view("hello"));
                arr.add(true);
            }
        }
        enc.finish();
    });

    auto r2 = measure("qcborpp static operator<<", ITERS, []() {
        uint8_t buf[16384];
        encoder enc(byte_span{buf, sizeof(buf)});
        {
            auto arr = enc.array();
            for (int i = 0; i < 200; ++i)
                arr << i << "hello" << true;
        }
        enc.finish();
    });

    auto r3 = measure("qcborpp dynamic add()", ITERS, []() {
        dynamic_encoder enc;
        {
            auto arr = enc.array();
            for (int i = 0; i < 200; ++i) {
                arr.add(int64_t(i));
                arr.add(std::string_view("hello"));
                arr.add(true);
            }
        }
        enc.finish();
    });

    auto r4 = measure("qcborpp dynamic operator<<", ITERS, []() {
        dynamic_encoder enc;
        {
            auto arr = enc.array();
            for (int i = 0; i < 200; ++i)
                arr << i << "hello" << true;
        }
        enc.finish();
    });

    WARN(r1.name << " : " << r1.us_per_op << " us/op");
    WARN(r2.name << " : " << r2.us_per_op << " us/op");
    WARN(r3.name << " : " << r3.us_per_op << " us/op");
    WARN(r4.name << " : " << r4.us_per_op << " us/op");
    CHECK(true);
}

// ══════════════════════════════════════════════════════════════════════════
// Scenario: roundtrip 100 floats
// ══════════════════════════════════════════════════════════════════════════

TEST_CASE("bench: roundtrip 100 floats", "[bench][roundtrip]") {
    constexpr size_t ITERS = 300;

    auto r1 = measure("QCBOR C encode+decode", ITERS, []() {
        QCBOREncodeContext ectx;
        QCBOREncode_Init(&ectx, UsefulBuf{nullptr, SIZE_MAX});
        QCBOREncode_OpenArray(&ectx);
        for (int i = 0; i < 100; ++i)
            QCBOREncode_AddFloat(&ectx, static_cast<float>(i) * 1.5f);
        QCBOREncode_CloseArray(&ectx);
        size_t needed;
        QCBOREncode_FinishGetSize(&ectx, &needed);
        std::vector<uint8_t> buf(needed);
        QCBOREncode_Init(&ectx, UsefulBuf{buf.data(), buf.size()});
        QCBOREncode_OpenArray(&ectx);
        for (int i = 0; i < 100; ++i)
            QCBOREncode_AddFloat(&ectx, static_cast<float>(i) * 1.5f);
        QCBOREncode_CloseArray(&ectx);
        UsefulBufC out;
        QCBOREncode_Finish(&ectx, &out);

        QCBORDecodeContext dctx;
        QCBORDecode_Init(&dctx, UsefulBufC{buf.data(), buf.size()},
                         QCBOR_DECODE_MODE_NORMAL);
        QCBORDecode_EnterArray(&dctx, nullptr);
        double sum = 0;
        for (int i = 0; i < 100; ++i) {
            QCBORItem item;
            QCBORDecode_GetNext(&dctx, &item);
            sum += item.val.dfnum;
        }
        QCBORDecode_ExitArray(&dctx);
        QCBORDecode_Finish(&dctx);
        volatile double v = sum; (void)v;
    });

    auto r2 = measure("qcborpp static encode+decode", ITERS, []() {
        uint8_t buf[4096];
        encoder enc(byte_span{buf, sizeof(buf)});
        enc.open_array();
        for (int i = 0; i < 100; ++i)
            enc.add_float(static_cast<float>(i) * 1.5f);
        enc.close_array();
        auto data = enc.finish();

        decoder dec(data);
        auto a = dec.array();
        double sum = 0;
        while (!a.done())
            sum += static_cast<double>(a.next());
        dec.finish();
        volatile double v = sum; (void)v;
    });

    auto r3 = measure("qcborpp dynamic encode+decode", ITERS, []() {
        dynamic_encoder enc;
        enc.open_array();
        for (int i = 0; i < 100; ++i)
            enc.add_float(static_cast<float>(i) * 1.5f);
        enc.close_array();
        auto data = enc.finish();

        decoder dec(data);
        auto a = dec.array();
        double sum = 0;
        while (!a.done())
            sum += static_cast<double>(a.next());
        dec.finish();
        volatile double v = sum; (void)v;
    });

    WARN(r1.name << " : " << r1.us_per_op << " us/op");
    WARN(r2.name << " : " << r2.us_per_op << " us/op");
    WARN(r3.name << " : " << r3.us_per_op << " us/op");
    CHECK(true);
}

// ══════════════════════════════════════════════════════════════════════════
// Scenario: roundtrip 1KB bytes x20
// ══════════════════════════════════════════════════════════════════════════

TEST_CASE("bench: roundtrip 1KB bytes x20", "[bench][roundtrip]") {
    constexpr size_t ITERS = 200;
    static const std::vector<uint8_t> kPayload(1024, 0xCD);

    auto r1 = measure("QCBOR C encode+decode", ITERS, [&]() {
        QCBOREncodeContext ectx;
        QCBOREncode_Init(&ectx, UsefulBuf{nullptr, SIZE_MAX});
        QCBOREncode_OpenArray(&ectx);
        for (int i = 0; i < 20; ++i)
            QCBOREncode_AddBytes(&ectx, UsefulBufC{kPayload.data(), kPayload.size()});
        QCBOREncode_CloseArray(&ectx);
        size_t needed;
        QCBOREncode_FinishGetSize(&ectx, &needed);
        std::vector<uint8_t> buf(needed);
        QCBOREncode_Init(&ectx, UsefulBuf{buf.data(), buf.size()});
        QCBOREncode_OpenArray(&ectx);
        for (int i = 0; i < 20; ++i)
            QCBOREncode_AddBytes(&ectx, UsefulBufC{kPayload.data(), kPayload.size()});
        QCBOREncode_CloseArray(&ectx);
        UsefulBufC out;
        QCBOREncode_Finish(&ectx, &out);

        QCBORDecodeContext dctx;
        QCBORDecode_Init(&dctx, UsefulBufC{buf.data(), buf.size()},
                         QCBOR_DECODE_MODE_NORMAL);
        QCBORDecode_EnterArray(&dctx, nullptr);
        size_t total = 0;
        for (int i = 0; i < 20; ++i) {
            QCBORItem item;
            QCBORDecode_GetNext(&dctx, &item);
            total += item.val.string.len;
        }
        QCBORDecode_ExitArray(&dctx);
        QCBORDecode_Finish(&dctx);
        volatile size_t v = total; (void)v;
    });

    auto r2 = measure("qcborpp static encode+decode", ITERS, [&]() {
        uint8_t buf[32768];
        encoder enc(byte_span{buf, sizeof(buf)});
        enc.open_array();
        for (int i = 0; i < 20; ++i)
            enc.add_bytes(const_byte_span{kPayload.data(), kPayload.size()});
        enc.close_array();
        auto data = enc.finish();

        decoder dec(data);
        auto a = dec.array();
        size_t total = 0;
        while (!a.done())
            total += static_cast<const_byte_span>(a.next()).size();
        dec.finish();
        volatile size_t v = total; (void)v;
    });

    auto r3 = measure("qcborpp dynamic encode+decode", ITERS, [&]() {
        dynamic_encoder enc;
        enc.open_array();
        for (int i = 0; i < 20; ++i)
            enc.add_bytes_ref(const_byte_span{kPayload.data(), kPayload.size()});
        enc.close_array();
        auto data = enc.finish();

        decoder dec(data);
        auto a = dec.array();
        size_t total = 0;
        while (!a.done())
            total += static_cast<const_byte_span>(a.next()).size();
        dec.finish();
        volatile size_t v = total; (void)v;
    });

    WARN(r1.name << " : " << r1.us_per_op << " us/op");
    WARN(r2.name << " : " << r2.us_per_op << " us/op");
    WARN(r3.name << " : " << r3.us_per_op << " us/op");
    CHECK(true);
}

