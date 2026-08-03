/*
 * qcborpp/decoder.hpp -- C++ decoder wrapping QCBOR C decode API
 *
 * Copyright (c) 2024
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef QCBORPP_DECODER_HPP
#define QCBORPP_DECODER_HPP

#include "qcborpp/error.hpp"
#include "qcborpp/types.hpp"
#include "qcbor/qcbor_decode.h"
#include "qcbor/qcbor_spiffy_decode.h"
#include <cstring>
#include <functional>
#include <vector>
#include <string>
#include <unordered_map>
#include <chrono>
#include <optional>
#include <cmath>

namespace qcborpp {

// Forward declarations
class decoder;
class map_scope;
class array_scope;
class item_proxy;

// ============================================================================
// decoder
// ============================================================================

class decoder {
    friend class map_scope;
    friend class array_scope;
    friend class item_proxy;

    QCBORDecodeContext ctx_{};
    bool               top_set_   = false;
    bool               top_map_   = false;
    int                map_depth_  = 0;
    int                array_depth_ = 0;
    int64_t            array_count_ = 0;
    bool               finished_  = false;
    bool               force_prefetch_ = true;
    bool               map_auto_rewind_ = true;
    uint8_t            first_byte_ = 0;

    void check_err() {
        auto e = QCBORDecode_GetError(&ctx_);
        if (e != QCBOR_SUCCESS)
            throw error(static_cast<errc>(e));
    }

    // Rewind the bounded map cursor after a Spiffy lookup, then check errors.
    // On force_prefetch(false), Spiffy InMapSZ/InMapN calls consume the bounded
    // map cursor; without a rewind the next lookup may fail even for valid keys.
    // Exceptions are still thrown on error — the rewind happens regardless
    // so that get_or/try_get catch blocks inherit a clean decoder state.
    void auto_rewind_check() {
        uint8_t err = ctx_.uLastError;
        if (map_auto_rewind_ && map_depth_ > 0)
            QCBORDecode_Rewind(&ctx_);
        if (err != QCBOR_SUCCESS)
            throw error(static_cast<errc>(err));
    }

    void enter_map() {
        QCBORDecode_EnterMap(&ctx_, nullptr);
        check_err();
        ++map_depth_;
    }
    void exit_map() {
        // VGetNext may have set uLastError to NO_MORE_ITEMS.
        // Clear it so ExitMap (which checks uLastError first) can proceed.
        if (ctx_.uLastError == QCBOR_ERR_NO_MORE_ITEMS || ctx_.uLastError == QCBOR_ERR_HIT_END)
            ctx_.uLastError = QCBOR_SUCCESS;
        QCBORDecode_ExitMap(&ctx_);
        check_err();
        --map_depth_;
    }
    void enter_array() {
        QCBORItem item;
        QCBORDecode_EnterArray(&ctx_, &item);
        check_err();
        ++array_depth_;
        array_count_ = item.val.uCount;
    }
    void exit_array() {
        if (ctx_.uLastError == QCBOR_ERR_NO_MORE_ITEMS || ctx_.uLastError == QCBOR_ERR_HIT_END)
            ctx_.uLastError = QCBOR_SUCCESS;
        QCBORDecode_ExitArray(&ctx_);
        check_err();
        --array_depth_;
    }

public:
    /**
     * @brief  Construct a decoder from CBOR bytes.
     *
     * @param data  The encoded CBOR byte span.
     * @param mode  Decoding mode (normal, map_strings_only, or map_as_array).
     */
    explicit decoder(const_byte_span data, decode_mode mode = decode_mode::normal) {
        QCBORDecode_Init(&ctx_,
                         UsefulBufC{const_cast<uint8_t*>(data.data()), data.size()},
                         static_cast<QCBORDecodeMode>(mode));
        if (!data.empty()) first_byte_ = data[0];
    }

    decoder(const decoder&) = delete;
    decoder& operator=(const decoder&) = delete;

    ~decoder() {
        if (!finished_)
            finish();
    }

    /**
     * @brief  Enter the top-level CBOR map for label-based access.
     * @return A map_scope for keyed lookup.
     * @throws error if the top-level structure is not a map.
     */
    map_scope map();

    /**
     * @brief  Enter the top-level CBOR array for sequential access.
     * @return An array_scope for item iteration.
     * @throws error if the top-level structure is not an array.
     */
    array_scope array();

    /**
     * @brief  Check whether the top-level item is a map.
     *
     * Inspects the CBOR initial byte (without consuming it) when not yet
     * entered; after map() or array() is called, returns the actual type.
     *
     * @return true if the top-level structure is a CBOR map.
     */
    bool is_map()   const noexcept {
        if (top_set_) return top_map_;
        return first_byte_ != 0 && ((first_byte_ >> 5) & 0x07) == 5;
    }

    /**
     * @brief  Check whether the top-level item is an array.
     * @return true if the top-level structure is a CBOR array.
     */
    bool is_array() const noexcept {
        if (top_set_) return !top_map_;
        return first_byte_ != 0 && ((first_byte_ >> 5) & 0x07) == 4;
    }

    /**
     * @brief  Set whether map() and as_map() should force a full prefetch.
     *
     * When true (default), every map scope automatically prefetches all
     * entries — optimal for multi-key lookups. When false, prefetch is
     * on-demand: operator[] uses lazy single-key resolution, and only
     * contains/size/for_each trigger prefetch.
     *
     * @return *this for chaining.
     */
    decoder& set_force_prefetch(bool enable) noexcept {
        force_prefetch_ = enable;
        return *this;
    }

    /** @brief Returns current force_prefetch setting. */
    bool force_prefetch() const noexcept { return force_prefetch_; }

    /**
     * @brief  Enable or disable automatic map rewind after each label lookup.
     *
     * When true (default), every m["key"] or m[42] access automatically
     * rewinds the bounded-map cursor so subsequent lookups start from the
     * beginning — safe "random access" semantics even without prefetch.
     * When false, lookups consume the map cursor sequentially, which is
     * faster for known-ordered access patterns but fragile for random access.
     *
     * Has no effect when force_prefetch is true (cached lookups never touch
     * the Spiffy cursor).
     *
     * @return *this for chaining.
     */
    decoder& set_map_auto_rewind(bool enable) noexcept {
        map_auto_rewind_ = enable;
        return *this;
    }

    /** @brief Returns current map_auto_rewind setting. */
    bool map_auto_rewind() const noexcept { return map_auto_rewind_; }

    /**
     * @brief  Complete decoding and return the final status.
     *
     * After this call, the decoder is marked finished and cannot be used
     * further.
     *
     * @return A zero error_code on success, or a decode error.
     */
    std::error_code finish() {
        finished_ = true;
        QCBORError e = QCBORDecode_Finish(&ctx_);
        return e == QCBOR_SUCCESS ? std::error_code{}
                                  : make_error_code(static_cast<errc>(e));
    }

    /**
     * @brief  Return the current decoder error state without consuming data.
     * @return A zero error_code if no error has occurred.
     */
    std::error_code error_state() const noexcept {
        QCBORError e = QCBORDecode_GetError(
            const_cast<QCBORDecodeContext*>(&ctx_));
        return e == QCBOR_SUCCESS ? std::error_code{}
                                  : make_error_code(static_cast<errc>(e));
    }

    /**
     * @brief  Rewind the decode cursor to the beginning.
     *
     * Allows re-decoding the same CBOR data without constructing a new
     * decoder.
     */
    void rewind() { QCBORDecode_Rewind(&ctx_); check_err(); }

    /**
     * @brief  Configure a memory pool for indefinite-length string allocation.
     *
     * Required when decoding indefinite-length text or byte strings.
     *
     * @param pool         Pre-allocated memory pool.
     * @param all_strings  If true, use pool for all strings (not just
     *                     indefinite-length ones).
     * @return A zero error_code on success.
     */
    std::error_code set_mem_pool(byte_span pool, bool all_strings = false) {
        QCBORError e = QCBORDecode_SetMemPool(
            &ctx_, UsefulBuf{pool.data(), pool.size()}, all_strings);
        return e == QCBOR_SUCCESS ? std::error_code{}
                                  : make_error_code(static_cast<errc>(e));
    }

    /**
     * @brief  Low-level: decode the next item (V version, error-tracked).
     *
     * The V variant uses QCBOR's internal error tracking (uLastError).
     * Use with check_err().
     *
     * @return The decoded item.
     */
    decoded_item v_get_next();

    /**
     * @brief  Low-level: peek at the next item without consuming it (V version).
     * @return The next item, without advancing the cursor.
     */
    decoded_item v_peek_next();

    /**
     * @brief  Low-level: decode the next item (raw, returns error explicitly).
     *
     * Unlike the V variant, returns the error via the QCBOR return value.
     * Throws on failure.
     *
     * @return The decoded item.
     * @throws error if decoding fails.
     */
    decoded_item get_next();

    /**
     * @brief  Specification for a single item to extract from a map.
     *
     * Used with get_items_in_map() to batch-decode multiple labeled items
     * in a single map traversal.
     */
    struct item_spec {
        int64_t     label_int    = 0;
        const char* label_str    = nullptr;
        cbor_type   type         = cbor_type::any;
        bool        is_int_label = false;
    };

    /**
     * @brief  Decode multiple labeled items from the current map in one pass.
     *
     * More efficient than individual operator[] lookups when many items are
     * needed from the same map.
     *
     * @param specs  List of items to look up (label + expected type).
     * @param out    Output vector populated in the same order as specs.
     * @return A zero error_code on success.
     */
    std::error_code get_items_in_map(const std::vector<item_spec>& specs,
                                      std::vector<decoded_item>& out);

    /**
     * @brief  Access the underlying QCBOR decode context (mutable).
     * @return Pointer to the raw QCBORDecodeContext.
     */
    QCBORDecodeContext*       raw_ctx()       noexcept { return &ctx_; }

    /**
     * @brief  Access the underlying QCBOR decode context (const).
     * @return Const pointer to the raw QCBORDecodeContext.
     */
    const QCBORDecodeContext* raw_ctx() const noexcept { return &ctx_; }

    /**
     * @brief  Check if the decoder is currently inside a map.
     * @return true if map_depth_ > 0.
     */
    bool in_map_state()   const noexcept { return map_depth_ > 0; }

    /**
     * @brief  Check if the decoder is currently inside an array.
     * @return true if array_depth_ > 0.
     */
    bool in_array_state() const noexcept { return array_depth_ > 0; }

    /**
     * @brief  Check if finish() has been called.
     * @return true once decoding is complete.
     */
    bool is_finished()    const noexcept { return finished_; }

