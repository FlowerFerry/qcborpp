/*
 * qcborpp/dynamic_encoder.hpp -- C++ dynamic-buffer encoder (deferred encoding)
 *
 * Copyright (c) 2024
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * This is the original qcborpp encoder. It records operations into an
 * ops_ bytecode buffer and replays them twice in flush_encode() — once
 * to calculate size, once to produce output. Use encoder (encoder.hpp)
 * when you can provide a pre-allocated buffer.
 */

#ifndef QCBORPP_DYNAMIC_ENCODER_HPP
#define QCBORPP_DYNAMIC_ENCODER_HPP

#include "qcborpp/encoder.hpp"

namespace qcborpp {

// Forward declaration for friend access
class dynamic_encoder;

// ============================================================================
// dynamic_encoder — original deferred encoder, now renamed
// ============================================================================

class dynamic_encoder {
    // Friend the template builder classes so they can access private
    // open_map/close_map/open_array/close_array via enc_->close_map() etc.
    template<typename Enc> friend class basic_key_proxy;
    template<typename Enc> friend class basic_map_builder;
    template<typename Enc> friend class basic_array_builder;

    std::vector<uint8_t> ops_;   // recorded operations
    std::vector<uint8_t> buf_;   // final encoded output
    size_t   size_estimate_ = 0; // running size estimate (avoids Phase 1)
    bool finished_ = false;
    bool top_set_  = false;
    bool top_map_  = false;
    int  proxy_map_depth_ = 0;

    /** True when size estimate needs Phase 1 fallback (i.e. float/double preferred). */
    bool has_variable_op_ = false;

    /**
     * Replay recorded ops into a QCBOR context, producing output.
     *
     * Fast path: when no variable-size operations (float/double preferred),
     * size_estimate_ is exact — skip Phase 1 entirely.
     *
     * Slow path (float/double present): Phase 1 virtual context, Phase 2 real.
     */
    void flush_encode() {
        if (!has_variable_op_) {
            // Fast path: size_estimate_ is deterministic for non-float
            // ops, but container headers depend on item count (not
            // known at open time). Add margin: each open/close pair
            // may contribute up to 9 bytes (5 header + size field).
            buf_.resize(size_estimate_ + ops_.size());
            QCBOREncodeContext real_ctx;
            QCBOREncode_Init(&real_ctx,
                             UsefulBuf{buf_.data(), buf_.size()});
            detail::replay_ops(&real_ctx, ops_);
            UsefulBufC result;
            QCBORError err = QCBOREncode_Finish(&real_ctx, &result);
            if (err != QCBOR_SUCCESS)
                throw error(static_cast<errc>(err));
            buf_.resize(result.len);
        } else {
            // Slow path: float/double preferred — Phase 1 calc, Phase 2 encode
            UsefulBuf size_buf = {nullptr, SIZE_MAX};
            {
                QCBOREncodeContext calc_ctx;
                QCBOREncode_Init(&calc_ctx, size_buf);
                detail::replay_ops(&calc_ctx, ops_);
                size_t needed;
                QCBORError err = QCBOREncode_FinishGetSize(&calc_ctx, &needed);
                if (err != QCBOR_SUCCESS)
                    throw error(static_cast<errc>(err));
                buf_.resize(needed);
            }
            {
                QCBOREncodeContext real_ctx;
                QCBOREncode_Init(&real_ctx,
                                 UsefulBuf{buf_.data(), buf_.size()});
                detail::replay_ops(&real_ctx, ops_);
                UsefulBufC result;
                QCBORError err = QCBOREncode_Finish(&real_ctx, &result);
                if (err != QCBOR_SUCCESS)
                    throw error(static_cast<errc>(err));
                buf_.resize(result.len);
            }
        }
    }

public:
    int  proxy_map_depth() const noexcept { return proxy_map_depth_; }
    void inc_proxy_map_depth() noexcept  { ++proxy_map_depth_; }
    void dec_proxy_map_depth() noexcept  { if (proxy_map_depth_ > 0) --proxy_map_depth_; }

