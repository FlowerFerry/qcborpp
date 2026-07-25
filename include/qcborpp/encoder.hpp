/*
 * qcborpp/encoder.hpp -- static-buffer encoder + template builders
 *
 * Copyright (c) 2024
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef QCBORPP_ENCODER_HPP
#define QCBORPP_ENCODER_HPP

#include "qcborpp/error.hpp"
#include "qcborpp/types.hpp"
#include "qcborpp/cbor_ref.hpp"
#include "qcbor/qcbor_encode.h"
#include <vector>
#include <cstring>
#include <chrono>
#include <cassert>

namespace qcborpp {

// Forward declarations
class encoder;
template<typename Enc> class basic_key_proxy;
template<typename Enc> class basic_map_builder;
template<typename Enc> class basic_array_builder;

// ============================================================================
// detail -- op recording (shared with dynamic_encoder)
// ============================================================================
namespace detail {

/** Internal operation codes for recording encoder actions. */
enum class enc_op : uint8_t {
    open_map    = 0,
    close_map   = 1,
    open_array  = 2,
    close_array = 3,
    add_int64   = 10,
    add_uint64  = 11,
    add_text    = 12,
    add_text_sz = 13,
    add_bytes   = 14,
    add_double  = 15,
    add_float   = 16,
    add_bool    = 17,
    add_null    = 18,
    add_undef   = 19,
    add_tag     = 20,
    add_simple  = 21,
    add_double_np = 22,
    add_float_np  = 23,
};

/**
 * Replay recorded operations into a QCBOR encode context using a
 * function-pointer dispatch table (no switch, single branch per op).
 *
 * @param ctx   The QCBOR encode context to replay into.
 * @param ops   The recorded operation buffer.
 */
inline void replay_ops(QCBOREncodeContext* ctx, const std::vector<uint8_t>& ops) {
    using handler_t = const uint8_t* (*)(QCBOREncodeContext*, const uint8_t*);

    // Static dispatch table indexed by enc_op value (0..23)
    static const handler_t dispatch[32] = {
        /*  0 open_map    */  [](QCBOREncodeContext* c, const uint8_t* p) { QCBOREncode_OpenMap(c);      return p; },
        /*  1 close_map   */  [](QCBOREncodeContext* c, const uint8_t* p) { QCBOREncode_CloseMap(c);     return p; },
        /*  2 open_array  */  [](QCBOREncodeContext* c, const uint8_t* p) { QCBOREncode_OpenArray(c);    return p; },
        /*  3 close_array */  [](QCBOREncodeContext* c, const uint8_t* p) { QCBOREncode_CloseArray(c);   return p; },
        /*  4..9          */  nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
        /* 10 add_int64  */   [](QCBOREncodeContext* c, const uint8_t* p) { int64_t v;  std::memcpy(&v, p, 8); QCBOREncode_AddInt64(c, v);  return p + 8; },
        /* 11 add_uint64 */   [](QCBOREncodeContext* c, const uint8_t* p) { uint64_t v; std::memcpy(&v, p, 8); QCBOREncode_AddUInt64(c, v); return p + 8; },
        /* 12 add_text   */   [](QCBOREncodeContext* c, const uint8_t* p) { uint16_t len; std::memcpy(&len, p, 2); UsefulBufC ub{p + 2, len}; QCBOREncode_AddText(c, ub);  return p + 2 + len; },
        /* 13 add_text_sz */  [](QCBOREncodeContext* c, const uint8_t* p) { uint16_t len; std::memcpy(&len, p, 2); UsefulBufC ub{p + 2, len}; QCBOREncode_AddText(c, ub);  return p + 2 + len; },
        /* 14 add_bytes  */   [](QCBOREncodeContext* c, const uint8_t* p) { uint16_t len; std::memcpy(&len, p, 2); UsefulBufC ub{p + 2, len}; QCBOREncode_AddBytes(c, ub); return p + 2 + len; },
        /* 15 add_double */   [](QCBOREncodeContext* c, const uint8_t* p) { double v;   std::memcpy(&v, p, 8); QCBOREncode_AddDouble(c, v);   return p + 8; },
        /* 16 add_float  */   [](QCBOREncodeContext* c, const uint8_t* p) { float v;    std::memcpy(&v, p, 4); QCBOREncode_AddFloat(c, v);    return p + 4; },
        /* 17 add_bool   */   [](QCBOREncodeContext* c, const uint8_t* p) { QCBOREncode_AddBool(c, *p != 0);                         return p + 1; },
        /* 18 add_null   */   [](QCBOREncodeContext* c, const uint8_t* p) { QCBOREncode_AddNULL(c);                                 return p; },
        /* 19 add_undef  */   [](QCBOREncodeContext* c, const uint8_t* p) { QCBOREncode_AddUndef(c);                                return p; },
        /* 20 add_tag    */   [](QCBOREncodeContext* c, const uint8_t* p) { uint64_t v; std::memcpy(&v, p, 8); QCBOREncode_AddTag(c, v);    return p + 8; },
        /* 21 add_simple */   [](QCBOREncodeContext* c, const uint8_t* p) { uint64_t v; std::memcpy(&v, p, 8); QCBOREncode_AddSimple(c, v); return p + 8; },
        /* 22 dbl_np    */    [](QCBOREncodeContext* c, const uint8_t* p) { double v;   std::memcpy(&v, p, 8); QCBOREncode_AddDoubleNoPreferred(c, v); return p + 8; },
        /* 23 flt_np    */    [](QCBOREncodeContext* c, const uint8_t* p) { float v;    std::memcpy(&v, p, 4); QCBOREncode_AddFloatNoPreferred(c, v);  return p + 4; },
    };

    const uint8_t* p = ops.data();
    const uint8_t* end = p + ops.size();
    while (p < end) {
        auto op = static_cast<uint8_t>(*p++);
        if (op < 32) {
            auto h = dispatch[op];
            if (h) p = h(ctx, p);
        }
    }
}