private:
    static decoded_item convert_item(const QCBORItem& item);

    /** Implementation for get_items_in_map using native QCBORDecode_GetItemsInMap. */
    std::error_code impl_get_items_in_map(const std::vector<item_spec>& specs,
                                           std::vector<decoded_item>& out);

    /** Private helper: convert item_spec vector to QCBORItem array (on stack for perf). */
};

// ============================================================================
// map_scope -- returned by dec.map() and item_proxy::as_map()
// ============================================================================
//
// Auto-closing scope for label-based map access. Closes the map in the
// decoder on destruction unless moved from or manually exited.

class map_scope {
    friend class decoder;
    friend class item_proxy;
    decoder* dec_;
    bool     exited_ = false;
    /** The last label set for map lookups. */
    std::string last_label_;
    /** Prefetch cache: label -> decoded_item, populated by prefetch(). */
    std::unordered_map<std::string, decoded_item> cache_;
    /** Prefetch cache: integer label -> decoded_item, populated by prefetch(). */
    std::vector<std::pair<int64_t, decoded_item>> int_cache_;
    bool prefetched_ = false;

    explicit map_scope(decoder& d) noexcept : dec_(&d) {}

public:
    ~map_scope();
    map_scope(map_scope&& o) noexcept;
    map_scope& operator=(map_scope&& o) noexcept;

    map_scope(const map_scope&) = delete;
    map_scope& operator=(const map_scope&) = delete;

    /**
     * @brief  One-pass prefetch: decode every item in this map into a cache.
     *
     * After this call, operator[] hits are O(1) from cache.
     * Best for maps with many lookups (50+ items). For single-item lookups,
     * skip this and use operator[] directly.
     */
    void prefetch();

    /** Static callback for QCBORDecode_GetItemsInMapWithCallback. */
    static QCBORError prefetch_cb(void* ctx, const QCBORItem* item);

    /**
     * @brief  Look up an item by text label.
     *
     * If prefetch() was called, hits the O(1) cache. Otherwise, creates a
     * lazy item_proxy that resolves on first access.
     *
     * @param key  The text label (string_view).
     * @return An item_proxy bound to this label.
     */
    item_proxy operator[](std::string_view key);

    /**
     * @brief  Look up an item by integer label.
     * @param key  The integer label.
     * @return An item_proxy bound to this label.
     */
    item_proxy operator[](int64_t key);

    /**
     * @brief  Look up an item by int label.
     *
     * Delegates to the int64_t overload.
     *
     * @param key  Integer label (promoted to int64_t).
     * @return An item_proxy bound to this label.
     */
    item_proxy operator[](int key);

    /**
     * @brief  Look up key and return its value, or a default if not found.
     *
     * Syntax sugar: m.get_or("port", 8080) instead of m["port"].get_or(8080).
     * For string keys, auto-prefetches if needed, then returns the cached
     * value if the key exists, otherwise returns the provided default.
     *
     * @tparam T  Default value type (must match one of the get_or overloads).
     * @param  key    The string key to look up.
     * @param  def    Default value returned when key is absent.
     * @return The value associated with key, or def.
     */
    /** @copydoc get_or(std::string_view,T) const */
    template<typename T>
    T get_or(std::string_view key, T def) const;

    /** @copydoc get_or(std::string_view,T) const */
    template<typename T>
    T get_or(int64_t key, T def) const;

    /** @copydoc try_get(std::string_view) const noexcept */
    template<typename T>
    std::optional<T> try_get(std::string_view key) const noexcept;

    /** @copydoc try_get(std::string_view) const noexcept */
    template<typename T>
    std::optional<T> try_get(int64_t key) const noexcept;

    /**
     * @brief  Check whether a key exists in this map.
     *
     * Auto-prefetches if not already prefetched, then checks the cache.
     * Does not advance the decoder cursor — safe to call between other
     * map operations.
     *
     * @param key  The text label to look up.
     * @return true if the key exists.
     */
    bool contains(std::string_view key);

    /**
     * @brief  Check whether an integer key exists in this map.
     *
     * Auto-prefetches if not already prefetched.
     *
     * @param key  The integer label to look up.
     * @return true if the key exists.
     */
    bool contains(int64_t key);

    /**
     * @brief  Return the number of key-value pairs in this map.
     *
     * Auto-prefetches if not already prefetched, then returns cache size.
     * This is the count from the CBOR map header.
     *
     * @return The number of entries.
     */
    size_t size();

    /** @brief Returns true if the map has no entries. */
    bool empty() { return size() == 0; }

    /**
     * @brief  Iterate all key-value pairs in this map via a callback.
     *
     * Auto-prefetches if not already prefetched. The visitor receives
     * a std::string_view key and a decoded_item value for every entry.
     * Integer-keyed entries are skipped (use for_each_int for those).
     *
     * @param visitor  Callable invoked for each (key, value) pair.
     * @return *this for chaining.
     */
    template<typename F>
    map_scope& for_each(F&& visitor);

    /**
     * @brief  Iterate all integer-keyed entries in this map via a callback.
     *
     * Auto-prefetches if not already prefetched. The visitor receives
     * an int64_t key and a decoded_item value. String-keyed entries are
     * skipped.
     *
     * @param visitor  Callable invoked for each (key, value) pair.
     * @return *this for chaining.
     */
    template<typename F>
    map_scope& for_each_int(F&& visitor);

    /**
     * @brief  Look up an item by null-terminated string label.
     * @param key  Pointer to a null-terminated label string.
     * @return An item_proxy bound to this label.
     */
    item_proxy operator[](const char* key);

    /**
     * @brief  Batch-decode items from this map in one pass.
     *
     * Uses QCBORDecode_GetItemsInMap - single traversal, O(n).
     * Much faster than individual operator[] calls for many keys.
     *
     * @param specs  List of (label, type) items to look up.
     * @param out    Populated in the same order as specs.
     * @return Zero error_code on success.
     */
    std::error_code get_items(
            const std::vector<decoder::item_spec>& specs,
            std::vector<decoded_item>& out);

};

// ============================================================================
// item_proxy -- returned by scope[key]
// ============================================================================

class item_proxy {
    friend class map_scope;
    friend class array_scope;
    decoder*     dec_;
    bool         is_int_label_ = false;
    int64_t      int_label_    = 0;
    std::string  str_label_;
    decoded_item cached_{};
    bool         has_cached_ = false;

    explicit item_proxy(decoder& d) noexcept : dec_(&d) {}

    item_proxy(decoder& d, const decoded_item& di) noexcept
        : dec_(&d), cached_(di), has_cached_(true) {}

    // Cache hit with a string label: preserves label for as_map()/as_array() lookups.
    item_proxy(decoder& d, const decoded_item& di, std::string str_label) noexcept
        : dec_(&d), str_label_(std::move(str_label)), cached_(di), has_cached_(true) {}

    // Cache hit with an int label.
    item_proxy(decoder& d, const decoded_item& di, int64_t int_label) noexcept
        : dec_(&d), is_int_label_(true), int_label_(int_label), cached_(di), has_cached_(true) {}

    item_proxy(decoder& d, const std::string& label, bool is_int, int64_t ilabel) noexcept
        : dec_(&d), is_int_label_(is_int), int_label_(ilabel), str_label_(label) {}

    // Throw if cached_ type does not match expected, otherwise no-op.
    // Called from as_*() cached paths so get_or/try_get catch type mismatches.
    void check_cache_type(cbor_type expected) const {
        if (!has_cached_ || cached_.type == expected) return;
        // bool special case: both true_v and false_v are valid
        if (expected == cbor_type::true_v && cached_.type == cbor_type::false_v) return;
        if (expected == cbor_type::false_v && cached_.type == cbor_type::true_v) return;
        dec_->ctx_.uLastError = QCBOR_ERR_UNEXPECTED_TYPE;
        dec_->check_err();
    }

public:
    // ── implicit conversions ──

    /**
     * @brief  Implicit conversion to signed 64-bit integer.
     *
     * Equivalent to as_int64(). Throws if the actual CBOR type cannot be
     * converted.
     */
    operator int64_t() const;

    /**
     * @brief  Implicit conversion to unsigned 64-bit integer.
     *
     * Equivalent to as_uint64(). Throws on type mismatch.
     */
    operator uint64_t() const;

    /**
     * @brief  Implicit conversion to UTF-8 string_view.
     *
     * Equivalent to as_string(). Throws on type mismatch.
     */
    operator std::string_view() const;

    /**
     * @brief  Implicit conversion to IEEE 754 double.
     *
     * Equivalent to as_double(). Throws on type mismatch.
     */
    operator double() const;

    /**
     * @brief  Implicit conversion to boolean.
     *
     * Equivalent to as_bool(). Throws on type mismatch.
     */
    operator bool() const;

    /**
     * @brief  Implicit conversion to byte span.
     *
     * Equivalent to as_bytes(). Throws on type mismatch.
     */
    operator const_byte_span() const;

    // ── safe access with default ──

    /**
     * @brief  Extract the item as int64_t, returning a default if the key
     *         is not found or the value is the wrong type.
     *
     * @param default_val  Returned when the key is absent or type mismatches.
     * @return The int64_t value or default_val.
     */
    int64_t get_or(int64_t default_val) const noexcept;

    /**
     * @brief  Extract the item as uint64_t with default fallback.
     */
    uint64_t get_or(uint64_t default_val) const noexcept;