    /** Construct with default 1024-byte pre-allocated recording buffer. */
    dynamic_encoder() { buf_.reserve(1024); ops_.reserve(1024); }

    /** Construct with reserved recording buffer size. */
    explicit dynamic_encoder(size_t reserve_size) {
        buf_.reserve(reserve_size);
        ops_.reserve(reserve_size);
    }

    /** @brief  Construct and encode from an initializer list.

        The list is traversed once and operations are recorded into
        the dynamic encoder's internal buffer — the encoding is resolved
        only when finish() is called. */
    explicit dynamic_encoder(std::initializer_list<cbor_ref> init)
        : dynamic_encoder()
    {
        // Wrap in a map — all CBOR must be a map or array at top level
        auto m = map();
        for (auto& elem : init) {
            if (elem.kind_ != cbor_ref::kind::list_v || elem.list_count() != 2)
                throw error(errc::close_mismatch,
                    "dynamic_encoder(init_list): top-level elements must be "
                    "{key, value} pairs");
            write_ref(elem.list_data()[0]);
            write_ref(elem.list_data()[1]);
        }
    }

private:
    /** Write a cbor_ref tree into the dynamic encoder via recorded ops. */
    void write_ref(const cbor_ref& ref) {
        switch (ref.kind_) {
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
        case cbor_ref::kind::time_point_v: add_date_epoch(ref.i64_); break;
        case cbor_ref::kind::days_v:       add_days_epoch(ref.i64_); break;
        case cbor_ref::kind::list_v: {
            auto* items = ref.list_data();
            auto  count = ref.list_count();
            if (is_map_list(items, count)) {
                open_map();
                for (size_t i = 0; i < count; ++i) {
                    auto& pair = items[i];
                    write_ref(pair.list_data()[0]);
                    write_ref(pair.list_data()[1]);
                }
                close_map();
            } else {
                open_array();
                for (size_t i = 0; i < count; ++i) write_ref(items[i]);
                close_array();
            }
            break;
        }
        case cbor_ref::kind::array_v: {
            auto* items = ref.list_data();
            auto  count = ref.list_count();
            open_array();
            for (size_t i = 0; i < count; ++i) write_ref(items[i]);
            close_array();
            break;
        }
        case cbor_ref::kind::map_v: {
            auto* items = ref.list_data();
            auto  count = ref.list_count();
            open_map();
            for (size_t i = 0; i < count; ++i) {
                auto& pair = items[i];
                write_ref(pair.list_data()[0]);
                write_ref(pair.list_data()[1]);
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
                write_ref(pair.list_data()[0]);
                write_ref(pair.list_data()[1]);
            }
            close_map();
            break;
        }
        }
    }

    /** Determine whether a list of cbor_ref elements forms a map. */
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

public:
    /** Pre-allocate internal buffer capacity. */
    void reserve(size_t n) {
        buf_.reserve(n);
        ops_.reserve(n);
    }

    size_t capacity() const noexcept { return buf_.capacity(); }

    dynamic_encoder(const dynamic_encoder&) = delete;
    dynamic_encoder& operator=(const dynamic_encoder&) = delete;

    // ── top-level structure ──

    basic_map_builder<dynamic_encoder> map() {
        if (top_set_) throw error(errc::close_mismatch);
        top_set_ = true; top_map_ = true;
        open_map();
        return basic_map_builder<dynamic_encoder>{*this};
    }

    basic_array_builder<dynamic_encoder> array() {
        if (top_set_) throw error(errc::close_mismatch);
        top_set_ = true; top_map_ = false;
        open_array();
        return basic_array_builder<dynamic_encoder>{*this};
    }

    bool is_map()   const noexcept { return top_set_ && top_map_; }
    bool is_array() const noexcept { return top_set_ && !top_map_; }

    const_byte_span finish() {
        if (finished_)
            throw error(errc::buffer_too_large);
        finished_ = true;
        flush_encode();
        return {buf_.data(), buf_.size()};
    }

    const_byte_span data() const noexcept {
        return {buf_.data(), buf_.size()};
    }

    // ── low-level direct-add API ──