/** Append a single-byte op to the recording buffer. */
inline void record(std::vector<uint8_t>& buf, detail::enc_op op) {
    buf.push_back(static_cast<uint8_t>(op));
}

/** Append an op + int64 to the recording buffer. */
inline void record_i64(std::vector<uint8_t>& buf, detail::enc_op op, int64_t v) {
    buf.push_back(static_cast<uint8_t>(op));
    uint8_t tmp[8]; std::memcpy(tmp, &v, 8);
    buf.insert(buf.end(), tmp, tmp + 8);
}

/** Append an op + uint64 to the recording buffer. */
inline void record_u64(std::vector<uint8_t>& buf, detail::enc_op op, uint64_t v) {
    buf.push_back(static_cast<uint8_t>(op));
    uint8_t tmp[8]; std::memcpy(tmp, &v, 8);
    buf.insert(buf.end(), tmp, tmp + 8);
}

/** Append an op + text string to the recording buffer. */
inline void record_text(std::vector<uint8_t>& buf, detail::enc_op op, std::string_view s) {
    buf.push_back(static_cast<uint8_t>(op));
    uint16_t len = static_cast<uint16_t>(s.size());
    uint8_t ltmp[2]; std::memcpy(ltmp, &len, 2);
    buf.insert(buf.end(), ltmp, ltmp + 2);
    buf.insert(buf.end(), reinterpret_cast<const uint8_t*>(s.data()),
               reinterpret_cast<const uint8_t*>(s.data()) + s.size());
}

/** Append an op + byte span to the recording buffer. */
inline void record_bytes(std::vector<uint8_t>& buf, detail::enc_op op, const_byte_span b) {
    buf.push_back(static_cast<uint8_t>(op));
    uint16_t len = static_cast<uint16_t>(b.size());
    uint8_t ltmp[2]; std::memcpy(ltmp, &len, 2);
    buf.insert(buf.end(), ltmp, ltmp + 2);
    buf.insert(buf.end(), b.data(), b.data() + b.size());
}

/** Append a double value to the recording buffer. */
inline void record_double(std::vector<uint8_t>& buf, detail::enc_op op, double v) {
    buf.push_back(static_cast<uint8_t>(op));
    uint8_t tmp[8]; std::memcpy(tmp, &v, 8);
    buf.insert(buf.end(), tmp, tmp + 8);
}

/** Append a float value to the recording buffer. */
inline void record_float(std::vector<uint8_t>& buf, detail::enc_op op, float v) {
    buf.push_back(static_cast<uint8_t>(op));
    uint8_t tmp[4]; std::memcpy(tmp, &v, 4);
    buf.insert(buf.end(), tmp, tmp + 4);
}

/** Append a bool value to the recording buffer. */
inline void record_bool(std::vector<uint8_t>& buf, bool v) {
    buf.push_back(static_cast<uint8_t>(detail::enc_op::add_bool));
    buf.push_back(v ? 1 : 0);
}

/** Append a simple value to the recording buffer. */
inline void record_simple(std::vector<uint8_t>& buf, uint64_t v) {
    buf.push_back(static_cast<uint8_t>(detail::enc_op::add_simple));
    uint8_t tmp[8]; std::memcpy(tmp, &v, 8);
    buf.insert(buf.end(), tmp, tmp + 8);
}

} // namespace detail

// ============================================================================
// basic_key_proxy<Enc> -- DECLARATION ONLY (bodies after encoder)
// ============================================================================
//
// Emitted by basic_map_builder::operator[]. Holds a key and allows assigning
// a value, opening a nested map/array, or chaining sub-keys.

template<typename Enc>
class basic_key_proxy {
    friend class basic_map_builder<Enc>;
    Enc* enc_;
    bool owns_map_;
    bool closed_ = false;

    basic_key_proxy(Enc& e, bool owns_map = false) noexcept
        : enc_(&e), owns_map_(owns_map) {}

    void close_if_owns();

public:
    ~basic_key_proxy();
    basic_key_proxy(basic_key_proxy&& o) noexcept;
    basic_key_proxy& operator=(basic_key_proxy&& o) noexcept;
    basic_key_proxy(const basic_key_proxy&) = delete;
    basic_key_proxy& operator=(const basic_key_proxy&) = delete;

    /** @brief  Assign a signed 64-bit integer as the value for this key. */
    void operator=(int64_t v);
    /** @brief  Assign a signed 32-bit integer (promoted to int64_t). */
    void operator=(int v);
    /** @brief  Assign an unsigned 32-bit integer (promoted to uint64_t). */
    void operator=(unsigned int v);
    /** @brief  Assign an unsigned 64-bit integer. */
    void operator=(uint64_t v);
    /** @brief  Assign a UTF-8 text string. */
    void operator=(std::string_view v);
    /** @brief  Assign a null-terminated C string. */
    void operator=(const char* v);
    /** @brief  Assign an IEEE 754 double. */
    void operator=(double v);
    /** @brief  Assign a boolean. */
    void operator=(bool v);
    /** @brief  Assign the CBOR null literal. */
    void operator=(std::nullptr_t);
    /** @brief  Assign a byte string. */
    void operator=(const_byte_span v);
    /** @brief  Assign a std::chrono::system_clock::time_point as epoch date (tag 1). */
    void operator=(std::chrono::system_clock::time_point tp);
    /** @brief  Assign a std::chrono::duration as epoch days (tag 100). */
    template<typename Rep, typename Period>
    void operator=(std::chrono::duration<Rep, Period> d);