    /**
     * @brief  Extract the item as a string_view with default fallback.
     */
    std::string_view get_or(std::string_view default_val) const noexcept;

    /**
     * @brief  Extract the item as double with default fallback.
     */
    double get_or(double default_val) const noexcept;

    /**
     * @brief  Extract the item as bool with default fallback.
     */
    bool get_or(bool default_val) const noexcept;

    /** @brief Convenience overload for int literals — forwards to int64_t. */
    int64_t get_or(int default_val) const noexcept { return get_or(static_cast<int64_t>(default_val)); }

    // ── try_get — std::optional access ──

    /**
     * @brief  Try to extract the value as T. Returns std::nullopt if the
     *         key was not found or the type does not match.
     *
     * Uses get_or semantics: catches error thrown when key is absent or
     * type is wrong. Distinct from get_or() because std::nullopt cleanly
     * separates "no value" from a legitimate default (e.g. zero).
     */
    template<typename T>
    std::optional<T> try_get() const noexcept {
        try { return std::optional<T>(static_cast<const item_proxy&>(*this)); }
        catch (...) { return std::nullopt; }
    }

    // ── explicit getters ──

    /**
     * @brief  Extract the item as a signed 64-bit integer.
     * @return The int64_t value.
     * @throws error if the item is not an int64 or cannot be converted.
     */
    int64_t          as_int64() const;

    /**
     * @brief  Extract the item as an unsigned 64-bit integer.
     * @return The uint64_t value.
     * @throws error if the item is not a uint64 or cannot be converted.
     */
    uint64_t         as_uint64() const;

    /**
     * @brief  Extract the item as a UTF-8 text string.
     * @return A string_view over the decoded text (does not own the data).
     * @throws error if the item is not a text string.
     */
    std::string_view as_string() const;

    /**
     * @brief  Extract the item as a byte string.
     * @return A byte span over the decoded bytes (does not own the data).
     * @throws error if the item is not a byte string.
     */
    const_byte_span  as_bytes() const;

    /**
     * @brief  Extract the item as an IEEE 754 double.
     *
     * Integer items are automatically converted to double when requested.
     *
     * @return The double value.
     * @throws error if the item cannot be represented as double.
     */
    double           as_double() const;

    /**
     * @brief  Extract the item as a boolean.
     * @return The bool value.
     * @throws error if the item is not a CBOR true or false.
     */
    bool             as_bool() const;

    // ── loose numeric getters (cross-type convert) ──

    int64_t  get_int64() const;
    uint64_t get_uint64() const;
    double   get_double() const;

    // ── sub-structures ──

    /**
     * @brief  Enter the item as a nested CBOR map.
     *
     * Consumes the map header. The returned map_scope provides label-based
     * access to the map entries and auto-closes on destruction.
     *
     * @return A map_scope for accessing the nested map.
     * @throws error if the item is not a map.
     */
    map_scope   as_map();

    /**
     * @brief  Enter the item as a nested CBOR array.
     *
     * Consumes the array header. The returned array_scope provides
     * sequential access and auto-closes on destruction.
     *
     * @return An array_scope for iterating the nested array.
     * @throws error if the item is not an array.
     */
    array_scope as_array();

    // ── type queries ──

    /**
     * @brief  Return the CBOR data type of the referenced item.
     *
     * This is a non-consuming query — the decode cursor is not advanced.
     * It works for items from all three internal states:
     *  - cached scalar from array_scope::next()
     *  - label-based lookup from map_scope::operator[]
     *  - bare proxy for containers (peeked via QCBORDecode_PeekNext)
     *
     * @return The matching cbor_type enumerator.
     *
     * @throws error if the underlying QCBOR peek operation fails.
     */
    cbor_type type() const;

    /**
     * @brief  Check if the item is a signed integer (CBOR major type 0/1, value fits int64).
     * @return true if type() == cbor_type::int64.
     */
    bool is_int64()  const;

    /**
     * @brief  Check if the item is an unsigned integer exceeding int64 range.
     * @return true if type() == cbor_type::uint64.
     */
    bool is_uint64() const;

    /**
     * @brief  Check if the item is an IEEE 754 double-precision float (CBOR major type 7, 64-bit).
     * @return true if type() == cbor_type::double_v.
     */
    bool is_double() const;

    /**
     * @brief  Check if the item is an IEEE 754 single-precision float (CBOR major type 7, 32-bit).
     * @return true if type() == cbor_type::float_v.
     */
    bool is_float()  const;

    /**
     * @brief  Check if the item is a UTF-8 text string (CBOR major type 3).
     * @return true if type() == cbor_type::text_string.
     */
    bool is_string() const;

    /**
     * @brief  Check if the item is a byte string (CBOR major type 2).
     * @return true if type() == cbor_type::byte_string.
     */
    bool is_bytes()  const;

    /**
     * @brief  Check if the item is a CBOR boolean value.
     * @return true if type() is cbor_type::true_v or cbor_type::false_v.
     */
    bool is_bool()   const;

    /**
     * @brief  Check if the item is the CBOR literal true.
     * @return true if type() == cbor_type::true_v.
     */
    bool is_bool_true()  const;

    /**
     * @brief  Check if the item is the CBOR literal false.
     * @return true if type() == cbor_type::false_v.
     */
    bool is_bool_false() const;

    /**
     * @brief  Check if the item is the CBOR literal null (simple value 22).
     * @return true if type() == cbor_type::null_v.
     */
    bool is_null()   const;

    /**
     * @brief  Check if the item is the CBOR literal undefined (simple value 23).
     * @return true if type() == cbor_type::undef_v.
     */
    bool is_undef()  const;

    /**
     * @brief  Check if the item is a CBOR map (major type 5).
     * @return true if type() == cbor_type::map.
     */
    bool is_map()    const;

    /**
     * @brief  Check if the item is a CBOR array (major type 4).
     * @return true if type() == cbor_type::array.
     */
    bool is_array()  const;

    /**
     * @brief  Check if the item is an unrecognized CBOR simple value (0–19).
     *
     * CBOR major type 7 encodes simple values 0–23.  Values 20–23 are
     * reserved for false / true / null / undefined; values 0–19 are
     * unassigned.  QCBOR reports those as QCBOR_TYPE_UKNOWN_SIMPLE.
     *
     * @return true if type() == cbor_type::unknown_simple.
     */
    bool is_simple() const;

    /**
     * @brief  Check if the item is a CBOR container type.
     * @return true if type() is cbor_type::map or cbor_type::array.
     */
    bool is_container() const;

    /**
     * @brief  Check if the item carries a CBOR tag (major type 6).
     *
     * Returns true when type() is one of the semantic tagged types —
     * e.g. cbor_type::uri, cbor_type::base64, cbor_type::date_epoch, etc.
     *
     * @return true if the item is tagged.
     */
    bool is_tag() const;

    // ── lifecycle observability ──

    /**
     * @brief  Check whether the item has already been decoded and cached.
     *
     * Returns true if this proxy holds a cached copy of the CBOR item
     * (via prefetch or explicit resolve()). When true, the proxy is
     * safe to use even after its parent scope has been destroyed.
     *
     * When false, the proxy has a lazy label (from operator[]) or is
     * a bare container proxy (from array_scope::next()). Access methods
     * will attempt on-demand decode, which may fail if the parent
     * decoder scope has been exited.
     */
    bool is_resolved() const noexcept;

    /**
     * @brief  Force immediate decoding of a lazy proxy.
     *
     * If is_resolved() is already true, this is a no-op.
     *
     * For lazy proxies (created with force_prefetch=false), this
     * decodes the item via the decoder and populates the cache.
     * After resolve() returns successfully, is_resolved() == true
     * and the proxy is independent of its parent scope.
     *
     * @throws error if the decoder is not in the expected map/array
     *         context or the item cannot be decoded.
     */
    void resolve();

    /**
     * @brief  Return the tag number if this item is tagged.
     *
     * Returns the outermost CBOR tag number (e.g. tag 1 for epoch dates,
     * tag 32 for URIs). For untagged items, returns std::nullopt.
     *
     * When this proxy is lazy (is_resolved() == false), resolve() is
     * called first to populate the cache. If resolution fails (e.g.
     * parent scope was already destroyed), the error propagates.
     *
     * @return The tag number, or std::nullopt if untagged.
     * @throws error if lazy resolution fails.
     */
    std::optional<uint64_t> tag_number();

    // ── tagged getters ──

    /**
     * @brief  Extract as an RFC 3339 date string (tag 0).
     *
     * @param tag_req  Tag requirement: 0 = must be tagged, 1 = must NOT be
     *                 tagged, 2 = optional.
     * @return The date string.
     * @throws error on type/tag mismatch.
     */
    std::string_view as_date_string(tag_requirement tag_req = tag_requirement::must_be_tag) const;

    /**
     * @brief  Extract as a days-count string (tag 1004).
     * @param tag_req  Tag requirement.
     * @return The days string.
     * @throws error on type/tag mismatch.
     */
    std::string_view as_days_string(tag_requirement tag_req = tag_requirement::must_be_tag) const;

    // ── epoch date getters ──

    /**
     * @brief  Extract as epoch seconds (tag 1).
     * @param tag_req  Tag requirement.
     * @return Seconds since epoch as int64_t.
     * @throws error on type/tag mismatch.
     */
    int64_t as_date_epoch(tag_requirement tag_req = tag_requirement::must_be_tag) const;

    /**
     * @brief  Extract as a std::chrono::system_clock::time_point (tag 1).
     *
     * @param tag_req  Tag requirement.
     * @return time_point constructed from epoch seconds.
     * @throws error on type/tag mismatch.
     */
    std::chrono::system_clock::time_point as_time_point(tag_requirement tag_req = tag_requirement::must_be_tag) const;

    /**
     * @brief  Extract as epoch days (tag 100).
     * @param tag_req  Tag requirement.
     * @return Days since epoch as int64_t.
     * @throws error on type/tag mismatch.
     */
    int64_t as_days_epoch(tag_requirement tag_req = tag_requirement::must_be_tag) const;

