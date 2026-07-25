/*
 * qcborpp/types.hpp -- Common types and aliases
 *
 * Copyright (c) 2024
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef QCBORPP_TYPES_HPP
#define QCBORPP_TYPES_HPP

#include <string_view>
#include <cstdint>
#include <optional>
#include <cstddef>

namespace qcborpp {

/** Mutable byte span (C++17-compatible, no std::span dependency). */
struct byte_span {
    uint8_t* ptr = nullptr;
    size_t   len = 0;

    constexpr byte_span() = default;
    constexpr byte_span(uint8_t* p, size_t n) noexcept : ptr(p), len(n) {}

    constexpr uint8_t*       data()       noexcept { return ptr; }
    constexpr const uint8_t* data() const noexcept { return ptr; }
    constexpr size_t         size() const noexcept { return len; }
    constexpr bool           empty() const noexcept { return len == 0; }
    constexpr uint8_t&       operator[](size_t i)       noexcept { return ptr[i]; }
    constexpr const uint8_t& operator[](size_t i) const noexcept { return ptr[i]; }
};

/** Immutable byte span (C++17-compatible). */
struct const_byte_span {
    const uint8_t* ptr = nullptr;
    size_t         len = 0;

    constexpr const_byte_span() = default;
    constexpr const_byte_span(const uint8_t* p, size_t n) noexcept : ptr(p), len(n) {}
    // implicit conversion from mutable span
    constexpr const_byte_span(byte_span s) noexcept : ptr(s.ptr), len(s.len) {}

    constexpr const uint8_t* data()  const noexcept { return ptr; }
    constexpr size_t         size()  const noexcept { return len; }
    constexpr bool           empty() const noexcept { return len == 0; }
    constexpr const uint8_t& operator[](size_t i) const noexcept { return ptr[i]; }
    constexpr const uint8_t* begin()  const noexcept { return ptr; }
    constexpr const uint8_t* end()    const noexcept { return ptr + len; }
};

/** Decode mode options, matching QCBORDecodeMode. */
enum class decode_mode : uint8_t {
    normal           = 0,
    map_strings_only = 1,
    map_as_array     = 2,
};

/** CBOR major types. */
enum class major_type : uint8_t {
    positive_int = 0,
    negative_int = 1,
    byte_string  = 2,
    text_string  = 3,
    array        = 4,
    map          = 5,
    tag          = 6,
    simple       = 7,
};

/** CBOR data item type, directly mapped from QCBOR's QCBOR_TYPE_* constants. */
enum class cbor_type : uint8_t {
    /** Type unknown, unset, or invalid. */
    none          = 0,
    /** Wildcard; matches any type in lookup functions. */
    any           = 1,
    /** Signed integer fitting in int64 range (CBOR major type 0/1). */
    int64         = 2,
    /** Unsigned integer exceeding int64 range (CBOR major type 0). */
    uint64        = 3,
    /** CBOR array (major type 4). */
    array         = 4,
    /** CBOR map (major type 5). */
    map           = 5,
    /** Raw byte string (major type 2). */
    byte_string   = 6,
    /** UTF-8 text string (major type 3). */
    text_string   = 7,
    /** Positive bignum (tag 2). @c val.bignum is a pointer+length. */
    pos_bignum    = 9,
    /** Negative bignum (tag 3). @c val.bignum is a pointer+length. */
    neg_bignum    = 10,
    /** RFC 3339 date string (tag 0). */
    date_string   = 11,
    /** Integer seconds since 1970-01-01 (tag 1). */
    date_epoch    = 12,
    /** Unrecognized simple value (0–19 or 32–255). */
    unknown_simple = 13,
    /** Decimal fraction: integer mantissa + base-10 exponent (tag 4). */
    decimal_fraction             = 14,
    /** Decimal fraction with positive bignum mantissa. */
    decimal_fraction_pos_bignum  = 15,
    /** Decimal fraction with negative bignum mantissa. */
    decimal_fraction_neg_bignum  = 16,
    /** Bigfloat: integer mantissa + base-2 exponent (tag 5). */
    bigfloat                     = 17,
    /** Bigfloat with positive bignum mantissa. */
    bigfloat_pos_bignum          = 18,
    /** Bigfloat with negative bignum mantissa. */
    bigfloat_neg_bignum          = 19,
    /** CBOR false literal (simple value 20). */
    false_v      = 20,
    /** CBOR true literal (simple value 21). */
    true_v       = 21,
    /** CBOR null literal (simple value 22). */
    null_v       = 22,
    /** CBOR undefined literal (simple value 23). */
    undef_v       = 23,
    /** IEEE 754 single-precision float (major type 7, 32-bit). */
    float_v       = 26,
    /** IEEE 754 double-precision float (major type 7, 64-bit). */
    double_v      = 27,
    /** Map decoded as array (decode_mode::map_as_array). Internal use. */
    map_as_array  = 32,
    /** Wrapped CBOR item (tag 24). @c val.bytes is the encoded content. */
    wrapped_cbor  = 36,
    /** URI string (tag 32). */
    uri           = 44,
    /** Base64url-encoded string (tag 33). */
    base64url     = 45,
    /** Base64-encoded string (tag 34). */
    base64        = 46,
    /** Regular expression pattern (tag 35). */
    regex         = 47,
    /** Text MIME message (tag 36). */
    mime          = 48,
    /** Binary UUID (tag 37). Always 16 bytes. */
    uuid          = 49,
    /** Wrapped CBOR sequence. Internal use. */
    wrapped_cbor_sequence = 75,
    /** Binary MIME message (tag 257). */
    binary_mime   = 76,
    /** Days-count string (tag 1004). */
    days_string   = 77,
    /** Days-count epoch (tag 100). */
    days_epoch    = 78,
};