    /** @brief  Open a nested map as the value for this key. */
    basic_map_builder<Enc>   map();
    /** @brief  Open a nested array as the value for this key. */
    basic_array_builder<Enc> array();

    /** @brief  Chain to a nested map entry by text label. */
    basic_key_proxy operator[](std::string_view subkey);
    /** @brief  Chain to a nested map entry by integer label. */
    basic_key_proxy operator[](int64_t subkey);
    /** @brief  Chain to a nested map entry by null-terminated string label. */
    basic_key_proxy operator[](const char* subkey);
};

// ============================================================================
// basic_map_builder<Enc> -- DECLARATION ONLY (bodies after encoder)
// ============================================================================
//
// Scope guard for a CBOR map being built. Records close_map on destruction.

template<typename Enc>
class basic_map_builder {
    friend class encoder;
    friend class dynamic_encoder;
    friend class basic_key_proxy<Enc>;
    friend class basic_array_builder<Enc>;
    Enc* enc_;
    bool closed_ = false;

    explicit basic_map_builder(Enc& e) noexcept : enc_(&e) {}

public:
    ~basic_map_builder();
    basic_map_builder(basic_map_builder&& o) noexcept;
    basic_map_builder& operator=(basic_map_builder&& o) noexcept;
    basic_map_builder(const basic_map_builder&) = delete;
    basic_map_builder& operator=(const basic_map_builder&) = delete;

    /** @brief  Add or access a map entry by text label. */
    basic_key_proxy<Enc> operator[](std::string_view key);
    /** @brief  Add or access a map entry by integer label. */
    basic_key_proxy<Enc> operator[](int64_t key);
    /** @brief  Add or access a map entry by int label (promoted to int64_t). */
    basic_key_proxy<Enc> operator[](int key) { return (*this)[static_cast<int64_t>(key)]; }
    /** @brief  Add or access a map entry by null-terminated string label. */
    basic_key_proxy<Enc> operator[](const char* key);

    /** @brief  Merge key-value pairs from an initializer_list into this map.
     *
     * Appends entries without opening/closing the map. Typical use:
     * enc.map().merge({{"key1", 1}, {"key2", "hello"}});
     *
     * Each entry must be a cbor_ref pair. Map remains open after merge.
     */
    void merge(std::initializer_list<cbor_ref> items);

    /** @brief  Merge a single key-value pair. Sugar: m.merge("key", 42). */
    basic_map_builder& merge(std::string_view key, int v) { return merge(key, static_cast<int64_t>(v)); }
    basic_map_builder& merge(std::string_view key, int64_t v);
    basic_map_builder& merge(std::string_view key, std::string_view v);
    basic_map_builder& merge(std::string_view key, double v);
    basic_map_builder& merge(std::string_view key, bool v);
    basic_map_builder& merge(std::string_view key, std::nullptr_t);
};

// ============================================================================
// basic_array_builder<Enc> -- DECLARATION ONLY (bodies after encoder)
// ============================================================================
//
// Scope guard for a CBOR array being built. Records close_array on destruction.

template<typename Enc>
class basic_array_builder {
    friend class encoder;
    friend class dynamic_encoder;
    friend class basic_key_proxy<Enc>;
    friend class basic_map_builder<Enc>;
    Enc* enc_;
    bool closed_ = false;

    explicit basic_array_builder(Enc& e) noexcept : enc_(&e) {}

public:
    ~basic_array_builder();
    basic_array_builder(basic_array_builder&& o) noexcept;
    basic_array_builder& operator=(basic_array_builder&& o) noexcept;
    basic_array_builder(const basic_array_builder&) = delete;
    basic_array_builder& operator=(const basic_array_builder&) = delete;

    // ── add() overloads ──
    basic_array_builder& add(int64_t v);
    basic_array_builder& add(int v);
    basic_array_builder& add(unsigned int v);
    basic_array_builder& add(uint64_t v);
    basic_array_builder& add(std::string_view v);
    basic_array_builder& add(const char* v);
    basic_array_builder& add(double v);
    basic_array_builder& add(bool v);
    basic_array_builder& add(std::nullptr_t);
    basic_array_builder& add(const_byte_span v);

    // ── operator<< overloads (delegate to add) ──
    basic_array_builder& operator<<(int64_t v);
    basic_array_builder& operator<<(int v);
    basic_array_builder& operator<<(unsigned int v);
    basic_array_builder& operator<<(uint64_t v);
    basic_array_builder& operator<<(std::string_view v);
    basic_array_builder& operator<<(const char* v);
    basic_array_builder& operator<<(double v);
    basic_array_builder& operator<<(bool v);
    basic_array_builder& operator<<(std::nullptr_t v);

    /** @brief  Append a nested CBOR map to the array. */
    basic_map_builder<Enc> add_map();
    /** @brief  Append a nested CBOR array to the array. */
    basic_array_builder add_array();
};

// ============================================================================
// encoder -- static buffer, directly calls QCBOR C API
// ============================================================================

class encoder {
    template<typename Enc> friend class basic_key_proxy;
    template<typename Enc> friend class basic_map_builder;
    template<typename Enc> friend class basic_array_builder;
    friend class dynamic_encoder;

    QCBOREncodeContext  ctx_{};
    bool                finished_ = false;
    bool                top_set_  = false;
    bool                top_map_  = false;

    int proxy_map_depth_ = 0;

    void check_err() {
        QCBORError e = QCBOREncode_GetErrorState(&ctx_);
        if (e != QCBOR_SUCCESS)
            throw error(static_cast<errc>(e));
    }

    // ── init-list helpers ──────────────────────────────────────────────