    /**
     * @brief  Extract as a std::chrono::days-equivalent duration (tag 100).
     *
     * Returns std::chrono::duration<int64_t, std::ratio<86400>> —
     * equivalent to std::chrono::days on C++20 compilers, compatible with C++17.
     *
     * @param tag_req  Tag requirement.
     * @return Duration representing days since epoch.
     * @throws error on type/tag mismatch.
     */
    std::chrono::duration<int64_t, std::ratio<86400>> as_days_duration(tag_requirement tag_req = tag_requirement::must_be_tag) const;

    /**
     * @brief  Extract as a URI (tag 32).
     * @param tag_req  Tag requirement.
     * @return The URI string.
     * @throws error on type/tag mismatch.
     */
    std::string_view as_uri(tag_requirement tag_req = tag_requirement::must_be_tag) const;

    /**
     * @brief  Extract as a base64-encoded string (tag 34).
     * @param tag_req  Tag requirement.
     * @return The base64 string.
     * @throws error on type/tag mismatch.
     */
    std::string_view as_b64_text(tag_requirement tag_req = tag_requirement::must_be_tag) const;

    /**
     * @brief  Extract as a base64url-encoded string (tag 33).
     * @param tag_req  Tag requirement.
     * @return The base64url string.
     * @throws error on type/tag mismatch.
     */
    std::string_view as_b64url(tag_requirement tag_req = tag_requirement::must_be_tag) const;

    /**
     * @brief  Extract as a regular expression pattern (tag 35).
     * @param tag_req  Tag requirement.
     * @return The regex string.
     * @throws error on type/tag mismatch.
     */
    std::string_view as_regex(tag_requirement tag_req = tag_requirement::must_be_tag) const;

    /**
     * @brief  Extract as MIME-encoded content (tag 36 or 257).
     *
     * @param[out] is_binary  If non-null, set to true when the item carries
     *                        binary MIME tag 257 instead of text MIME tag 36.
     * @param tag_req  Tag requirement.
     * @return The MIME content string.
     * @throws error on type/tag mismatch.
     */
    std::string_view as_mime_data(bool* is_binary = nullptr, tag_requirement tag_req = tag_requirement::must_be_tag) const;

    /**
     * @brief  Extract as a binary UUID (tag 37).
     *
     * The returned byte span is always 16 bytes.
     *
     * @param tag_req  Tag requirement.
     * @return The 16-byte UUID.
     * @throws error on type/tag mismatch.
     */
    const_byte_span  as_uuid(tag_requirement tag_req = tag_requirement::must_be_tag) const;

    // ── bignum / decimal fraction / bigfloat ──

    /**
     * @brief  Extract bignum bytes (tag 2 or 3).
     *
     * Returns the raw big-endian unsigned magnitude. The sign is determined
     * by type(): pos_bignum or neg_bignum.
     *
     * @return Raw bignum bytes (zero-copy, points into encoded data).
     * @throws error on type mismatch.
     */
    const_byte_span as_bignum() const;

    /**
     * @brief  Extract a decimal fraction (tag 4).
     *
     * The mantissa must fit in int64_t. For bignum mantissa, use
     * as_decimal_fraction() with tag_requirement::optional_tag and check
     * is_bignum() on the result, or catch the error.
     *
     * @param tag_req  Tag requirement.
     * @return exp_and_mantissa with exponent and integer mantissa.
     * @throws error if mantissa is a bignum that overflows int64_t.
     */
    exp_and_mantissa as_decimal_fraction(tag_requirement tag_req = tag_requirement::must_be_tag) const;

    /**
     * @brief  Extract a bigfloat (tag 5).
     *
     * The mantissa must fit in int64_t. For bignum mantissa, see
     * as_decimal_fraction() note.
     *
     * @param tag_req  Tag requirement.
     * @return exp_and_mantissa with exponent (base-2) and integer mantissa.
     * @throws error if mantissa is a bignum that overflows int64_t.
     */
    exp_and_mantissa as_bigfloat(tag_requirement tag_req = tag_requirement::must_be_tag) const;

    // ── nested key access ──

    /**
     * @brief  Chain into a nested map by text label.
     *
     * Enters the current item as a map (consuming it), then looks up the
     * given sub-key.
     *
     * @param key  Text label in the nested map.
     * @return An item_proxy for assigning or reading the nested value.
     */
    item_proxy operator[](std::string_view key);

    /**
     * @brief  Chain into a nested map by integer label.
     * @param key  Integer label in the nested map.
     * @return An item_proxy for the nested value.
     */
    item_proxy operator[](int64_t key);

    /**
     * @brief  Chain into a nested map by int label.
     *
     * Delegates to the int64_t overload.
     *
     * @param key  Integer label (promoted to int64_t).
     * @return An item_proxy for the nested value.
     */
    item_proxy operator[](int key) { return (*this)[static_cast<int64_t>(key)]; }

    /**
     * @brief  Chain into a nested map by null-terminated string label.
     * @param key  Pointer to a null-terminated label string.
     * @return An item_proxy for the nested value.
     */
    item_proxy operator[](const char* key);
};

// ============================================================================
// array_scope -- returned by dec.array() and item_proxy::as_array()
// ============================================================================
//
// Auto-closing scope for sequential array access. Closes the array in the
// decoder on destruction unless moved from or manually exited.

class array_scope {
    friend class decoder;
    friend class item_proxy;
    decoder* dec_;
    bool     exited_ = false;
    size_t   count_  = 0;

    explicit array_scope(decoder& d) noexcept : dec_(&d), count_(d.array_count_) {}

public:
    ~array_scope();
    array_scope(array_scope&& o) noexcept;
    array_scope& operator=(array_scope&& o) noexcept;

    array_scope(const array_scope&) = delete;
    array_scope& operator=(const array_scope&) = delete;

    /**
     * @brief  Check whether all items in the array have been consumed.
     *
     * Uses QCBORDecode_PeekNext to test whether the next item is
     * NO_MORE_ITEMS or HIT_END.
     *
     * @return true when iteration is complete.
     */
    bool done() const;

    /**
     * @brief  Read the next item from the array.
     *
     * For scalar items, the item is decoded and cached in the returned
     * item_proxy. For container items (maps/arrays), a bare proxy is
     * returned — call as_map() or as_array() to enter.
     *
     * @return An item_proxy for the next item.
     * @throws error if the array has been fully consumed.
     */
    item_proxy next();

    /**
     * @brief  Return the total number of items in the array.
     *
     * This is the count reported by the CBOR array header. It does not
     * change as items are consumed.
     *
     * @return The item count.
     */
    size_t size() const noexcept { return count_; }

    /**
     * @brief  Access the parent decoder.
     * @return Reference to the decoder this array belongs to.
     */
    decoder& dec() noexcept { return *dec_; }
};

// ============================================================================
// Inline implementations
// ============================================================================

// ── decoder ──

inline map_scope decoder::map() {
    if (top_set_) throw error(errc::close_mismatch);
    if (finished_) throw error(errc::no_more_items);
    enter_map();
    top_set_ = true;
    top_map_ = true;
    map_scope ms{*this};
    if (force_prefetch_)
        ms.prefetch();
    return ms;
}

inline array_scope decoder::array() {
    if (top_set_) throw error(errc::close_mismatch);
    if (finished_) throw error(errc::no_more_items);
    enter_array();
    top_set_ = true;
    top_map_ = false;
    return array_scope{*this};
}

inline decoded_item decoder::v_get_next() {
    QCBORItem item;
    QCBORDecode_VGetNext(&ctx_, &item);
    check_err();
    return convert_item(item);
}

inline decoded_item decoder::v_peek_next() {
    QCBORItem item;
    QCBORDecode_VPeekNext(&ctx_, &item);
    check_err();
    return convert_item(item);
}

inline decoded_item decoder::get_next() {
    QCBORItem item;
    QCBORError e = QCBORDecode_GetNext(&ctx_, &item);
    if (e != QCBOR_SUCCESS)
        throw error(static_cast<errc>(e));
    return convert_item(item);
}

// ── map_scope ──

inline map_scope::~map_scope() {
    if (dec_ && !exited_ && !dec_->finished_ && dec_->map_depth_ > 0) {
        try { dec_->exit_map(); } catch (...) {}
    }
}

inline map_scope::map_scope(map_scope&& o) noexcept
    : dec_(o.dec_), exited_(o.exited_), last_label_(std::move(o.last_label_))
    , cache_(std::move(o.cache_)), int_cache_(std::move(o.int_cache_))
    , prefetched_(o.prefetched_) { o.dec_ = nullptr; }

inline map_scope& map_scope::operator=(map_scope&& o) noexcept {
    if (this != &o) {
        if (dec_ && !exited_) { try { dec_->exit_map(); } catch (...) {} }
        dec_ = o.dec_; exited_ = o.exited_; last_label_ = std::move(o.last_label_);
        cache_      = std::move(o.cache_);
        int_cache_  = std::move(o.int_cache_);
        prefetched_ = o.prefetched_;
        o.dec_ = nullptr;
    }
    return *this;
}

// ── map_scope::prefetch_cb + prefetch + operator[] ──

inline QCBORError map_scope::prefetch_cb(void* ctx, const QCBORItem* item) {
    auto* self = static_cast<map_scope*>(ctx);
    if (item->uLabelType == QCBOR_TYPE_TEXT_STRING) {
        std::string label(static_cast<const char*>(item->label.string.ptr),
                          item->label.string.len);
        self->cache_.emplace(std::move(label), decoder::convert_item(*item));
    } else if (item->uLabelType == QCBOR_TYPE_INT64 || item->uLabelType == QCBOR_TYPE_UINT64) {
        self->int_cache_.emplace_back(item->label.int64, decoder::convert_item(*item));
    }
    return QCBOR_SUCCESS;
}