    dynamic_encoder& add_int64(int64_t v) {
        size_estimate_ += detail::cbor_int64_size(v);
        detail::record_i64(ops_, detail::enc_op::add_int64, v); return *this;
    }
    dynamic_encoder& add_uint64(uint64_t v) {
        size_estimate_ += detail::cbor_uint64_size(v);
        detail::record_u64(ops_, detail::enc_op::add_uint64, v); return *this;
    }
    dynamic_encoder& add_text(std::string_view v) {
        size_estimate_ += detail::cbor_string_overhead(v.size()) + v.size();
        detail::record_text(ops_, detail::enc_op::add_text, v); return *this;
    }
    dynamic_encoder& add_text(const char* v) {
        return add_text(std::string_view(v));
    }
    dynamic_encoder& add_bytes(const_byte_span v) {
        size_estimate_ += detail::cbor_string_overhead(v.size()) + v.size();
        detail::record_bytes(ops_, detail::enc_op::add_bytes, v); return *this;
    }
    /** Zero-copy text add — stores (pointer, len), not data. Caller ensures data outlives finish(). */
    dynamic_encoder& add_text_ref(std::string_view s) {
        size_estimate_ += detail::cbor_string_overhead(s.size()) + s.size();
        detail::record_text_ref(ops_, s); return *this;
    }
    /** Zero-copy byte add — stores (pointer, len), not data. Caller ensures data outlives finish(). */
    dynamic_encoder& add_bytes_ref(const_byte_span b) {
        size_estimate_ += detail::cbor_string_overhead(b.size()) + b.size();
        detail::record_bytes_ref(ops_, b); return *this;
    }
    dynamic_encoder& add_double(double v) {
        has_variable_op_ = true;
        detail::record_double(ops_, detail::enc_op::add_double, v); return *this;
    }
    dynamic_encoder& add_float(float v) {
        has_variable_op_ = true;
        detail::record_float(ops_, detail::enc_op::add_float, v); return *this;
    }
    dynamic_encoder& add_double_no_preferred(double v) {
        size_estimate_ += 9; // CBOR fixed float64
        detail::record_double(ops_, detail::enc_op::add_double_np, v); return *this;
    }
    dynamic_encoder& add_float_no_preferred(float v) {
        size_estimate_ += 5; // CBOR fixed float32
        detail::record_float(ops_, detail::enc_op::add_float_np, v); return *this;
    }
    dynamic_encoder& add_bool(bool v) {
        size_estimate_ += 1;
        detail::record_bool(ops_, v); return *this;
    }
    dynamic_encoder& add_null() {
        size_estimate_ += 1;
        detail::record(ops_, detail::enc_op::add_null); return *this;
    }
    dynamic_encoder& add_undef() {
        size_estimate_ += 1;
        detail::record(ops_, detail::enc_op::add_undef); return *this;
    }
    dynamic_encoder& add_simple(uint64_t v) {
        size_estimate_ += detail::cbor_uint64_size(v);
        detail::record_simple(ops_, v); return *this;
    }
    dynamic_encoder& add_tag(uint64_t tag) {
        size_estimate_ += detail::cbor_uint64_size(tag);
        detail::record_u64(ops_, detail::enc_op::add_tag, tag); return *this;
    }
    dynamic_encoder& open_map() {
        // container header size depends on item count (backpatched at close)
        // — handled by margin + fallback
        detail::record(ops_, detail::enc_op::open_map); return *this;
    }
    dynamic_encoder& close_map() {
        detail::record(ops_, detail::enc_op::close_map); return *this;
    }
    dynamic_encoder& open_array() {
        detail::record(ops_, detail::enc_op::open_array); return *this;
    }
    dynamic_encoder& close_array() {
        detail::record(ops_, detail::enc_op::close_array); return *this;
    }

    // ── tagged semantic types ──

    dynamic_encoder& add_date_epoch(int64_t sec, bool as_tag = true) {
        if (as_tag) add_tag(1);
        return add_int64(sec);
    }