    /** Determine whether a list of cbor_ref elements forms a map
     *  (all are 2-element [string, value] pairs).
     *  @return false for empty lists (empty → map is handled by caller). */
    static bool is_map_list(const cbor_ref* items, size_t count) noexcept {
        if (count == 0) return false;
        for (size_t i = 0; i < count; ++i) {
            if (items[i].kind_ != cbor_ref::kind::list_v
                || items[i].list_count() != 2
                || items[i].list_data()[0].kind_ != cbor_ref::kind::string_v)
                return false;
        }
        return true;
    }

    /** Encode a cbor_ref tree recursively into the encoder buffer. */
    void encode_ref(const cbor_ref& ref) {
        switch (ref.kind_) {
        // ── scalars ──
        case cbor_ref::kind::null_v:       add_null(); break;
        case cbor_ref::kind::bool_v:       add_bool(ref.b_); break;
        case cbor_ref::kind::int64_v:      add_int64(ref.i64_); break;
        case cbor_ref::kind::uint64_v:     add_uint64(ref.u64_); break;
        case cbor_ref::kind::double_v:     add_double(ref.d_); break;
        case cbor_ref::kind::string_v:
            add_text(std::string_view(ref.str_ptr_, ref.str_len_));
            break;
        case cbor_ref::kind::bytes_v:
            add_bytes(const_byte_span{ref.bytes_ptr_, ref.bytes_len_});
            break;
        case cbor_ref::kind::time_point_v:
            add_date_epoch(ref.i64_); break;
        case cbor_ref::kind::days_v:
            add_days_epoch(ref.i64_); break;

        // ── deferred inference: list_v ──
        case cbor_ref::kind::list_v: {
            auto* items = ref.list_data();
            auto  count = ref.list_count();
            if (is_map_list(items, count)) {
                open_map();
                for (size_t i = 0; i < count; ++i) {
                    auto& pair = items[i];
                    encode_ref(pair.list_data()[0]);  // key
                    encode_ref(pair.list_data()[1]);  // value
                }
                close_map();
            } else {
                open_array();
                for (size_t i = 0; i < count; ++i)
                    encode_ref(items[i]);
                close_array();
            }
            break;
        }

        // ── forced collection types ──
        case cbor_ref::kind::array_v: {
            auto* items = ref.list_data();
            auto  count = ref.list_count();
            open_array();
            for (size_t i = 0; i < count; ++i)
                encode_ref(items[i]);
            close_array();
            break;
        }
        case cbor_ref::kind::map_v: {
            auto* items = ref.list_data();
            auto  count = ref.list_count();
            open_map();
            for (size_t i = 0; i < count; ++i) {
                auto& pair = items[i];
                encode_ref(pair.list_data()[0]);  // key (string)
                encode_ref(pair.list_data()[1]);  // value
            }
            close_map();
            break;
        }
        case cbor_ref::kind::imap_v: {
            auto* items = ref.list_data();
            auto  count = ref.list_count();
            open_map();
            for (size_t i = 0; i < count; ++i) {
                auto& pair = items[i];
                encode_ref(pair.list_data()[0]);  // key (int)
                encode_ref(pair.list_data()[1]);  // value
            }
            close_map();
            break;
        }
        }
    }

public:
    int  proxy_map_depth() const noexcept { return proxy_map_depth_; }
    void inc_proxy_map_depth() noexcept  { ++proxy_map_depth_; }
    void dec_proxy_map_depth() noexcept  { if (proxy_map_depth_ > 0) --proxy_map_depth_; }
    /** @brief  Construct an encoder writing into a pre-allocated buffer. */
    explicit encoder(byte_span buf) {
        QCBOREncode_Init(&ctx_, UsefulBuf{buf.data(), buf.size()});
    }

    /** @brief  Construct an encoder and encode from an initializer list.

        The initializer list is traversed once and written directly into
        the CBOR buffer — no intermediate tree is built.

        @param  buf   pre-allocated output buffer
        @param  init  initializer list of cbor_ref values
        @throws error  if the buffer is exhausted or encoding fails

        Inference rules for nested initializer_lists:
        - If every element is a 2-element list whose first element is a
          string, the list encodes as a map (string keys).
        - Otherwise, the list encodes as an array.
        - Use arr(), map(), imap() factories to override inference. */
    encoder(byte_span buf, std::initializer_list<cbor_ref> init)
        : encoder(buf)
    {
        // Empty initializer list produces an empty map
        top_set_ = true; top_map_ = true;
        QCBOREncode_OpenMap(&ctx_);
        for (auto& elem : init) {
            // Each top-level element must be a 2-element [key, value] pair
            if (elem.kind_ != cbor_ref::kind::list_v || elem.list_count() != 2)
                throw error(errc::close_mismatch,
                    "encoder(init_list): top-level elements must be "
                    "{key, value} pairs");
            encode_ref(elem.list_data()[0]);  // key
            encode_ref(elem.list_data()[1]);  // value
        }
        QCBOREncode_CloseMap(&ctx_);
    }

    encoder(const encoder&) = delete;
    encoder& operator=(const encoder&) = delete;

    // ── top-level structure ──

    /** @brief  Start encoding as a CBOR map. */
    basic_map_builder<encoder> map() {
        if (top_set_) throw error(errc::close_mismatch);
        top_set_ = true; top_map_ = true;
        QCBOREncode_OpenMap(&ctx_);
        return basic_map_builder<encoder>{*this};
    }

    /** @brief  Start encoding as a CBOR array. */
    basic_array_builder<encoder> array() {
        if (top_set_) throw error(errc::close_mismatch);
        top_set_ = true; top_map_ = false;
        QCBOREncode_OpenArray(&ctx_);
        return basic_array_builder<encoder>{*this};
    }

    /** @brief  Check if the top-level structure is a map. */
    bool is_map()   const noexcept { return top_set_ && top_map_; }