inline void map_scope::prefetch() {
    if (prefetched_) return;
    prefetched_ = true;

    // Use GetItemsInMapWithCallback with an empty item list so the
    // callback fires for every top-level entry.  MapSearch internally
    // saves/rewinds/restores the cursor — the decode context is left
    // exactly as it was before the call, so all Spiffy lookups
    // (EnterMapFromMapSZ, GetInt64ConvertInMapSZ, …) continue to work.
    QCBORItem sentinel{};
    QCBORDecode_GetItemsInMapWithCallback(
        dec_->raw_ctx(), &sentinel, this, prefetch_cb);
    dec_->check_err();
}

inline item_proxy map_scope::operator[](std::string_view key) {
    last_label_ = std::string(key);
    if (prefetched_) {
        auto it = cache_.find(last_label_);
        if (it != cache_.end())
            return item_proxy{*dec_, it->second, last_label_};
    }
    return item_proxy{*dec_, last_label_, false, 0};
}

inline item_proxy map_scope::operator[](int64_t key) {
    if (prefetched_) {
        for (auto& [ik, iv] : int_cache_) {
            if (ik == key)
                return item_proxy{*dec_, iv, key};
        }
    }
    return item_proxy{*dec_, {}, true, key};
}

inline item_proxy map_scope::operator[](const char* key) {
    return (*this)[std::string_view(key)];
}

// Delayed definition: map_scope::operator[](int) needs item_proxy complete
inline item_proxy map_scope::operator[](int key) {
    return (*this)[static_cast<int64_t>(key)];
}

// ── contains / size / for_each ──

inline size_t map_scope::size() {
    if (!prefetched_) prefetch();
    return cache_.size() + int_cache_.size();
}

inline bool map_scope::contains(std::string_view key) {
    if (!prefetched_) prefetch();
    return cache_.find(std::string(key)) != cache_.end();
}

inline bool map_scope::contains(int64_t key) {
    if (!prefetched_) prefetch();
    // Scan int-keyed cache for the requested key.
    for (auto& entry : int_cache_) {
        if (entry.first == key) return true;
    }
    return false;
}

template<typename F>
inline map_scope& map_scope::for_each(F&& visitor) {
    if (!prefetched_) prefetch();
    // prefetch() stores all string-keyed entries in cache_.
    // Integer-keyed entries are not in the cache.
    for (auto& [key, val] : cache_)
        visitor(std::string_view(key), val);
    return *this;
}

template<typename F>
inline map_scope& map_scope::for_each_int(F&& visitor) {
    if (!prefetched_) prefetch();
    for (auto& entry : int_cache_)
        visitor(entry.first, entry.second);
    return *this;
}

inline std::error_code map_scope::get_items(
        const std::vector<decoder::item_spec>& specs,
        std::vector<decoded_item>& out) {
    out.clear();
    if (specs.empty()) return {};
    out.reserve(specs.size());

    std::vector<QCBORItem> items(specs.size() + 1);
    for (size_t i = 0; i < specs.size(); ++i) {
        QCBORItem& qi = items[i];
        memset(&qi, 0, sizeof(qi));
        if (specs[i].is_int_label) {
            qi.uLabelType = QCBOR_TYPE_INT64;
            qi.label.int64 = specs[i].label_int;
        } else if (specs[i].label_str) {
            qi.uLabelType = QCBOR_TYPE_TEXT_STRING;
            qi.label.string.ptr  = specs[i].label_str;
            qi.label.string.len  = strlen(specs[i].label_str);
        }
        qi.uDataType = static_cast<uint8_t>(specs[i].type);
    }

    QCBORDecode_GetItemsInMap(dec_->raw_ctx(), items.data());
    QCBORError err = QCBORDecode_GetError(dec_->raw_ctx());
    if (err != QCBOR_SUCCESS)
        return make_error_code(static_cast<errc>(err));

    for (size_t i = 0; i < specs.size(); ++i)
        out.push_back(decoder::convert_item(items[i]));

    return {};
}

// ── array_scope ──

inline array_scope::~array_scope() {
    if (dec_ && !exited_ && !dec_->finished_ && dec_->array_depth_ > 0) {
        try { dec_->exit_array(); } catch (...) {}
    }
}

inline array_scope::array_scope(array_scope&& o) noexcept
    : dec_(o.dec_), exited_(o.exited_), count_(o.count_) { o.dec_ = nullptr; }

inline array_scope& array_scope::operator=(array_scope&& o) noexcept {
    if (this != &o) {
        if (dec_ && !exited_) { try { dec_->exit_array(); } catch (...) {} }
        dec_ = o.dec_; exited_ = o.exited_; count_ = o.count_; o.dec_ = nullptr;
    }
    return *this;
}

inline bool array_scope::done() const {
    QCBORItem item;
    QCBORError e = QCBORDecode_PeekNext(dec_->raw_ctx(), &item);
    return e == QCBOR_ERR_NO_MORE_ITEMS || e == QCBOR_ERR_HIT_END;
}

// Strategy for next():
//   Peek to decide container vs scalar. Do NOT consume container headers —
//   as_map()/as_array() need to consume them via Enter*.
//   Use PeekNext (returns QCBORError) not VPeekNext (sets uLastError).

inline item_proxy array_scope::next() {
    QCBORItem peek;
    QCBORError e = QCBORDecode_PeekNext(dec_->raw_ctx(), &peek);
    if (e != QCBOR_SUCCESS) {
        // Force error through VGetNext + check_err to get exception
        QCBORItem dummy;
        QCBORDecode_VGetNext(dec_->raw_ctx(), &dummy);
        dec_->check_err();
    }
    if (peek.uDataType == QCBOR_TYPE_MAP || peek.uDataType == QCBOR_TYPE_ARRAY) {
        // Container: bare proxy. as_map()/as_array() call Enter* to consume header.
        return item_proxy{*dec_};
    }
    // Scalar: VGetNext consumes + cache for getters.
    decoded_item di = dec_->v_get_next();
    return item_proxy{*dec_, di};
}

// ── item_proxy implicit conversions ──

inline item_proxy::operator int64_t() const { return get_int64(); }
inline item_proxy::operator uint64_t() const { return get_uint64(); }
inline item_proxy::operator std::string_view() const { return as_string(); }
inline item_proxy::operator double() const { return get_double(); }
inline item_proxy::operator bool() const { return as_bool(); }
inline item_proxy::operator const_byte_span() const { return as_bytes(); }

// ── get_or safe access ──

inline int64_t item_proxy::get_or(int64_t default_val) const noexcept {
    try { return get_int64(); } catch (...) { return default_val; }
}

inline uint64_t item_proxy::get_or(uint64_t default_val) const noexcept {
    try { return get_uint64(); } catch (...) { return default_val; }
}

inline std::string_view item_proxy::get_or(std::string_view default_val) const noexcept {
    try { return as_string(); } catch (...) { return default_val; }
}

inline double item_proxy::get_or(double default_val) const noexcept {
    try { return get_double(); } catch (...) { return default_val; }
}

inline bool item_proxy::get_or(bool default_val) const noexcept {
    try { return as_bool(); } catch (...) { return default_val; }
}

// ── loose numeric getters (cross-type convert) ──

inline int64_t item_proxy::get_int64() const {
    if (has_cached_) {
        switch (cached_.type) {
        case cbor_type::int64:  return cached_.value.int64_val;
        case cbor_type::uint64: return static_cast<int64_t>(cached_.value.uint64_val);
        case cbor_type::double_v: return static_cast<int64_t>(std::llround(cached_.value.double_val));
        case cbor_type::float_v:  return static_cast<int64_t>(std::llround(cached_.value.float_val));
        default:
            dec_->ctx_.uLastError = QCBOR_ERR_UNEXPECTED_TYPE;
            dec_->check_err();
            return 0;
        }
    }
    int64_t v = 0;
    if (is_int_label_) {
        QCBORDecode_GetInt64ConvertInMapN(dec_->raw_ctx(), int_label_,
            QCBOR_CONVERT_TYPE_XINT64 | QCBOR_CONVERT_TYPE_FLOAT, &v);
    } else if (!str_label_.empty()) {
        QCBORDecode_GetInt64ConvertInMapSZ(dec_->raw_ctx(), str_label_.c_str(),
            QCBOR_CONVERT_TYPE_XINT64 | QCBOR_CONVERT_TYPE_FLOAT, &v);
    } else {
        QCBORDecode_GetInt64Convert(dec_->raw_ctx(),
            QCBOR_CONVERT_TYPE_XINT64 | QCBOR_CONVERT_TYPE_FLOAT, &v);
    }
    dec_->auto_rewind_check();
    return v;
}

inline uint64_t item_proxy::get_uint64() const {
    if (has_cached_) {
        switch (cached_.type) {
        case cbor_type::uint64: return cached_.value.uint64_val;
        case cbor_type::int64:  return static_cast<uint64_t>(cached_.value.int64_val);
        case cbor_type::double_v: return static_cast<uint64_t>(std::llround(cached_.value.double_val));
        case cbor_type::float_v:  return static_cast<uint64_t>(std::llround(cached_.value.float_val));
        default:
            dec_->ctx_.uLastError = QCBOR_ERR_UNEXPECTED_TYPE;
            dec_->check_err();
            return 0;
        }
    }
    uint64_t v = 0;
    if (is_int_label_) {
        QCBORDecode_GetUInt64ConvertInMapN(dec_->raw_ctx(), int_label_,
            QCBOR_CONVERT_TYPE_XINT64, &v);
    } else if (!str_label_.empty()) {
        QCBORDecode_GetUInt64ConvertInMapSZ(dec_->raw_ctx(), str_label_.c_str(),
            QCBOR_CONVERT_TYPE_XINT64, &v);
    } else {
        QCBORDecode_GetUInt64Convert(dec_->raw_ctx(),
            QCBOR_CONVERT_TYPE_XINT64, &v);
    }
    dec_->auto_rewind_check();
    return v;
}

