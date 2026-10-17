//=================================================================================================
/*
    Copyright (C) 2026 Splat authors.

    Author: Benjamin Arnaud. <http://bunjee.me> <bunjee@omega.gg>

    This file is part of Splat.

    - GNU Lesser General Public License Usage:
    This file may be used under the terms of the GNU Lesser General Public License version 3 as
    published by the Free Software Foundation and appearing in the LICENSE.md file included in the
    packaging of this file. Please review the following information to ensure the GNU Lesser
    General Public License requirements will be met: https://www.gnu.org/licenses/lgpl.html.

    - Private License Usage:
    Splat licensees holding valid private licenses may use this file in accordance with the
    private license agreement provided with the Software or, alternatively, in accordance with the
    terms contained in written agreement between you and Splat authors. For further information
    contact us at contact@omega.gg.

    Portions of Splat are ported from Spark (https://github.com/sparkjsdev/spark).
    Copyright (C) 2025 World Labs Technologies, Inc. MIT License, see LICENSE.spark.
*/
//=================================================================================================

// SPARK v2.3.1: the Rust libm crate (FreeBSD msun) and the f32 semantics of Rust, linked by
//               Spark's wasm decoders (rust/spark-lib).

#ifndef WSPLATMATH_H
#define WSPLATMATH_H

// Splat includes
#include <Splat.h>

// NOTE: Byte exact decoding needs Rust's float behavior, not the C runtime's: the MinGW expf and
//       logf differ from libm on many inputs, and its roundf(0.49999997f) returns 1. These are
//       exact ports, checked against Rust over all 2^32 inputs.
class SPLAT_EXPORT WSplatMath
{
public: // Static functions
    // NOTE: libm expf, logf, acosf and sinf.
    static float expf (float x);
    static float logf (float x);
    static float acosf(float x);
    static float sinf (float x);

    // NOTE: f32::round (half away from zero) and f32::clamp (NaN goes through).
    static float round(float value);
    static float clamp(float value, float min, float max);

    // NOTE: 'as u8' and 'as i8': saturating, NaN goes to 0.
    static quint8 castU8(float value);
    static qint8  castI8(float value);
};

#endif // WSPLATMATH_H