    /** @brief  Check if the top-level structure is an array. */
    bool is_array() const noexcept { return top_set_ && !top_map_; }

    /** @brief  Complete encoding and return the encoded bytes. */
    const_byte_span finish() {
        if (finished_)
            throw error(errc::buffer_too_large);
        finished_ = true;
        UsefulBufC result;
        QCBORError err = QCBOREncode_Finish(&ctx_, &result);
        if (err != QCBOR_SUCCESS)
            throw error(static_cast<errc>(err));
        return {static_cast<const uint8_t*>(result.ptr), result.len};
    }

    // ── low-level direct-add API ──

    /** @brief  Record a signed 64-bit integer item. */
    encoder& add_int64(int64_t v) {
        QCBOREncode_AddInt64(&ctx_, v); check_err(); return *this;
    }
    /** @brief  Record an unsigned 64-bit integer item. */
    encoder& add_uint64(uint64_t v) {
        QCBOREncode_AddUInt64(&ctx_, v); check_err(); return *this;
    }
    /** @brief  Record a UTF-8 text string item. */
    encoder& add_text(std::string_view v) {
        UsefulBufC ub{reinterpret_cast<const void*>(v.data()), v.size()};
        QCBOREncode_AddText(&ctx_, ub); check_err(); return *this;
    }
    /** @brief  Record a UTF-8 text string from a null-terminated C string. */
    encoder& add_text(const char* v) {
        return add_text(std::string_view(v));
    }
    /** @brief  Record a byte string item. */
    encoder& add_bytes(const_byte_span v) {
        UsefulBufC ub{v.data(), v.size()};
        QCBOREncode_AddBytes(&ctx_, ub); check_err(); return *this;
    }
    /** @brief  Record an IEEE 754 double-precision float item. */
    encoder& add_double(double v) {
        QCBOREncode_AddDouble(&ctx_, v); check_err(); return *this;
    }
    /** @brief  Record an IEEE 754 single-precision float item. */
    encoder& add_float(float v) {
        QCBOREncode_AddFloat(&ctx_, v); check_err(); return *this;
    }
    /** @brief  Record a double item without shortest-representation optimization. */
    encoder& add_double_no_preferred(double v) {
        QCBOREncode_AddDoubleNoPreferred(&ctx_, v); check_err(); return *this;
    }
    /** @brief  Record a float item without shortest-representation optimization. */
    encoder& add_float_no_preferred(float v) {
        QCBOREncode_AddFloatNoPreferred(&ctx_, v); check_err(); return *this;
    }
    /** @brief  Record a boolean item. */
    encoder& add_bool(bool v) {
        QCBOREncode_AddBool(&ctx_, v); check_err(); return *this;
    }
    /** @brief  Record the CBOR null literal. */
    encoder& add_null() {
        QCBOREncode_AddNULL(&ctx_); check_err(); return *this;
    }
    /** @brief  Record the CBOR undefined literal. */
    encoder& add_undef() {
        QCBOREncode_AddUndef(&ctx_); check_err(); return *this;
    }
    /** @brief  Record an unrecognized simple value (0-19 or 32-255). */
    encoder& add_simple(uint64_t v) {
        QCBOREncode_AddSimple(&ctx_, v); check_err(); return *this;
    }
    /** @brief  Record a CBOR tag (major type 6). */
    encoder& add_tag(uint64_t tag) {
        QCBOREncode_AddTag(&ctx_, tag); check_err(); return *this;
    }
    /** @brief  Record an open-map marker. */
    encoder& open_map() {
        QCBOREncode_OpenMap(&ctx_); check_err(); return *this;
    }
    /** @brief  Record a close-map marker. */
    encoder& close_map() {
        QCBOREncode_CloseMap(&ctx_); check_err(); return *this;
    }
    /** @brief  Record an open-array marker. */
    encoder& open_array() {
        QCBOREncode_OpenArray(&ctx_); check_err(); return *this;
    }
    /** @brief  Record a close-array marker. */
    encoder& close_array() {
        QCBOREncode_CloseArray(&ctx_); check_err(); return *this;
    }

    // ── tagged semantic types ──

    /** @brief  Record an epoch-based date with optional tag 1. */
    encoder& add_date_epoch(int64_t sec, bool as_tag = true) {
        if (as_tag) add_tag(1);
        return add_int64(sec);
    }

    /** @brief  Record a std::chrono::system_clock::time_point as epoch date (tag 1). */
    encoder& add_date(std::chrono::system_clock::time_point tp, bool as_tag = true) {
        return add_date_epoch(
            std::chrono::duration_cast<std::chrono::seconds>(
                tp.time_since_epoch()).count(), as_tag);
    }

    /** @brief  Record days since epoch with optional tag 100. */
    encoder& add_days_epoch(int64_t days, bool as_tag = true) {
        if (as_tag) add_tag(100);
        return add_int64(days);
    }

