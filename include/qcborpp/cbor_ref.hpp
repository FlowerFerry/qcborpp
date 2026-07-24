/*
 * qcborpp/cbor_ref.hpp -- Lightweight value wrapper for initializer-list encoding
 *
 * cbor_ref is a tagged-union type that represents a single CBOR value
 * (scalar, array, map) as a compile-time initializer-list element.
 * At runtime, it is consumed by encoder constructors to directly write
 * CBOR data -- no intermediate json tree is built.
 *
 * Ownership model for nested lists:
 *   A cbor_ref constructed from std::initializer_list deep-copies the
 *   elements into a std::shared_ptr<const std::vector<cbor_ref>>.
 *   This ensures the data outlives the initializer_list's temporary
 *   backing array. The shared_ptr is shared among copies/moves of the
 *   cbor_ref, making factory returns (arr/map/imap) safe.
 *
 * Copyright (c) 2024
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef QCBORPP_CBOR_REF_HPP
#define QCBORPP_CBOR_REF_HPP

#include <chrono>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <memory>
#include <string_view>
#include <vector>

#include "qcborpp/types.hpp"

namespace qcborpp {

// ............................................................................
// cbor_ref -- lightweight value wrapper for initializer-list encoding
// ............................................................................

class cbor_ref {
    // encoder constructors need to inspect kind_ and list internals
    friend class encoder;
    friend class dynamic_encoder;

    // factory functions need to write kind_
    friend cbor_ref arr(std::initializer_list<cbor_ref>);
    friend cbor_ref map(std::initializer_list<cbor_ref>);
    friend cbor_ref imap(std::initializer_list<cbor_ref>);

public:
    // ── kind enum ──────────────────────────────────────────────────────

    enum class kind : uint8_t {
        null_v,         // CBOR null
        bool_v,         // boolean
        int64_v,        // signed integer
        uint64_v,       // unsigned integer
        double_v,       // floating-point
        string_v,       // UTF-8 text (borrows data, not owning)
        bytes_v,        // byte string (borrows data, not owning)
        time_point_v,   // system_clock::time_point (epoch seconds)
        days_v,         // duration counted in days (epoch days)

        list_v,         // initializer_list -- inference at write time
        array_v,        // forced array (arr() factory)
        map_v,          // forced map with string keys (map() factory)
        imap_v,         // forced map with int keys   (imap() factory)
    };

    // ── constructors: scalars ──────────────────────────────────────────

    cbor_ref() noexcept                       : kind_(kind::null_v) {}
    cbor_ref(std::nullptr_t) noexcept         : kind_(kind::null_v) {}
    cbor_ref(bool v) noexcept                 : kind_(kind::bool_v),    b_(v) {}
    cbor_ref(int v) noexcept                  : kind_(kind::int64_v),   i64_(v) {}
    cbor_ref(unsigned v) noexcept             : kind_(kind::uint64_v),  u64_(v) {}
    cbor_ref(int64_t v) noexcept              : kind_(kind::int64_v),   i64_(v) {}
    cbor_ref(uint64_t v) noexcept             : kind_(kind::uint64_v),  u64_(v) {}
    cbor_ref(double v) noexcept               : kind_(kind::double_v),  d_(v) {}
    cbor_ref(float v) noexcept                : kind_(kind::double_v),  d_(v) {}
    cbor_ref(const char* s) noexcept          : kind_(kind::string_v)
        { str_ptr_ = s; str_len_ = std::strlen(s); }
    cbor_ref(std::string_view s) noexcept     : kind_(kind::string_v)
        { str_ptr_ = s.data(); str_len_ = s.size(); }
    cbor_ref(const_byte_span b) noexcept      : kind_(kind::bytes_v)
        { bytes_ptr_ = b.data(); bytes_len_ = b.size(); }

    cbor_ref(std::chrono::system_clock::time_point tp) noexcept
        : kind_(kind::time_point_v)
    {
        i64_ = std::chrono::duration_cast<std::chrono::seconds>(
            tp.time_since_epoch()).count();
    }

    template<typename Rep, typename Period>
    cbor_ref(std::chrono::duration<Rep, Period> dur) noexcept
        : kind_(kind::days_v)
    {
        i64_ = static_cast<int64_t>(
            std::chrono::duration_cast<std::chrono::duration<int64_t, std::ratio<86400>>>(dur).count());
    }

    // ── constructor: deferred inference (deep-copies the list) ──────────

    cbor_ref(std::initializer_list<cbor_ref> il)
        : kind_(kind::list_v)
        , list_elements_(std::make_shared<const std::vector<cbor_ref>>(il.begin(), il.end()))
    {}

    // ── accessors ─────────────────────────────────────────────────────

    kind get_kind() const noexcept { return kind_; }

    // ── list helpers (for encoder friend) ──────────────────────────────

    /** Number of child elements (valid for list/array/map/imap kinds). */
    size_t list_count() const noexcept { return list_elements_ ? list_elements_->size() : 0; }

    /** Reference to the child vector (valid for list/array/map/imap kinds). */
    const std::vector<cbor_ref>& list_items() const noexcept {
        static const std::vector<cbor_ref> empty;
        return list_elements_ ? *list_elements_ : empty;
    }

    /** Const pointer to children, for iterator-style access. */
    const cbor_ref* list_data() const noexcept {
        return list_elements_ ? list_elements_->data() : nullptr;
    }

private:
    // ── scalar storage (flat struct; union avoided for shared_ptr dtor) ──

    kind        kind_ = kind::null_v;

    // Scalars
    bool        b_      = false;
    int64_t     i64_    = 0;
    uint64_t    u64_    = 0;
    double      d_      = 0.0;

    // Non-owning string / bytes references
    const char*     str_ptr_   = nullptr;
    size_t          str_len_   = 0;
    const uint8_t*  bytes_ptr_ = nullptr;
    size_t          bytes_len_ = 0;

    // Owning nested-list storage (shared across copies)
    std::shared_ptr<const std::vector<cbor_ref>> list_elements_;
};

// ............................................................................
// Factory functions -- override inference
// ............................................................................

/** Force an initializer_list to encode as a CBOR array. */
inline cbor_ref arr(std::initializer_list<cbor_ref> il) {
    cbor_ref r(il);
    r.kind_ = cbor_ref::kind::array_v;
    return r;
}

/** Force an initializer_list to encode as a CBOR map (string keys). */
inline cbor_ref map(std::initializer_list<cbor_ref> il) {
    cbor_ref r(il);
    r.kind_ = cbor_ref::kind::map_v;
    return r;
}

/** Force an initializer_list to encode as a CBOR map (integer keys). */
inline cbor_ref imap(std::initializer_list<cbor_ref> il) {
    cbor_ref r(il);
    r.kind_ = cbor_ref::kind::imap_v;
    return r;
}

} // namespace qcborpp

#endif // QCBORPP_CBOR_REF_HPP