    /** @brief  Record a std::chrono::system_clock::time_point as epoch date (tag 1). */
    dynamic_encoder& add_date(std::chrono::system_clock::time_point tp, bool as_tag = true) {
        return add_date_epoch(
            std::chrono::duration_cast<std::chrono::seconds>(
                tp.time_since_epoch()).count(), as_tag);
    }

    dynamic_encoder& add_days_epoch(int64_t days, bool as_tag = true) {
        if (as_tag) add_tag(100);
        return add_int64(days);
    }

    /** @brief  Record a std::chrono::duration in days as epoch days (tag 100). */
    template<typename Rep, typename Period>
    dynamic_encoder& add_days(std::chrono::duration<Rep, Period> dur, bool as_tag = true) {
        return add_days_epoch(
            static_cast<int64_t>(
                std::chrono::duration_cast<
                    std::chrono::duration<int64_t, std::ratio<86400>>>(dur).count()),
            as_tag);
    }
    dynamic_encoder& add_date_string(std::string_view date, bool as_tag = true) {
        if (as_tag) add_tag(0);
        return add_text(date);
    }
    dynamic_encoder& add_days_string(std::string_view date, bool as_tag = true) {
        if (as_tag) add_tag(1004);
        return add_text(date);
    }
    dynamic_encoder& add_bignum_positive(const_byte_span bytes, bool as_tag = true) {
        if (as_tag) add_tag(2);
        return add_bytes(bytes);
    }
    dynamic_encoder& add_bignum_negative(const_byte_span bytes, bool as_tag = true) {
        if (as_tag) add_tag(3);
        return add_bytes(bytes);
    }
    dynamic_encoder& add_decimal_fraction(int64_t mantissa, int64_t exp10, bool as_tag = true) {
        if (as_tag) add_tag(4);
        open_array();
        add_int64(exp10);
        add_int64(mantissa);
        close_array();
        return *this;
    }
    dynamic_encoder& add_decimal_fraction_bignum(const_byte_span mantissa, bool is_neg,
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
    dynamic_encoder& add_bigfloat(int64_t mantissa, int64_t exp2, bool as_tag = true) {
        if (as_tag) add_tag(5);
        open_array();
        add_int64(exp2);
        add_int64(mantissa);
        close_array();
        return *this;
    }
    dynamic_encoder& add_bigfloat_bignum(const_byte_span mantissa, bool is_neg,
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
    dynamic_encoder& add_uri(std::string_view uri, bool as_tag = true) {
        if (as_tag) add_tag(32);
        return add_text(uri);
    }
    dynamic_encoder& add_b64_text(std::string_view b64, bool as_tag = true) {
        if (as_tag) add_tag(34);
        return add_text(b64);
    }
    dynamic_encoder& add_b64url_text(std::string_view b64url, bool as_tag = true) {
        if (as_tag) add_tag(33);
        return add_text(b64url);
    }
    dynamic_encoder& add_regex(std::string_view regex, bool as_tag = true) {
        if (as_tag) add_tag(35);
        return add_text(regex);
    }
    dynamic_encoder& add_mime_data(std::string_view data, bool as_tag = true) {
        if (as_tag) add_tag(36);
        return add_text(data);
    }
    dynamic_encoder& add_binary_uuid(const_byte_span uuid, bool as_tag = true) {
        if (as_tag) add_tag(37);
        return add_bytes(uuid);
    }

    // ── encoded insertion ──

    dynamic_encoder& add_encoded(const_byte_span encoded) {
        size_estimate_ += encoded.size();
        detail::record_bytes(ops_, detail::enc_op::add_encoded, encoded);
        return *this;
    }
    /** @brief  Insert pre-encoded CBOR via encoded_item strong type. */
    dynamic_encoder& add_encoded(encoded_item item) {
        return add_encoded(item.bytes);
    }
};

// ============================================================================
// Type aliases for dynamic_encoder
// ============================================================================

using dynamic_key_proxy     = basic_key_proxy<dynamic_encoder>;
using dynamic_map_builder   = basic_map_builder<dynamic_encoder>;
using dynamic_array_builder = basic_array_builder<dynamic_encoder>;

} // namespace qcborpp

#endif // QCBORPP_DYNAMIC_ENCODER_HPP
