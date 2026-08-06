/*
 * qcborpp/error.hpp -- Error types and error_code integration
 *
 * Copyright (c) 2024
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef QCBORPP_ERROR_HPP
#define QCBORPP_ERROR_HPP

#include <cstdint>
#include <system_error>
#include <stdexcept>
#include <string>

namespace qcborpp {

/**
 * Error codes mapped from QCBOR's QCBORError enum.
 *
 * Values are assigned to match QCBOR's error constants exactly,
 * so static_cast from QCBORError is safe.
 */
enum class errc : uint8_t {
    success                     = 0,   ///< No error.
    buffer_too_small            = 1,   ///< Output buffer too small for encoded CBOR.
    encode_unsupported          = 2,   ///< The requested encoding is not supported.
    buffer_too_large            = 3,   ///< Output buffer size exceeds implementation limit.
    array_nesting_too_deep      = 4,   ///< Encoder array/map nesting exceeded.
    close_mismatch              = 5,   ///< Close did not match the currently open item.
    array_too_long              = 6,   ///< Encoder array length exceeded.
    too_many_closes             = 7,   ///< More close calls than open calls.
    array_or_map_still_open     = 8,   ///< Unclosed map/array at finish.
    open_byte_string            = 9,   ///< Byte string left open (indefinite length).
    cannot_cancel               = 10,  ///< Cannot cancel an indefinite-length item.
    bad_type_7                  = 20,  ///< Invalid simple/float encoding in major type 7.
    extra_bytes                 = 21,  ///< Unconsumed bytes after the root CBOR item.
    unsupported                 = 22,  ///< Feature not supported by this QCBOR build.
    array_or_map_unconsumed     = 23,  ///< Partially consumed map/array.
    bad_int                     = 24,  ///< Invalid integer encoding.
    indefinite_string_chunk     = 30,  ///< Bad chunk in an indefinite-length string.
    hit_end                     = 31,  ///< Reached end of input unexpectedly.
    bad_break                   = 32,  ///< Invalid break byte in indefinite-length item.
    input_too_large             = 40,  ///< Input exceeds implementation limits.
    array_decode_nesting_too_deep = 41, ///< Decoded array/map nesting too deep.
    array_decode_too_long       = 42,  ///< Decoded array too long.
    string_too_long             = 43,  ///< Decoded string exceeds limit.
    bad_exp_and_mantissa        = 44,  ///< Invalid decimal fraction / bigfloat encoding.
    no_string_allocator         = 45,  ///< No string allocator configured for indefinite strings.
    string_allocate             = 46,  ///< String allocation failed.
    map_label_type              = 47,  ///< Unexpected map label type.
    unrecoverable_tag_content   = 48,  ///< Tag content is invalid and cannot be recovered.
    indef_len_strings_disabled  = 49,  ///< Indefinite-length strings are disabled.
    indef_len_arrays_disabled   = 50,  ///< Indefinite-length arrays are disabled.
    tags_disabled               = 51,  ///< Tags are disabled.
    too_many_tags               = 60,  ///< Too many tags on one item.
    unexpected_type             = 61,  ///< Item type does not match expected type.
    duplicate_label             = 62,  ///< Duplicate map label found.
    mem_pool_size               = 63,  ///< Memory pool is too small.
    int_overflow                = 64,  ///< Integer value overflow during conversion.
    date_overflow               = 65,  ///< Date value overflow during conversion.
    exit_mismatch               = 66,  ///< ExitMap/ExitArray called on wrong nesting level.
    no_more_items               = 67,  ///< No more items in the current container.
    label_not_found             = 68,  ///< Requested label not found in map.
    number_sign_conversion      = 69,  ///< Sign mismatch during numeric conversion.
    conversion_under_over_flow  = 70,  ///< Conversion underflow or overflow.
    map_not_entered             = 71,  ///< Map access attempted without entering.
    callback_fail               = 72,  ///< User callback returned failure.
    float_date_disabled         = 73,  ///< Floating-point date decoding disabled.
    half_precision_disabled     = 74,  ///< Half-precision float decoding disabled.
    hw_float_disabled           = 75,  ///< Hardware float support disabled.
    float_exception             = 76,  ///< Floating-point exception occurred.
    all_float_disabled          = 77,  ///< All floating-point support disabled.
    recoverable_bad_tag_content = 78,  ///< Bad tag content; data may still be recoverable.
};

/** Error category for qcborpp errors. */
const std::error_category& qcborpp_category() noexcept;