inline double item_proxy::get_double() const {
    if (has_cached_) {
        switch (cached_.type) {
        case cbor_type::double_v: return cached_.value.double_val;
        case cbor_type::float_v:  return static_cast<double>(cached_.value.float_val);
        case cbor_type::int64:    return static_cast<double>(cached_.value.int64_val);
        case cbor_type::uint64:   return static_cast<double>(cached_.value.uint64_val);
        default:
            dec_->ctx_.uLastError = QCBOR_ERR_UNEXPECTED_TYPE;
            dec_->check_err();
            return 0.0;
        }
    }
    double v = 0.0;
    if (is_int_label_) {
        QCBORDecode_GetDoubleConvertInMapN(dec_->raw_ctx(), int_label_,
            QCBOR_CONVERT_TYPE_XINT64 | QCBOR_CONVERT_TYPE_FLOAT, &v);
    } else if (!str_label_.empty()) {
        QCBORDecode_GetDoubleConvertInMapSZ(dec_->raw_ctx(), str_label_.c_str(),
            QCBOR_CONVERT_TYPE_XINT64 | QCBOR_CONVERT_TYPE_FLOAT, &v);
    } else {
        QCBORDecode_GetDoubleConvert(dec_->raw_ctx(),
            QCBOR_CONVERT_TYPE_XINT64 | QCBOR_CONVERT_TYPE_FLOAT, &v);
    }
    dec_->auto_rewind_check();
    return v;
}

inline int64_t item_proxy::as_int64() const {
    if (has_cached_) { check_cache_type(cbor_type::int64); return cached_.value.int64_val; }
    int64_t v = 0;
    if (is_int_label_) {
        QCBORDecode_GetInt64InMapN(dec_->raw_ctx(), int_label_, &v);
    } else if (!str_label_.empty()) {
        QCBORDecode_GetInt64InMapSZ(dec_->raw_ctx(), str_label_.c_str(), &v);
    } else {
        QCBORDecode_GetInt64(dec_->raw_ctx(), &v);
    }
    dec_->auto_rewind_check();
    return v;
}

inline uint64_t item_proxy::as_uint64() const {
    if (has_cached_) { check_cache_type(cbor_type::uint64); return cached_.value.uint64_val; }
    uint64_t v = 0;
    if (is_int_label_) {
        QCBORDecode_GetUInt64InMapN(dec_->raw_ctx(), int_label_, &v);
    } else if (!str_label_.empty()) {
        QCBORDecode_GetUInt64InMapSZ(dec_->raw_ctx(), str_label_.c_str(), &v);
    } else {
        QCBORDecode_GetUInt64(dec_->raw_ctx(), &v);
    }
    dec_->auto_rewind_check();
    return v;
}

inline std::string_view item_proxy::as_string() const {
    if (has_cached_) { check_cache_type(cbor_type::text_string); return cached_.value.text; }
    UsefulBufC text{nullptr, 0};
    if (is_int_label_) {
        QCBORDecode_GetTextStringInMapN(dec_->raw_ctx(), int_label_, &text);
    } else if (!str_label_.empty()) {
        QCBORDecode_GetTextStringInMapSZ(dec_->raw_ctx(), str_label_.c_str(), &text);
    } else {
        QCBORDecode_GetTextString(dec_->raw_ctx(), &text);
    }
    dec_->auto_rewind_check();
    return {static_cast<const char*>(text.ptr), text.len};
}

inline const_byte_span item_proxy::as_bytes() const {
    if (has_cached_) { check_cache_type(cbor_type::byte_string); return cached_.value.bytes; }
    UsefulBufC bytes{nullptr, 0};
    if (is_int_label_) {
        QCBORDecode_GetByteStringInMapN(dec_->raw_ctx(), int_label_, &bytes);
    } else if (!str_label_.empty()) {
        QCBORDecode_GetByteStringInMapSZ(dec_->raw_ctx(), str_label_.c_str(), &bytes);
    } else {
        QCBORDecode_GetByteString(dec_->raw_ctx(), &bytes);
    }
    dec_->auto_rewind_check();
    return {static_cast<const uint8_t*>(bytes.ptr), bytes.len};
}

inline double item_proxy::as_double() const {
    if (has_cached_) { check_cache_type(cbor_type::double_v); return cached_.value.double_val; }
    double v = 0.0;
    if (is_int_label_) {
        QCBORDecode_GetDoubleInMapN(dec_->raw_ctx(), int_label_, &v);
    } else if (!str_label_.empty()) {
        QCBORDecode_GetDoubleInMapSZ(dec_->raw_ctx(), str_label_.c_str(), &v);
    } else {
        QCBORDecode_GetDouble(dec_->raw_ctx(), &v);
    }
    dec_->auto_rewind_check();
    return v;
}

inline bool item_proxy::as_bool() const {
    if (has_cached_) { check_cache_type(cbor_type::true_v); return cached_.value.bool_val; }
    bool v = false;
    if (is_int_label_) {
        QCBORDecode_GetBoolInMapN(dec_->raw_ctx(), int_label_, &v);
    } else if (!str_label_.empty()) {
        QCBORDecode_GetBoolInMapSZ(dec_->raw_ctx(), str_label_.c_str(), &v);
    } else {
        QCBORDecode_GetBool(dec_->raw_ctx(), &v);
    }
    dec_->auto_rewind_check();
    return v;
}

// ── item_proxy sub-structures ──

inline map_scope item_proxy::as_map() {
    if (is_int_label_) {
        QCBORDecode_EnterMapFromMapN(dec_->raw_ctx(), int_label_);
    } else if (!str_label_.empty()) {
        QCBORDecode_EnterMapFromMapSZ(dec_->raw_ctx(), str_label_.c_str());
    } else {
        QCBORDecode_EnterMap(dec_->raw_ctx(), nullptr);
    }
    dec_->auto_rewind_check();
    dec_->map_depth_++;
    map_scope ms{*dec_};
    if (dec_->force_prefetch_)
        ms.prefetch();
    return ms;
}

inline array_scope item_proxy::as_array() {
    if (is_int_label_) {
        QCBORDecode_EnterArrayFromMapN(dec_->raw_ctx(), int_label_);
    } else if (!str_label_.empty()) {
        QCBORDecode_EnterArrayFromMapSZ(dec_->raw_ctx(), str_label_.c_str());
    } else {
        QCBORDecode_EnterArray(dec_->raw_ctx(), nullptr);
    }
    dec_->auto_rewind_check();
    dec_->array_depth_++;
    return array_scope{*dec_};
}

// ── item_proxy tagged getters ──

#define QCBORPP_TAGGED_SZ_GETTER(fn_name, spiffy_fn) \
inline std::string_view item_proxy::fn_name(tag_requirement tag_req) const { \
    if (has_cached_) { \
        (void)tag_req; \
        return cached_.value.text; \
    } \
    UsefulBufC result{nullptr, 0}; \
    if (is_int_label_) { \
        spiffy_fn##InMapN(dec_->raw_ctx(), int_label_, static_cast<uint8_t>(tag_req), &result); \
    } else if (!str_label_.empty()) { \
        spiffy_fn##InMapSZ(dec_->raw_ctx(), str_label_.c_str(), static_cast<uint8_t>(tag_req), &result); \
    } else { \
        spiffy_fn(dec_->raw_ctx(), static_cast<uint8_t>(tag_req), &result); \
    } \
    dec_->auto_rewind_check(); \
    return {static_cast<const char*>(result.ptr), result.len}; \
}

QCBORPP_TAGGED_SZ_GETTER(as_date_string, QCBORDecode_GetDateString)
QCBORPP_TAGGED_SZ_GETTER(as_days_string, QCBORDecode_GetDaysString)
QCBORPP_TAGGED_SZ_GETTER(as_uri,         QCBORDecode_GetURI)
QCBORPP_TAGGED_SZ_GETTER(as_b64_text,    QCBORDecode_GetB64)
QCBORPP_TAGGED_SZ_GETTER(as_b64url,      QCBORDecode_GetB64URL)
QCBORPP_TAGGED_SZ_GETTER(as_regex,       QCBORDecode_GetRegex)

#undef QCBORPP_TAGGED_SZ_GETTER

// ── item_proxy tagged int getters ──

#define QCBORPP_TAGGED_INT_GETTER(fn_name, spiffy_fn) \
inline int64_t item_proxy::fn_name(tag_requirement tag_req) const { \
    if (has_cached_) { \
        (void)tag_req; \
        return cached_.value.int64_val; \
    } \
    int64_t result = 0; \
    if (is_int_label_) { \
        spiffy_fn##InMapN(dec_->raw_ctx(), int_label_, static_cast<uint8_t>(tag_req), &result); \
    } else if (!str_label_.empty()) { \
        spiffy_fn##InMapSZ(dec_->raw_ctx(), str_label_.c_str(), static_cast<uint8_t>(tag_req), &result); \
    } else { \
        spiffy_fn(dec_->raw_ctx(), static_cast<uint8_t>(tag_req), &result); \
    } \
    dec_->auto_rewind_check(); \
    return result; \
}

QCBORPP_TAGGED_INT_GETTER(as_date_epoch, QCBORDecode_GetEpochDate)
QCBORPP_TAGGED_INT_GETTER(as_days_epoch,  QCBORDecode_GetEpochDays)

#undef QCBORPP_TAGGED_INT_GETTER

// ── item_proxy chrono getters ──

inline std::chrono::system_clock::time_point
item_proxy::as_time_point(tag_requirement tag_req) const {
    return std::chrono::system_clock::from_time_t(
        static_cast<std::time_t>(as_date_epoch(tag_req)));
}

inline std::chrono::duration<int64_t, std::ratio<86400>>
item_proxy::as_days_duration(tag_requirement tag_req) const {
    return std::chrono::duration<int64_t, std::ratio<86400>>(as_days_epoch(tag_req));
}

