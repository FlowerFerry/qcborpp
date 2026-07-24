/*
 * qcborpp/qcborpp.hpp -- Umbrella header for qcborpp
 *
 * Include this single header to use all qcborpp functionality.
 *
 * qcborpp is a header-only C++ wrapper for the QCBOR C library.
 * It provides RAII-managed encoding and decoding of CBOR data
 * with modern C++17 idioms.
 *
 * Copyright (c) 2024
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef QCBORPP_HPP
#define QCBORPP_HPP

#include "qcborpp/error.hpp"
#include "qcborpp/types.hpp"
#include "qcborpp/cbor_ref.hpp"
#include "qcborpp/encoder.hpp"
#include "qcborpp/dynamic_encoder.hpp"
#include "qcborpp/decoder.hpp"

/**
 * @mainpage
 *
 * # qcborpp -- C++ Header-Only Wrapper for QCBOR
 *
 * qcborpp provides a modern C++17 interface for CBOR encoding and decoding,
 * built on top of the QCBOR C library.
 *
 * ## Quick Start
 *
 * Encoding:
 * @code
 * #include <qcborpp/qcborpp.hpp>
 *
 * // Static-buffer encoder (fast, zero-copy)
 * uint8_t buf[512];
 * qcborpp::encoder enc(qcborpp::byte_span{buf, sizeof(buf)});
 *
 * {
 *     auto m = enc.map();
 *     m["name"]   = "example";
 *     m["count"]  = 42;
 *     m["active"] = true;
 *
 *     {
 *         auto arr = m["values"].array();
 *         arr << 1.0 << 2.0 << 3.0;
 *     }
 * }
 *
 * auto data = enc.finish();  // const_byte_span, aliases buf
 *
 * // or dynamic-buffer encoder
 * qcborpp::dynamic_encoder denc;
 * denc.map()["key"] = "value";
 * auto ddata = denc.finish();
 * @endcode
 *
 * Decoding:
 * @code
 * qcborpp::decoder dec{data};
 *
 * if (dec.is_map()) {
 *     auto m = dec.map();
 *     auto name   = std::string_view{m["name"]};
 *     auto count  = int64_t{m["count"]};
 *     auto active = bool{m["active"]};
 *
 *     auto arr = m["values"].as_array();
 *     while (!arr.done()) {
 *         double v = arr.next();
 *     }
 * }
 *
 * dec.finish();
 * @endcode
 */

#endif // QCBORPP_HPP