    /** @brief  Record a std::chrono::duration in days as epoch days (tag 100).
     *
     * Accepts any duration type convertible to a day count (e.g.
     * std::chrono::duration<int, std::ratio<86400>>, or C++20
     * std::chrono::days).
     */
    template<typename Rep, typename Period>
    encoder& add_days(std::chrono::duration<Rep, Period> dur, bool as_tag = true) {
        return add_days_epoch(
            static_cast<int64_t>(
                std::chrono::duration_cast<
                    std::chrono::duration<int64_t, std::ratio<86400>>>(dur).count()),
            as_tag);
    }
    /** @brief  Record an RFC 3339 date string with optional tag 0. */
    encoder& add_date_string(std::string_view date, bool as_tag = true) {
        if (as_tag) add_tag(0);
        return add_text(date);
    }
    /** @brief  Record a days-count string with optional tag 1004. */
    encoder& add_days_string(std::string_view date, bool as_tag = true) {
        if (as_tag) add_tag(1004);
        return add_text(date);
    }
    /** @brief  Record a positive bignum with optional tag 2. */
    encoder& add_bignum_positive(const_byte_span bytes, bool as_tag = true) {
        if (as_tag) add_tag(2);
        return add_bytes(bytes);
    }
    /** @brief  Record a negative bignum with optional tag 3. */
    encoder& add_bignum_negative(const_byte_span bytes, bool as_tag = true) {
        if (as_tag) add_tag(3);
        return add_bytes(bytes);
    }
    /** @brief  Record a decimal fraction with optional tag 4. */
    encoder& add_decimal_fraction(int64_t mantissa, int64_t exp10, bool as_tag = true) {
        if (as_tag) add_tag(4);
        open_array();
        add_int64(exp10);
        add_int64(mantissa);
        close_array();
        return *this;
    }
    /** @brief  Record a decimal fraction with a bignum mantissa and optional tag 4. */
    encoder& add_decimal_fraction_bignum(const_byte_span mantissa, bool is_neg,
                                          int64_t exp10, bool as_tag = true) {
        if (as_tag) add_tag(4);
        open_array();
        add_int64(exp10);
        if (is_neg) add_tag(3);
        else        add_tag(2);
        add_bytes(mantissa);
        close_array();
        return *this;
    }
    /** @brief  Record a bigfloat with optional tag 5. */
    encoder& add_bigfloat(int64_t mantissa, int64_t exp2, bool as_tag = true) {
        if (as_tag) add_tag(5);
        open_array();
        add_int64(exp2);
        add_int64(mantissa);
        close_array();
        return *this;
    }
    /** @brief  Record a bigfloat with a bignum mantissa and optional tag 5. */
    encoder& add_bigfloat_bignum(const_byte_span mantissa, bool is_neg,
                                  int64_t exp2, bool as_tag = true) {
        if (as_tag) add_tag(5);
        open_array();
        add_int64(exp2);
        if (is_neg) add_tag(3);
        else        add_tag(2);
        add_bytes(mantissa);
        close_array();
        return *this;
    }
    /** @brief  Record a URI with optional tag 32. */
    encoder& add_uri(std::string_view uri, bool as_tag = true) {
        if (as_tag) add_tag(32);
        return add_text(uri);
    }
    /** @brief  Record a base64-encoded string with optional tag 34. */
    encoder& add_b64_text(std::string_view b64, bool as_tag = true) {
        if (as_tag) add_tag(34);
        return add_text(b64);
    }
    /** @brief  Record a base64url-encoded string with optional tag 33. */
    encoder& add_b64url_text(std::string_view b64url, bool as_tag = true) {
        if (as_tag) add_tag(33);
        return add_text(b64url);
    }
    /** @brief  Record a regular expression pattern with optional tag 35. */
    encoder& add_regex(std::string_view regex, bool as_tag = true) {
        if (as_tag) add_tag(35);
        return add_text(regex);
    }
    /** @brief  Record MIME-encoded data with optional tag 36. */
    encoder& add_mime_data(std::string_view data, bool as_tag = true) {
        if (as_tag) add_tag(36);
        return add_text(data);
    }
    /** @brief  Record a binary UUID with optional tag 37. */
    encoder& add_binary_uuid(const_byte_span uuid, bool as_tag = true) {
        if (as_tag) add_tag(37);
        return add_bytes(uuid);
    }

    // ── encoded insertion ──

    /** @brief  Insert pre-encoded CBOR bytes verbatim. */
    encoder& add_encoded(const_byte_span encoded) {
        return add_bytes(encoded);
    }
};

// ============================================================================
// basic_key_proxy<Enc> INLINE BODIES
// ============================================================================

template<typename Enc>
inline void basic_key_proxy<Enc>::close_if_owns() {
    if (owns_map_ && !closed_) {
        enc_->close_map();
        enc_->dec_proxy_map_depth();
        closed_ = true;
    }
}

template<typename Enc>
inline basic_key_proxy<Enc>::~basic_key_proxy() { close_if_owns(); }

template<typename Enc>
inline basic_key_proxy<Enc>::basic_key_proxy(basic_key_proxy&& o) noexcept
    : enc_(o.enc_), owns_map_(o.owns_map_), closed_(o.closed_)
{ o.closed_ = true; }

template<typename Enc>
inline basic_key_proxy<Enc>& basic_key_proxy<Enc>::operator=(basic_key_proxy&& o) noexcept {
    if (this != &o) {
        close_if_owns();
        enc_ = o.enc_; owns_map_ = o.owns_map_; closed_ = o.closed_;
        o.closed_ = true;
    }
    return *this;
}

template<typename Enc>
inline void basic_key_proxy<Enc>::operator=(int64_t v) {
    close_if_owns();
    enc_->add_int64(v);
}
template<typename Enc>
inline void basic_key_proxy<Enc>::operator=(int v) {
    close_if_owns();
    enc_->add_int64(static_cast<int64_t>(v));
}
template<typename Enc>
inline void basic_key_proxy<Enc>::operator=(unsigned int v) {
    close_if_owns();
    enc_->add_uint64(static_cast<uint64_t>(v));
}
template<typename Enc>
inline void basic_key_proxy<Enc>::operator=(uint64_t v) {
    close_if_owns();
    enc_->add_uint64(v);
}
template<typename Enc>
inline void basic_key_proxy<Enc>::operator=(std::string_view v) {
    close_if_owns();
    enc_->add_text(v);
}
template<typename Enc>
inline void basic_key_proxy<Enc>::operator=(const char* v) {
    close_if_owns();
    enc_->add_text(v);
}
template<typename Enc>
inline void basic_key_proxy<Enc>::operator=(double v) {
    close_if_owns();
    enc_->add_double(v);
}
template<typename Enc>
inline void basic_key_proxy<Enc>::operator=(bool v) {
    close_if_owns();
    enc_->add_bool(v);
}
template<typename Enc>
inline void basic_key_proxy<Enc>::operator=(std::nullptr_t) {
    close_if_owns();
    enc_->add_null();
}
template<typename Enc>
inline void basic_key_proxy<Enc>::operator=(const_byte_span v) {
    close_if_owns();
    enc_->add_bytes(v);
}