/** Category implementation -- defined inline (header-only library). */
class qcborpp_error_category final : public std::error_category {
public:
    const char* name() const noexcept override { return "qcborpp"; }
    std::string message(int ev) const override {
        switch (static_cast<errc>(ev)) {
        case errc::success:                    return "success";
        case errc::buffer_too_small:           return "buffer too small";
        case errc::encode_unsupported:         return "encode unsupported";
        case errc::buffer_too_large:           return "buffer too large";
        case errc::array_nesting_too_deep:     return "array nesting too deep";
        case errc::close_mismatch:             return "close mismatch";
        case errc::array_too_long:             return "array too long";
        case errc::too_many_closes:            return "too many closes";
        case errc::array_or_map_still_open:    return "array or map still open";
        case errc::open_byte_string:           return "open byte string";
        case errc::cannot_cancel:              return "cannot cancel";
        case errc::bad_type_7:                 return "bad type 7";
        case errc::extra_bytes:                return "extra bytes";
        case errc::unsupported:                return "unsupported";
        case errc::array_or_map_unconsumed:    return "array or map unconsumed";
        case errc::bad_int:                    return "bad int";
        case errc::indefinite_string_chunk:    return "indefinite string chunk";
        case errc::hit_end:                    return "hit end";
        case errc::bad_break:                  return "bad break";
        case errc::input_too_large:            return "input too large";
        case errc::array_decode_nesting_too_deep: return "array decode nesting too deep";
        case errc::array_decode_too_long:      return "array decode too long";
        case errc::string_too_long:            return "string too long";
        case errc::bad_exp_and_mantissa:       return "bad exp and mantissa";
        case errc::no_string_allocator:        return "no string allocator";
        case errc::string_allocate:            return "string allocate";
        case errc::map_label_type:             return "map label type";
        case errc::unrecoverable_tag_content:  return "unrecoverable tag content";
        case errc::indef_len_strings_disabled: return "indef len strings disabled";
        case errc::indef_len_arrays_disabled:  return "indef len arrays disabled";
        case errc::tags_disabled:              return "tags disabled";
        case errc::too_many_tags:              return "too many tags";
        case errc::unexpected_type:            return "unexpected type";
        case errc::duplicate_label:            return "duplicate label";
        case errc::mem_pool_size:              return "mem pool size";
        case errc::int_overflow:               return "int overflow";
        case errc::date_overflow:              return "date overflow";
        case errc::exit_mismatch:              return "exit mismatch";
        case errc::no_more_items:              return "no more items";
        case errc::label_not_found:            return "label not found";
        case errc::number_sign_conversion:     return "number sign conversion";
        case errc::conversion_under_over_flow: return "conversion under/over flow";
        case errc::map_not_entered:            return "map not entered";
        case errc::callback_fail:              return "callback fail";
        case errc::float_date_disabled:        return "float date disabled";
        case errc::half_precision_disabled:    return "half precision disabled";
        case errc::hw_float_disabled:          return "hw float disabled";
        case errc::float_exception:            return "float exception";
        case errc::all_float_disabled:         return "all float disabled";
        case errc::recoverable_bad_tag_content: return "recoverable bad tag content";
        default: return "unknown error";
        }
    }
};

inline const std::error_category& qcborpp_category() noexcept {
    static const qcborpp_error_category instance;
    return instance;
}

/** Construct std::error_code from qcborpp::errc. */
inline std::error_code make_error_code(errc e) noexcept {
    return {static_cast<int>(e), qcborpp_category()};
}

/**
 * Exception type thrown by all qcborpp operations on error.
 *
 * All encoder and decoder methods throw this when an error occurs.
 * The error code can be accessed via code().
 */
class error : public std::runtime_error {
    errc c_;
public:
    explicit error(errc c)
        : std::runtime_error(std::string("qcborpp error: ")
                             + qcborpp_category().message(static_cast<int>(c)))
        , c_(c) {}

    error(errc c, const char* msg)
        : std::runtime_error(msg)
        , c_(c) {}

    errc code() const noexcept { return c_; }
};

/**
 * Convert a QCBOR error integer to qcborpp::errc and throw if non-zero.
 *
 * @param qcbor_err  The error code from a QCBOR API call.
 */
inline void check_qcbor_err(int qcbor_err) {
    if (qcbor_err != 0)
        throw error(static_cast<errc>(qcbor_err));
}

} // namespace qcborpp

namespace std {
template<> struct is_error_code_enum<qcborpp::errc> : true_type {};
} // namespace std

#endif // QCBORPP_ERROR_HPP
