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

#include "WSplatMath.h"

// C++ includes
#include <cfloat>
#include <cmath>
#include <cstring>

// NOTE: Spark's math is plain IEEE arithmetic without fused multiply add. We keep the compiler
//       from contracting a * b + c, otherwise the results are not bit exact on FMA targets.
#if defined(__clang__)
#pragma STDC FP_CONTRACT OFF
#elif defined(__GNUC__)
#pragma GCC optimize("fp-contract=off")
#elif defined(_MSC_VER)
#pragma fp_contract(off)
#endif

// NOTE: The libm ports build NaN and infinity on purpose, like (x - x) / 0.0f.
#ifdef _MSC_VER
#pragma warning(disable: 4723)
#endif

//-------------------------------------------------------------------------------------------------
// Static functions
//-------------------------------------------------------------------------------------------------

static quint32 floatToBits(float value)
{
    quint32 bits;

    std::memcpy(&bits, &value, sizeof(float));

    return bits;
}

static float bitsToFloat(quint32 bits)
{
    float value;

    std::memcpy(&value, &bits, sizeof(float));

    return value;
}

// NOTE: Rust libm crate helpers, origin FreeBSD msun.

// SPARK: libm scalbnf (generic::scalbn for f32)
static float libmScalbnf(float x, int n)
{
    const float fExpMax     = bitsToFloat(0x7f000000); // 2^127
    const float fExpMin     = bitsToFloat(0x00800000); // 2^-126
    const float fPowSubnorm = bitsToFloat(0x4b800000); // 2^24

    if (n > 127)
    {
        x *= fExpMax;
        n -= 127;

        if (n > 127)
        {
            x *= fExpMax;
            n -= 127;

            if (n > 127) n = 127;
        }
    }
    else if (n < -126)
    {
        const float mul = fExpMin * fPowSubnorm;

        const int add = 126 - 24;

        x *= mul;
        n += add;

        if (n < -126)
        {
            x *= mul;
            n += add;

            if (n < -126) n = -126;
        }
    }

    return x * bitsToFloat((quint32) (127 + n) << 23);
}

// SPARK: libm k_sinf
static float libmKSinf(double x)
{
    const double s1 = -0.166666666416265235595;
    const double s2 =  0.0083333293858894631756;
    const double s3 = -0.000198393348360966317347;
    const double s4 =  0.0000027183114939898219064;

    double z = x * x;
    double w = z * z;
    double r = s3 + z * s4;
    double s = z * x;

    return (float) ((x + s * (s1 + z * s2)) + s * w * r);
}

// SPARK: libm k_cosf
static float libmKCosf(double x)
{
    const double c0 = -0.499999997251031003120;
    const double c1 =  0.0416666233237390631894;
    const double c2 = -0.00138867637746099294692;
    const double c3 =  0.0000243904487962774090654;

    double z = x * x;
    double w = z * z;
    double r = c2 + z * c3;

    return (float) (((1.0 + z * c0) + w * c1) + (w * z) * r);
}

//-------------------------------------------------------------------------------------------------
// Rust f32
//-------------------------------------------------------------------------------------------------

// NOTE: Rust f32::clamp: NaN goes through untouched.
/* static */ float WSplatMath::clamp(float value, float min, float max)
{
    if (value < min) value = min;
    if (value > max) value = max;

    return value;
}

// NOTE: Rust f32::round, half away from zero. We do not use std::round because MinGW's roundf
//       returns 1 for 0.49999997.
/* static */ float WSplatMath::round(float value)
{
    float result = std::trunc(value);

    // NOTE: The fractional part is exact, NaN and infinity fall through.
    if (std::fabs(value - result) >= 0.5f) result += std::copysign(1.0f, value);

    return result;
}

// NOTE: Rust 'as u8' and 'as i8' saturate, and map NaN to 0.
/* static */ quint8 WSplatMath::castU8(float value)
{
    if (value != value) return 0;

    if (value <= 0.0f)   return 0;
    if (value >= 255.0f) return 255;

    return (quint8) value;
}

/* static */ qint8 WSplatMath::castI8(float value)
{
    if (value != value) return 0;

    if (value <= -128.0f) return -128;
    if (value >=  127.0f) return 127;

    return (qint8) value;
}

//-------------------------------------------------------------------------------------------------
// libm
//-------------------------------------------------------------------------------------------------