/** Tag requirement for tagged decode functions.
 *
 * These correspond to QCBOR's tag requirement constants. @ref allow_additional
 * can be OR'd with other values to permit additional (unexpected) tags on the
 * item.
 */
enum class tag_requirement : uint8_t {
    must_be_tag      = 0,          ///< Item must carry the expected tag.
    must_not_be_tag  = 1,          ///< Item must NOT carry the expected tag.
    optional_tag     = 2,          ///< Tag is optional.
    allow_additional = 0x80,       ///< OR with above: permit additional tags.
};

/** Epoch date structure: integer seconds + fractional component. */
struct epoch_date {
    int64_t seconds;               ///< Whole seconds since epoch.
    double  fraction;              ///< Fractional seconds (0.0–1.0).
};

/** Exponent and mantissa for decimal fractions and bigfloats.
 *
 * The mantissa is stored as either a plain int64_t integer or a bignum
 * byte span (big-endian unsigned magnitude).  Call is_bignum() to
 * determine which is active, then use as_integer() or as_big_num().
 *
 * This type is trivial and can reside inside a C union (e.g.
 * decoded_item::value).
 */
struct exp_and_mantissa {
    int64_t exponent;               ///< Base-10 (decimal) or base-2 (bigfloat) exponent.

    /// True when mantissa is a bignum (as_big_num() valid).
    bool is_bignum() const noexcept { return mantissa_is_bignum_; }

    /// Safe accessor. Returns the integer mantissa, or nullopt if bignum.
    std::optional<int64_t> as_integer() const noexcept {
        if (mantissa_is_bignum_) return std::nullopt;
        return integer_;
    }
    /// Safe accessor. Returns the bignum mantissa, or nullopt if integer.
    std::optional<const_byte_span> as_big_num() const noexcept {
        if (!mantissa_is_bignum_) return std::nullopt;
        return big_num_;
    }

private:
    union {
        int64_t         integer_;
        const_byte_span big_num_;
    };
    bool mantissa_is_bignum_ = false;
};

/** Decoded CBOR item returned by low-level GetNext / get_items_in_map. */
struct decoded_item {
    /** CBOR data type of this item. */
    cbor_type type      = cbor_type::none;
    /** CBOR type of the map label (if this item was map-accessed). */
    cbor_type label_type = cbor_type::none;

    /** Value union — access the member corresponding to type. */
    union {
        int64_t     int64_val;
        uint64_t    uint64_val;
        double      double_val;
        float       float_val;
        bool        bool_val;
        const_byte_span bytes;
        std::string_view text;
        exp_and_mantissa exp_mantissa;
        epoch_date  epoch_date_val;
        int64_t     epoch_days;
        uint8_t     simple_val;
    } value{};

    /** Label union — access depending on label_type. */
    union {
        std::string_view text_label;
        int64_t         int64_label;
        uint64_t        uint64_label;
    } label{};

    /** Nesting depth when this item was decoded. */
    uint8_t nesting_level      = 0;
    /** Nesting depth of the next item. */
    uint8_t next_nesting_level = 0;
    /** True if value.string points to allocated memory. */
    bool    data_allocated     = false;
    /** True if label.* points to allocated memory. */
    bool    label_allocated    = false;
};

} // namespace qcborpp

#endif // QCBORPP_TYPES_HPP