template<typename Enc>
inline void basic_key_proxy<Enc>::operator=(std::chrono::system_clock::time_point tp) {
    close_if_owns();
    enc_->add_date(tp);
}

template<typename Enc>
template<typename Rep, typename Period>
inline void basic_key_proxy<Enc>::operator=(std::chrono::duration<Rep, Period> d) {
    close_if_owns();
    enc_->add_days(d);
}

template<typename Enc>
inline basic_map_builder<Enc> basic_key_proxy<Enc>::map() {
    close_if_owns();
    enc_->open_map();
    return basic_map_builder<Enc>{*enc_};
}

template<typename Enc>
inline basic_array_builder<Enc> basic_key_proxy<Enc>::array() {
    close_if_owns();
    enc_->open_array();
    return basic_array_builder<Enc>{*enc_};
}

template<typename Enc>
inline basic_key_proxy<Enc> basic_key_proxy<Enc>::operator[](std::string_view subkey) {
    close_if_owns();
    owns_map_ = true;
    enc_->open_map();
    enc_->add_text(subkey);
    enc_->inc_proxy_map_depth();
    return basic_key_proxy<Enc>{*enc_, false};
}
template<typename Enc>
inline basic_key_proxy<Enc> basic_key_proxy<Enc>::operator[](int64_t subkey) {
    close_if_owns();
    owns_map_ = true;
    enc_->open_map();
    enc_->add_int64(subkey);
    enc_->inc_proxy_map_depth();
    return basic_key_proxy<Enc>{*enc_, false};
}
template<typename Enc>
inline basic_key_proxy<Enc> basic_key_proxy<Enc>::operator[](const char* subkey) {
    return (*this)[std::string_view(subkey)];
}

// ============================================================================
// basic_map_builder<Enc> INLINE BODIES
// ============================================================================

template<typename Enc>
inline basic_map_builder<Enc>::~basic_map_builder() {
    if (enc_ && !closed_) {
        enc_->close_map();
    }
}

template<typename Enc>
inline basic_map_builder<Enc>::basic_map_builder(basic_map_builder&& o) noexcept
    : enc_(o.enc_), closed_(o.closed_) { o.enc_ = nullptr; }

template<typename Enc>
inline basic_map_builder<Enc>& basic_map_builder<Enc>::operator=(basic_map_builder&& o) noexcept {
    if (this != &o) {
        if (enc_ && !closed_) { enc_->close_map(); }
        enc_ = o.enc_; closed_ = o.closed_; o.enc_ = nullptr;
    }
    return *this;
}

template<typename Enc>
inline basic_key_proxy<Enc> basic_map_builder<Enc>::operator[](std::string_view key) {
    if (enc_->proxy_map_depth() > 0)
        throw error(errc::close_mismatch,
            "basic_map_builder::operator[]: a key_proxy owns an "
            "unclosed nested map — finish or destroy its key_proxy first");
    enc_->add_text(key);
    return basic_key_proxy<Enc>{*enc_};
}
template<typename Enc>
inline basic_key_proxy<Enc> basic_map_builder<Enc>::operator[](int64_t key) {
    if (enc_->proxy_map_depth() > 0)
        throw error(errc::close_mismatch,
            "basic_map_builder::operator[]: a key_proxy owns an "
            "unclosed nested map — finish or destroy its key_proxy first");
    enc_->add_int64(key);
    return basic_key_proxy<Enc>{*enc_};
}
template<typename Enc>
inline basic_key_proxy<Enc> basic_map_builder<Enc>::operator[](const char* key) {
    return (*this)[std::string_view(key)];
}

// ── merge ──

template<typename Enc>
inline void basic_map_builder<Enc>::merge(std::initializer_list<cbor_ref> items) {
    for (auto& ref : items) {
        if (ref.list_count() < 2) continue;       // not a pair
        auto& k = ref.list_data()[0];
        auto& v = ref.list_data()[1];
        if (k.kind_ != cbor_ref::kind::string_v) continue;
        auto key_proxy = (*this)[std::string_view{k.str_ptr_, k.str_len_}];
        // Dispatch value by type
        switch (v.kind_) {
            case cbor_ref::kind::int64_v:   key_proxy = v.i64_; break;
            case cbor_ref::kind::double_v:  key_proxy = v.d_;   break;
            case cbor_ref::kind::string_v:  key_proxy = std::string_view{v.str_ptr_, v.str_len_}; break;
            case cbor_ref::kind::bool_v:    key_proxy = v.b_;   break;
            case cbor_ref::kind::null_v:    key_proxy = nullptr; break;
            case cbor_ref::kind::map_v:     {
                auto nested = key_proxy.map();
                nested.merge({v.list_data(), v.list_data() + v.list_count()});
                break;
            }
            case cbor_ref::kind::array_v:   {
                auto nested = key_proxy.array();
                for (size_t i = 0; i < v.list_count(); ++i) {
                    auto& elem = v.list_data()[i];
                    switch (elem.kind_) {
                        case cbor_ref::kind::int64_v:   nested.add(elem.i64_); break;
                        case cbor_ref::kind::double_v:  nested.add(elem.d_);   break;
                        case cbor_ref::kind::string_v:  nested.add(std::string_view{elem.str_ptr_, elem.str_len_}); break;
                        case cbor_ref::kind::bool_v:    nested.add(elem.b_);   break;
                        case cbor_ref::kind::null_v:    nested.add(nullptr);   break;
                        default: break;
                    }
                }
                break;
            }
            default: break;
        }
    }
}