// SPARK: libm expf
/* static */ float WSplatMath::expf(float x)
{
    const float half[2] = { 0.5f, -0.5f };

    const float ln2Hi  = 6.9314575195e-01f;
    const float ln2Lo  = 1.4286067653e-06f;
    const float invLn2 = 1.4426950216e+00f;

    const float p1 =  1.6666625440e-1f;
    const float p2 = -2.7667332906e-3f;

    const float x1p127 = bitsToFloat(0x7f000000);

    quint32 hx = floatToBits(x);

    int sign = (int) (hx >> 31);

    hx &= 0x7fffffff;

    // NOTE: |x| >= 87.33655 or NaN.
    if (hx >= 0x42aeac50)
    {
        // NOTE: NaN.
        if (hx > 0x7f800000) return x;

        // NOTE: Overflow, x >= 88.722839.
        if (hx >= 0x42b17218
            &&
            sign == 0)
        {
            return x * x1p127;
        }

        // NOTE: Underflow, x <= -103.972084.
        if (sign
            &&
            hx >= 0x42cff1b5)
        {
            return 0.0f;
        }
    }

    int   k;
    float hi;
    float lo;

    // NOTE: Argument reduction.
    if (hx > 0x3eb17218)
    {
        if (hx > 0x3f851592)
        {
            k = (int) (invLn2 * x + half[sign]);
        }
        else k = 1 - sign - sign;

        float kf = (float) k;

        hi = x - kf * ln2Hi;
        lo = kf * ln2Lo;

        x = hi - lo;
    }
    else if (hx > 0x39000000)
    {
        k = 0;

        hi = x;
        lo = 0.0f;
    }
    else return 1.0f + x;

    float xx = x * x;

    float c = x - xx * (p1 + xx * p2);

    float y = 1.0f + (x * c / (2.0f - c) - lo + hi);

    if (k == 0) return y;

    return libmScalbnf(y, k);
}

// SPARK: libm logf
/* static */ float WSplatMath::logf(float x)
{
    const float ln2Hi = 6.9313812256e-01f;
    const float ln2Lo = 9.0580006145e-06f;

    const float lg1 = 0.66666662693f;
    const float lg2 = 0.40000972152f;
    const float lg3 = 0.28498786688f;
    const float lg4 = 0.24279078841f;

    const float x1p25 = bitsToFloat(0x4c000000);

    quint32 ix = floatToBits(x);

    int k = 0;

    if (ix < 0x00800000 || (ix >> 31) != 0)
    {
        // NOTE: log(+-0) = -inf.
        if ((ix << 1) == 0) return -1.0f / (x * x);

        // NOTE: log(negative) = NaN.
        if ((ix >> 31) != 0) return (x - x) / 0.0f;

        // NOTE: Subnormal, scale up x.
        k -= 25;

        x *= x1p25;

        ix = floatToBits(x);
    }
    else if (ix >= 0x7f800000)
    {
        return x;
    }
    else if (ix == 0x3f800000)
    {
        return 0.0f;
    }

    // NOTE: Reduce x into [sqrt(2) / 2, sqrt(2)].
    ix += 0x3f800000 - 0x3f3504f3;

    k += (int) (ix >> 23) - 0x7f;

    ix = (ix & 0x007fffff) + 0x3f3504f3;

    x = bitsToFloat(ix);

    float f = x - 1.0f;
    float s = f / (2.0f + f);
    float z = s * s;
    float w = z * z;

    float t1 = w * (lg2 + w * lg4);
    float t2 = z * (lg1 + w * lg3);

    float r = t2 + t1;

    float hfsq = 0.5f * f * f;

    float dk = (float) k;

    return s * (hfsq + r) + dk * ln2Lo - hfsq + f + dk * ln2Hi;
}