inline const_byte_span item_proxy::as_bignum() const {
    // Cache fast path — convert_item now caches bignum bytes.
    // Array path next() consumes via VGetNext and caches; map path
    // via InMapN/InMapSZ falls through to Spiffy.
    if (has_cached_) {
        if (cached_.type != cbor_type::pos_bignum &&
            cached_.type != cbor_type::neg_bignum) {
            dec_->ctx_.uLastError = QCBOR_ERR_UNEXPECTED_TYPE;
            dec_->check_err();
        }
        return cached_.value.bytes;
    }
    // Map-path fallback: use QCBOR Spiffy.
    UsefulBufC result{nullptr, 0};
    bool is_negative = false;
    if (is_int_label_) {
        QCBORDecode_GetBignumInMapN(dec_->raw_ctx(), int_label_,
            static_cast<uint8_t>(tag_requirement::must_be_tag), &result, &is_negative);
    } else if (!str_label_.empty()) {
        QCBORDecode_GetBignumInMapSZ(dec_->raw_ctx(), str_label_.c_str(),
            static_cast<uint8_t>(tag_requirement::must_be_tag), &result, &is_negative);
    } else {
        QCBORDecode_GetBignum(dec_->raw_ctx(),
            static_cast<uint8_t>(tag_requirement::must_be_tag), &result, &is_negative);
    }
    dec_->auto_rewind_check();
    (void)is_negative; // sign is reflected in type(), caller checks pos/neg_bignum
    return {static_cast<const uint8_t*>(result.ptr), result.len};
}

inline exp_and_mantissa item_proxy::as_decimal_fraction(tag_requirement tag_req) const {
    // Cache fast path — convert_item now caches exp_mantissa.
    if (has_cached_) {
        if (cached_.type != cbor_type::decimal_fraction &&
            cached_.type != cbor_type::decimal_fraction_pos_bignum &&
            cached_.type != cbor_type::decimal_fraction_neg_bignum) {
            dec_->ctx_.uLastError = QCBOR_ERR_UNEXPECTED_TYPE;
            dec_->check_err();
        }
        // These cbor_types are always tagged — must_not_be_tag is an error.
        if (tag_req == tag_requirement::must_not_be_tag) {
            dec_->ctx_.uLastError = QCBOR_ERR_UNEXPECTED_TYPE;
            dec_->check_err();
        }
        return cached_.value.exp_mantissa;
    }
    // Map-path fallback: use QCBOR Spiffy.
    int64_t mantissa = 0, exponent = 0;
    if (is_int_label_) {
        QCBORDecode_GetDecimalFractionInMapN(dec_->raw_ctx(), int_label_,
            static_cast<uint8_t>(tag_req), &mantissa, &exponent);
    } else if (!str_label_.empty()) {
        QCBORDecode_GetDecimalFractionInMapSZ(dec_->raw_ctx(), str_label_.c_str(),
            static_cast<uint8_t>(tag_req), &mantissa, &exponent);
    } else {
        QCBORDecode_GetDecimalFraction(dec_->raw_ctx(),
            static_cast<uint8_t>(tag_req), &mantissa, &exponent);
    }
    dec_->auto_rewind_check();
    return exp_and_mantissa{exponent, mantissa};
}

inline exp_and_mantissa item_proxy::as_bigfloat(tag_requirement tag_req) const {
    // Cache fast path — convert_item now caches exp_mantissa.
    if (has_cached_) {
        if (cached_.type != cbor_type::bigfloat &&
            cached_.type != cbor_type::bigfloat_pos_bignum &&
            cached_.type != cbor_type::bigfloat_neg_bignum) {
            dec_->ctx_.uLastError = QCBOR_ERR_UNEXPECTED_TYPE;
            dec_->check_err();
        }
        if (tag_req == tag_requirement::must_not_be_tag) {
            dec_->ctx_.uLastError = QCBOR_ERR_UNEXPECTED_TYPE;
            dec_->check_err();
        }
        return cached_.value.exp_mantissa;
    }
    // Map-path fallback: use QCBOR Spiffy.
    int64_t mantissa = 0, exponent = 0;
    if (is_int_label_) {
        QCBORDecode_GetBigFloatInMapN(dec_->raw_ctx(), int_label_,
            static_cast<uint8_t>(tag_req), &mantissa, &exponent);
    } else if (!str_label_.empty()) {
        QCBORDecode_GetBigFloatInMapSZ(dec_->raw_ctx(), str_label_.c_str(),
            static_cast<uint8_t>(tag_req), &mantissa, &exponent);
    } else {
        QCBORDecode_GetBigFloat(dec_->raw_ctx(),
            static_cast<uint8_t>(tag_req), &mantissa, &exponent);
    }
    dec_->auto_rewind_check();
    return exp_and_mantissa{exponent, mantissa};
}

inline std::string_view item_proxy::as_mime_data(bool* is_binary, tag_requirement tag_req) const {
    if (has_cached_) {
        (void)tag_req;
        if (is_binary) *is_binary = false;
        return cached_.value.text;
    }
    UsefulBufC result{nullptr, 0};
    bool is_tag257 = false;
    if (is_int_label_) {
        QCBORDecode_GetMIMEMessageInMapN(dec_->raw_ctx(), int_label_, static_cast<uint8_t>(tag_req), &result, &is_tag257);
    } else if (!str_label_.empty()) {
        QCBORDecode_GetMIMEMessageInMapSZ(dec_->raw_ctx(), str_label_.c_str(), static_cast<uint8_t>(tag_req), &result, &is_tag257);
    } else {
        QCBORDecode_GetMIMEMessage(dec_->raw_ctx(), static_cast<uint8_t>(tag_req), &result, &is_tag257);
    }
    dec_->auto_rewind_check();
    if (is_binary) *is_binary = is_tag257;
    return {static_cast<const char*>(result.ptr), result.len};
}

inline const_byte_span item_proxy::as_uuid(tag_requirement tag_req) const {
    if (has_cached_) {
        (void)tag_req;
        return cached_.value.bytes;
    }
    UsefulBufC result{nullptr, 0};
    if (is_int_label_) {
        QCBORDecode_GetBinaryUUIDInMapN(dec_->raw_ctx(), int_label_, static_cast<uint8_t>(tag_req), &result);
    } else if (!str_label_.empty()) {
        QCBORDecode_GetBinaryUUIDInMapSZ(dec_->raw_ctx(), str_label_.c_str(), static_cast<uint8_t>(tag_req), &result);
    } else {
        QCBORDecode_GetBinaryUUID(dec_->raw_ctx(), static_cast<uint8_t>(tag_req), &result);
    }
    dec_->auto_rewind_check();
    return {static_cast<const uint8_t*>(result.ptr), result.len};
}

// ── item_proxy type queries ──

inline cbor_type item_proxy::type() const {
    if (has_cached_)
        return cached_.type;

    if (is_int_label_ || !str_label_.empty()) {
        QCBORItem item;
        if (is_int_label_) {
            QCBORDecode_GetItemInMapN(dec_->raw_ctx(), int_label_,
                                      QCBOR_TYPE_ANY, &item);
        } else {
            QCBORDecode_GetItemInMapSZ(dec_->raw_ctx(), str_label_.c_str(),
                                       QCBOR_TYPE_ANY, &item);
        }
        dec_->auto_rewind_check();
        return static_cast<cbor_type>(item.uDataType);
    }

    // Bare proxy (from array_scope::next() for containers):
    // PeekNext to determine type without consuming the header
    QCBORItem item;
    QCBORError e = QCBORDecode_PeekNext(dec_->raw_ctx(), &item);
    if (e != QCBOR_SUCCESS)
        throw error(static_cast<errc>(e));
    return static_cast<cbor_type>(item.uDataType);
}

inline bool item_proxy::is_int64()  const { return type() == cbor_type::int64; }
inline bool item_proxy::is_uint64() const { return type() == cbor_type::uint64; }
inline bool item_proxy::is_double() const { return type() == cbor_type::double_v; }
inline bool item_proxy::is_float()  const { return type() == cbor_type::float_v; }
inline bool item_proxy::is_string() const { return type() == cbor_type::text_string; }
inline bool item_proxy::is_bytes()  const { return type() == cbor_type::byte_string; }
inline bool item_proxy::is_bool()   const {
    auto t = type(); return t == cbor_type::true_v || t == cbor_type::false_v;
}
inline bool item_proxy::is_bool_true()  const { return type() == cbor_type::true_v; }
inline bool item_proxy::is_bool_false() const { return type() == cbor_type::false_v; }
inline bool item_proxy::is_null()   const { return type() == cbor_type::null_v; }
inline bool item_proxy::is_undef()  const { return type() == cbor_type::undef_v; }
inline bool item_proxy::is_map()    const { return type() == cbor_type::map; }
inline bool item_proxy::is_array()  const { return type() == cbor_type::array; }
inline bool item_proxy::is_simple() const {
    return type() == cbor_type::unknown_simple;
}
inline bool item_proxy::is_container() const {
    auto t = type(); return t == cbor_type::map || t == cbor_type::array;
}

inline bool item_proxy::is_tag() const {
    auto t = static_cast<uint8_t>(type());
    // Tagged types: bignums (9-12), decimal_fraction/bigfloat (14-19),
    // wrapped_cbor (36), text-tag group (44-49), binary_mime/days (76-78)
    return (t >= 9 && t <= 12) || (t >= 14 && t <= 19)
        || t == 36 || (t >= 44 && t <= 49) || (t >= 76 && t <= 78);
}