template<typename Enc>
inline basic_map_builder<Enc>& basic_map_builder<Enc>::merge(std::string_view key, int64_t v) {
    (*this)[key] = v; return *this;
}
template<typename Enc>
inline basic_map_builder<Enc>& basic_map_builder<Enc>::merge(std::string_view key, std::string_view v) {
    (*this)[key] = v; return *this;
}
template<typename Enc>
inline basic_map_builder<Enc>& basic_map_builder<Enc>::merge(std::string_view key, double v) {
    (*this)[key] = v; return *this;
}
template<typename Enc>
inline basic_map_builder<Enc>& basic_map_builder<Enc>::merge(std::string_view key, bool v) {
    (*this)[key] = v; return *this;
}
template<typename Enc>
inline basic_map_builder<Enc>& basic_map_builder<Enc>::merge(std::string_view key, std::nullptr_t) {
    (*this)[key] = nullptr; return *this;
}

// ============================================================================
// basic_array_builder<Enc> INLINE BODIES
// ============================================================================

template<typename Enc>
inline basic_array_builder<Enc>::~basic_array_builder() {
    if (enc_ && !closed_) {
        enc_->close_array();
    }
}

template<typename Enc>
inline basic_array_builder<Enc>::basic_array_builder(basic_array_builder&& o) noexcept
    : enc_(o.enc_), closed_(o.closed_) { o.enc_ = nullptr; }

template<typename Enc>
inline basic_array_builder<Enc>& basic_array_builder<Enc>::operator=(basic_array_builder&& o) noexcept {
    if (this != &o) {
        if (!closed_) { enc_->close_array(); }
        enc_ = o.enc_; closed_ = o.closed_; o.enc_ = nullptr;
    }
    return *this;
}

template<typename Enc>
inline basic_array_builder<Enc>& basic_array_builder<Enc>::add(int64_t v) {
    enc_->add_int64(v);
    return *this;
}
template<typename Enc>
inline basic_array_builder<Enc>& basic_array_builder<Enc>::add(int v) {
    enc_->add_int64(static_cast<int64_t>(v));
    return *this;
}
template<typename Enc>
inline basic_array_builder<Enc>& basic_array_builder<Enc>::add(unsigned int v) {
    enc_->add_uint64(static_cast<uint64_t>(v));
    return *this;
}
template<typename Enc>
inline basic_array_builder<Enc>& basic_array_builder<Enc>::add(uint64_t v) {
    enc_->add_uint64(v);
    return *this;
}
template<typename Enc>
inline basic_array_builder<Enc>& basic_array_builder<Enc>::add(std::string_view v) {
    enc_->add_text(v);
    return *this;
}
template<typename Enc>
inline basic_array_builder<Enc>& basic_array_builder<Enc>::add(const char* v) {
    return add(std::string_view(v));
}
template<typename Enc>
inline basic_array_builder<Enc>& basic_array_builder<Enc>::add(double v) {
    enc_->add_double(v);
    return *this;
}
template<typename Enc>
inline basic_array_builder<Enc>& basic_array_builder<Enc>::add(bool v) {
    enc_->add_bool(v);
    return *this;
}
template<typename Enc>
inline basic_array_builder<Enc>& basic_array_builder<Enc>::add(std::nullptr_t) {
    enc_->add_null();
    return *this;
}
template<typename Enc>
inline basic_array_builder<Enc>& basic_array_builder<Enc>::add(const_byte_span v) {
    enc_->add_bytes(v);
    return *this;
}

template<typename Enc>
inline basic_array_builder<Enc>& basic_array_builder<Enc>::operator<<(int64_t v)          { return add(v); }
template<typename Enc>
inline basic_array_builder<Enc>& basic_array_builder<Enc>::operator<<(int v)              { return add(v); }
template<typename Enc>
inline basic_array_builder<Enc>& basic_array_builder<Enc>::operator<<(unsigned int v)     { return add(v); }
template<typename Enc>
inline basic_array_builder<Enc>& basic_array_builder<Enc>::operator<<(uint64_t v)         { return add(v); }
template<typename Enc>
inline basic_array_builder<Enc>& basic_array_builder<Enc>::operator<<(std::string_view v) { return add(v); }
template<typename Enc>
inline basic_array_builder<Enc>& basic_array_builder<Enc>::operator<<(const char* v)      { return add(v); }
template<typename Enc>
inline basic_array_builder<Enc>& basic_array_builder<Enc>::operator<<(double v)           { return add(v); }
template<typename Enc>
inline basic_array_builder<Enc>& basic_array_builder<Enc>::operator<<(bool v)             { return add(v); }
template<typename Enc>
inline basic_array_builder<Enc>& basic_array_builder<Enc>::operator<<(std::nullptr_t v)   { return add(v); }

template<typename Enc>
inline basic_map_builder<Enc> basic_array_builder<Enc>::add_map() {
    enc_->open_map();
    return basic_map_builder<Enc>{*enc_};
}
template<typename Enc>
inline basic_array_builder<Enc> basic_array_builder<Enc>::add_array() {
    enc_->open_array();
    return basic_array_builder<Enc>{*enc_};
}

// ============================================================================
// Type aliases (default to static encoder)
// ============================================================================

using key_proxy     = basic_key_proxy<encoder>;
using map_builder   = basic_map_builder<encoder>;
using array_builder = basic_array_builder<encoder>;

} // namespace qcborpp

#endif // QCBORPP_ENCODER_HPP