// SPARK: libm acosf
/* static */ float WSplatMath::acosf(float x)
{
    const float pio2Hi = 1.5707962513e+00f;
    const float pio2Lo = 7.5497894159e-08f;

    const float pS0 =  1.6666586697e-01f;
    const float pS1 = -4.2743422091e-02f;
    const float pS2 = -8.6563630030e-03f;
    const float qS1 = -7.0662963390e-01f;

    const float x1p120 = bitsToFloat(0x03800000); // 2^-120

    quint32 hx = floatToBits(x);
    quint32 ix = hx & 0x7fffffff;

    // NOTE: |x| >= 1 or NaN.
    if (ix >= 0x3f800000)
    {
        if (ix == 0x3f800000)
        {
            if ((hx >> 31) != 0) return 2.0f * pio2Hi + x1p120;

            return 0.0f;
        }

        return 0.0f / (x - x);
    }

    float z;
    float p;
    float q;
    float s;
    float w;

    // NOTE: |x| < 0.5.
    if (ix < 0x3f000000)
    {
        // NOTE: |x| < 2^-26.
        if (ix <= 0x32800000) return pio2Hi + x1p120;

        z = x * x;

        p = z * (pS0 + z * (pS1 + z * pS2));
        q = 1.0f + z * qS1;

        return pio2Hi - (x - (pio2Lo - x * (p / q)));
    }

    // NOTE: x < -0.5.
    if ((hx >> 31) != 0)
    {
        z = (1.0f + x) * 0.5f;
        s = std::sqrt(z);

        p = z * (pS0 + z * (pS1 + z * pS2));
        q = 1.0f + z * qS1;

        w = (p / q) * s - pio2Lo;

        return 2.0f * (pio2Hi - (s + w));
    }

    // NOTE: x > 0.5.
    z = (1.0f - x) * 0.5f;
    s = std::sqrt(z);

    float df = bitsToFloat(floatToBits(s) & 0xfffff000);

    float c = (z - df * df) / (s + df);

    p = z * (pS0 + z * (pS1 + z * pS2));
    q = 1.0f + z * qS1;

    w = (p / q) * s + c;

    return 2.0f * (df + w);
}

// SPARK: libm sinf
// NOTE: The encoder only calls this on [0, pi / 2]. Arguments beyond 2^28 * pi / 2 need
//       rem_pio2_large, which is not ported: std::sin is used there instead.
/* static */ float WSplatMath::sinf(float x)
{
    const double pio2 = 1.57079632679489661923; // std::f64::consts::FRAC_PI_2

    const double s1Pio2 = 1.0 * pio2;
    const double s2Pio2 = 2.0 * pio2;
    const double s3Pio2 = 3.0 * pio2;
    const double s4Pio2 = 4.0 * pio2;

    double x64 = x;

    quint32 ix = floatToBits(x);

    bool sign = ((ix >> 31) != 0);

    ix &= 0x7fffffff;

    // NOTE: |x| ~<= pi / 4.
    if (ix <= 0x3f490fda)
    {
        // NOTE: |x| < 2^-12.
        if (ix < 0x39800000) return x;

        return libmKSinf(x64);
    }

    // NOTE: |x| ~<= 5 * pi / 4.
    if (ix <= 0x407b53d1)
    {
        // NOTE: |x| ~<= 3 * pi / 4.
        if (ix <= 0x4016cbe3)
        {
            if (sign)
            {
                return -libmKCosf(x64 + s1Pio2);
            }
            else return libmKCosf(x64 - s1Pio2);
        }

        if (sign)
        {
            return libmKSinf(-(x64 + s2Pio2));
        }
        else return libmKSinf(-(x64 - s2Pio2));
    }

    // NOTE: |x| ~<= 9 * pi / 4.
    if (ix <= 0x40e231d5)
    {
        // NOTE: |x| ~<= 7 * pi / 4.
        if (ix <= 0x40afeddf)
        {
            if (sign)
            {
                return libmKCosf(x64 + s3Pio2);
            }
            else return -libmKCosf(x64 - s3Pio2);
        }

        if (sign)
        {
            return libmKSinf(x64 + s4Pio2);
        }
        else return libmKSinf(x64 - s4Pio2);
    }

    // NOTE: sin(Inf or NaN) is NaN.
    if (ix >= 0x7f800000) return x - x;

    if (ix >= 0x4dc90fdb) return (float) std::sin(x64);

    // SPARK: libm rem_pio2f (medium size)
    const double toInt   = 1.5 / DBL_EPSILON;
    const double invPio2 = 6.36619772367581382433e-01;
    const double pio2_1  = 1.57079631090164184570e+00;
    const double pio2_1t = 1.58932547735281966916e-08;

    double tmp = x64 * invPio2 + toInt;

    double fn = tmp - toInt;

    int n = (int) fn;

    double y = x64 - fn * pio2_1 - fn * pio2_1t;

    switch (n & 3)
    {
        case 0:  return  libmKSinf(y);
        case 1:  return  libmKCosf(y);
        case 2:  return  libmKSinf(-y);
        default: return -libmKCosf(y);
    }
}