inline std::optional<uint64_t> item_proxy::tag_number() {
    if (!has_cached_)
        resolve(); // may throw if out of scope
    switch (cached_.type) {
    case cbor_type::date_string:            return std::optional<uint64_t>(0);
    case cbor_type::date_epoch:            return std::optional<uint64_t>(1);
    case cbor_type::pos_bignum:            return std::optional<uint64_t>(2);
    case cbor_type::neg_bignum:            return std::optional<uint64_t>(3);
    case cbor_type::decimal_fraction:
    case cbor_type::decimal_fraction_pos_bignum:
    case cbor_type::decimal_fraction_neg_bignum: return std::optional<uint64_t>(4);
    case cbor_type::bigfloat:
    case cbor_type::bigfloat_pos_bignum:
    case cbor_type::bigfloat_neg_bignum:        return std::optional<uint64_t>(5);
    case cbor_type::wrapped_cbor:          return std::optional<uint64_t>(24);
    case cbor_type::uri:                   return std::optional<uint64_t>(32);
    case cbor_type::base64url:             return std::optional<uint64_t>(33);
    case cbor_type::base64:                return std::optional<uint64_t>(34);
    case cbor_type::regex:                 return std::optional<uint64_t>(35);
    case cbor_type::mime:
    case cbor_type::binary_mime:           return std::optional<uint64_t>(36);
    case cbor_type::uuid:                  return std::optional<uint64_t>(37);
    case cbor_type::days_epoch:            return std::optional<uint64_t>(100);
    case cbor_type::days_string:           return std::optional<uint64_t>(1004);
    default: return std::nullopt;
    }
}

// ── item_proxy nested key chaining ──

inline item_proxy item_proxy::operator[](std::string_view subkey) {
    if (is_int_label_) {
        QCBORDecode_EnterMapFromMapN(dec_->raw_ctx(), int_label_);
    } else if (!str_label_.empty()) {
        QCBORDecode_EnterMapFromMapSZ(dec_->raw_ctx(), str_label_.c_str());
    } else {
        QCBORDecode_EnterMap(dec_->raw_ctx(), nullptr);
    }
    dec_->auto_rewind_check();
    dec_->map_depth_++;
    return item_proxy{*dec_, std::string(subkey), false, 0};
}

inline item_proxy item_proxy::operator[](int64_t subkey) {
    return item_proxy{*dec_, {}, true, subkey};
}

inline item_proxy item_proxy::operator[](const char* subkey) {
    return (*this)[std::string_view(subkey)];
}

// ── item_proxy lifecycle observability ──

inline bool item_proxy::is_resolved() const noexcept {
    return has_cached_;
}

inline void item_proxy::resolve() {
    if (has_cached_) return;

    QCBORItem item;
    if (is_int_label_) {
        QCBORDecode_GetItemInMapN(dec_->raw_ctx(), int_label_,
                                  QCBOR_TYPE_ANY, &item);
        dec_->auto_rewind_check();
    } else if (!str_label_.empty()) {
        QCBORDecode_GetItemInMapSZ(dec_->raw_ctx(), str_label_.c_str(),
                                   QCBOR_TYPE_ANY, &item);
        dec_->auto_rewind_check();
    } else {
        // Bare proxy — nothing to resolve (will fail on access)
        return;
    }
    cached_ = dec_->convert_item(item);
    has_cached_ = true;
}

// ============================================================================
// decoder static/private method implementations
// ============================================================================

inline decoded_item decoder::convert_item(const QCBORItem& item) {
    decoded_item di;
    di.type = static_cast<cbor_type>(item.uDataType);
    di.label_type = static_cast<cbor_type>(item.uLabelType);
    di.nesting_level = item.uNestingLevel;
    di.next_nesting_level = item.uNextNestLevel;
    di.data_allocated = item.uDataAlloc != 0;
    di.label_allocated = item.uLabelAlloc != 0;

    switch (item.uDataType) {
    case QCBOR_TYPE_INT64:
        di.value.int64_val = item.val.int64;
        break;
    case QCBOR_TYPE_UINT64:
        di.value.uint64_val = item.val.uint64;
        break;
    case QCBOR_TYPE_DOUBLE:
        di.value.double_val = item.val.dfnum;
        break;
    case QCBOR_TYPE_FLOAT:
        di.value.float_val = item.val.fnum;
        break;
    case QCBOR_TYPE_TRUE:
        di.value.bool_val = true;
        break;
    case QCBOR_TYPE_FALSE:
        di.value.bool_val = false;
        break;
    case QCBOR_TYPE_NULL:
        break;
    case QCBOR_TYPE_UNDEF:
    case QCBOR_TYPE_TEXT_STRING:
        di.value.text = std::string_view(
            static_cast<const char*>(item.val.string.ptr),
            item.val.string.len);
        break;
    case QCBOR_TYPE_BYTE_STRING:
        di.value.bytes = const_byte_span(
            static_cast<const uint8_t*>(item.val.string.ptr),
            item.val.string.len);
        break;
    // Spiffy tagged types — carry text or byte data
    case QCBOR_TYPE_URI:
    case QCBOR_TYPE_BASE64URL:
    case QCBOR_TYPE_BASE64:
    case QCBOR_TYPE_REGEX:
    case QCBOR_TYPE_MIME:
    case QCBOR_TYPE_BINARY_MIME:
    case QCBOR_TYPE_DATE_STRING:
    case QCBOR_TYPE_DAYS_STRING:
        di.value.text = std::string_view(
            static_cast<const char*>(item.val.string.ptr),
            item.val.string.len);
        break;
    case QCBOR_TYPE_UUID:
        di.value.bytes = const_byte_span(
            static_cast<const uint8_t*>(item.val.string.ptr),
            item.val.string.len);
        break;
    case QCBOR_TYPE_DATE_EPOCH:
        di.value.int64_val = item.val.epochDate.nSeconds;
        break;
    case QCBOR_TYPE_DAYS_EPOCH:
        di.value.int64_val = item.val.epochDays;
        break;
    // Semantic tagged types — bignum (was missing from cache, caused
    // array-path getter failures since the cursor had moved past the item).
    case QCBOR_TYPE_POSBIGNUM:
    case QCBOR_TYPE_NEGBIGNUM:
        di.value.bytes = const_byte_span(
            static_cast<const uint8_t*>(item.val.bigNum.ptr),
            item.val.bigNum.len);
        break;
    // Decimal fraction / bigfloat with integer mantissa
    case QCBOR_TYPE_DECIMAL_FRACTION:
    case QCBOR_TYPE_BIGFLOAT:
        di.value.exp_mantissa = exp_and_mantissa(
            item.val.expAndMantissa.nExponent,
            item.val.expAndMantissa.Mantissa.nInt);
        break;
    // Decimal fraction / bigfloat with bignum mantissa
    case QCBOR_TYPE_DECIMAL_FRACTION_POS_BIGNUM:
    case QCBOR_TYPE_DECIMAL_FRACTION_NEG_BIGNUM:
    case QCBOR_TYPE_BIGFLOAT_POS_BIGNUM:
    case QCBOR_TYPE_BIGFLOAT_NEG_BIGNUM:
        di.value.exp_mantissa = exp_and_mantissa::from_bignum(
            item.val.expAndMantissa.nExponent,
            const_byte_span(
                static_cast<const uint8_t*>(item.val.expAndMantissa.Mantissa.bigNum.ptr),
                item.val.expAndMantissa.Mantissa.bigNum.len));
        break;
    default:
        // For other types, we just have the type info
        break;
    }

    return di;
}

inline std::error_code decoder::get_items_in_map(const std::vector<item_spec>& specs,
                                                   std::vector<decoded_item>& out) {
    return impl_get_items_in_map(specs, out);
}

inline std::error_code decoder::impl_get_items_in_map(
        const std::vector<item_spec>& specs,
        std::vector<decoded_item>& out) {

    if (specs.empty()) {
        out.clear();
        return {};
    }

    out.clear();
    out.reserve(specs.size());

    // Enter the map first (GetItemsInMap requires the context positioned at a map)
    QCBORDecode_EnterMap(&ctx_, nullptr);
    QCBORError enterErr = QCBORDecode_GetError(&ctx_);
    if (enterErr != QCBOR_SUCCESS)
        return make_error_code(static_cast<errc>(enterErr));

    // Build QCBORItem list
    std::vector<QCBORItem> items(specs.size() + 1); // +1 for sentinel
    for (size_t i = 0; i < specs.size(); ++i) {
        QCBORItem& qi = items[i];
        memset(&qi, 0, sizeof(qi));
        if (specs[i].is_int_label) {
            qi.uLabelType = QCBOR_TYPE_INT64;
            qi.label.int64 = specs[i].label_int;
        } else if (specs[i].label_str) {
            qi.uLabelType = QCBOR_TYPE_TEXT_STRING;
            qi.label.string.ptr  = specs[i].label_str;
            qi.label.string.len  = strlen(specs[i].label_str);
        }
        qi.uDataType = static_cast<uint8_t>(specs[i].type);
    }

    QCBORDecode_GetItemsInMap(&ctx_, items.data());
    QCBORError err = QCBORDecode_GetError(&ctx_);
    if (err != QCBOR_SUCCESS)
        return make_error_code(static_cast<errc>(err));

    for (size_t i = 0; i < specs.size(); ++i)
        out.push_back(convert_item(items[i]));

    // ExitMap after GetItemsInMap (it consumes map items but doesn't exit)
    ctx_.uLastError = QCBOR_SUCCESS;
    QCBORDecode_ExitMap(&ctx_);
    err = QCBORDecode_GetError(&ctx_);
    return err == QCBOR_SUCCESS ? std::error_code{}
                                 : make_error_code(static_cast<errc>(err));
}

// ============================================================================
// map_scope template members (defined after item_proxy for GCC completeness)
// ============================================================================

template<typename T>
inline T map_scope::get_or(std::string_view key, T def) const {
    auto& self = const_cast<map_scope&>(*this);
    return self[key].get_or(std::move(def));
}

template<typename T>
inline T map_scope::get_or(int64_t key, T def) const {
    auto& self = const_cast<map_scope&>(*this);
    return self[key].get_or(std::move(def));
}

template<typename T>
inline std::optional<T> map_scope::try_get(std::string_view key) const noexcept {
    auto& self = const_cast<map_scope&>(*this);
    return self[key].template try_get<T>();
}

template<typename T>
inline std::optional<T> map_scope::try_get(int64_t key) const noexcept {
    auto& self = const_cast<map_scope&>(*this);
    return self[key].template try_get<T>();
}

} // namespace qcborpp

#endif // QCBORPP_DECODER_HPP
