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

// SPARK v2.3.1: rust/spark-rs/src/packed_splats.rs (PackedSplatsData)
//               rust/spark-lib/src/splat_encode.rs
//               src/utils.ts (getTextureSize)

#include "WPackedSplats.h"

// Splat includes
#include <WSplatMath.h>

// C++ includes
#include <cfloat>
#include <climits>
#include <cmath>
#include <cstring>
#include <new>

// NOTE: Spark's math is plain IEEE arithmetic without fused multiply add. We keep the compiler
//       from contracting a * b + c, otherwise the encoding is not bit exact on FMA targets.
#if defined(__clang__)
#pragma STDC FP_CONTRACT OFF
#elif defined(__GNUC__)
#pragma GCC optimize("fp-contract=off")
#elif defined(_MSC_VER)
#pragma fp_contract(off)
#endif

//-------------------------------------------------------------------------------------------------
// Static variables
//-------------------------------------------------------------------------------------------------

// NOTE: std::f32::consts::PI.
static const float PACKEDSPLATS_PI = 3.14159265358979323846f;

static const int PACKEDSPLATS_LAYER_SIZE = SPLAT_TEX_WIDTH * SPLAT_TEX_HEIGHT;

//-------------------------------------------------------------------------------------------------
// Static functions
//-------------------------------------------------------------------------------------------------

static quint32 floatToBits(float value)
{
    quint32 bits;

    memcpy(&bits, &value, sizeof(bits));

    return bits;
}

static float bitsToFloat(quint32 bits)
{
    float value;

    memcpy(&value, &bits, sizeof(value));

    return value;
}


//-------------------------------------------------------------------------------------------------

// SPARK: get_splat_tex_size
static WSplatTextureSize getSplatTexSize(int numSplats)
{
    WSplatTextureSize size;

    size.width = SPLAT_TEX_WIDTH;

    int height = (int) (((qint64) numSplats + SPLAT_TEX_WIDTH - 1) / SPLAT_TEX_WIDTH);

    if (height < SPLAT_TEX_MIN_HEIGHT) height = SPLAT_TEX_MIN_HEIGHT;
    if (height > SPLAT_TEX_HEIGHT)     height = SPLAT_TEX_HEIGHT;

    size.height = height;

    int depth = (int) (((qint64) numSplats + PACKEDSPLATS_LAYER_SIZE - 1)
                       / PACKEDSPLATS_LAYER_SIZE);

    if (depth < 1) depth = 1;

    size.depth = depth;

    size.maxSplats = size.width * size.height * size.depth;

    return size;
}

//-------------------------------------------------------------------------------------------------

// SPARK: float_to_u8
static quint8 floatToU8(float value, float min, float max)
{
    float scaled = WSplatMath::clamp((value - min) / (max - min) * 255.0f, 0.0f, 255.0f);

    return WSplatMath::castU8(WSplatMath::round(scaled));
}

// SPARK: encode_quat_oct888
static void encodeQuatOct888(const float * quatXyzw, quint8 * out)
{
    float quat[4];

    if (quatXyzw[3] < 0.0f)
    {
        for (int i = 0; i < 4; i++)
        {
            quat[i] = -quatXyzw[i];
        }
    }
    else memcpy(quat, quatXyzw, sizeof(quat));

    float theta = 2.0f * WSplatMath::acosf(WSplatMath::clamp(quat[3], 0.0f, 1.0f));

    float s = WSplatMath::sinf(theta * 0.5f);

    float axis[3];

    if (std::fabs(s) < 1e-6f)
    {
        axis[0] = 1.0f;
        axis[1] = 0.0f;
        axis[2] = 0.0f;
    }
    else
    {
        for (int i = 0; i < 3; i++)
        {
            axis[i] = quat[i] / s;
        }
    }

    float sum = std::fabs(axis[0]) + std::fabs(axis[1]) + std::fabs(axis[2]);

    float p0 = axis[0] / sum;
    float p1 = axis[1] / sum;

    if (axis[2] < 0.0f)
    {
        float x = (1.0f - std::fabs(p1)) * ((p0 >= 0.0f) ? 1.0f : -1.0f);
        float y = (1.0f - std::fabs(p0)) * ((p1 >= 0.0f) ? 1.0f : -1.0f);

        p0 = x;
        p1 = y;
    }

    out[0] = floatToU8(p0, -1.0f, 1.0f);
    out[1] = floatToU8(p1, -1.0f, 1.0f);

    out[2] = floatToU8(theta, 0.0f, PACKEDSPLATS_PI);
}

// SPARK: encode_scale8
static quint8 encodeScale8(float scale, float lnScaleMin, float lnScaleMax)
{
    if (scale <= 0.0f) return 0;

    float n = (WSplatMath::logf(scale) - lnScaleMin) / (lnScaleMax - lnScaleMin);

    return 1 + WSplatMath::castU8(WSplatMath::round(WSplatMath::clamp(n, 0.0f, 1.0f) * 254.0f));
}

//-------------------------------------------------------------------------------------------------

// SPARK: encode_sh1_internal
static void encodeSh1Internal(quint32 * words, const float * sh1, float sh1Scale)
{
    words[0] = 0;
    words[1] = 0;

    for (int i = 0; i < 9; i++)
    {
        float scaled = WSplatMath::clamp(sh1[i] * sh1Scale, -63.0f, 63.0f);

        quint32 value = (quint32) (WSplatMath::castI8(WSplatMath::round(scaled)) & 0x7f);

        int bitStart     = i * 7;
        int wordStart    = bitStart / 32;
        int wordBitStart = wordStart * 32;
        int bitOffset    = bitStart - wordBitStart;

        words[wordStart] |= value << bitOffset;

        if ((bitStart + 7) > (wordBitStart + 32))
        {
            words[wordStart + 1] |= value >> (32 - bitOffset);
        }
    }
}

// SPARK: encode_sh2_internal
static void encodeSh2Internal(quint32 * words, const float * sh2, float sh2Scale)
{
    quint32 bytes[15];

    for (int i = 0; i < 15; i++)
    {
        float scaled = WSplatMath::clamp(sh2[i] * sh2Scale, -127.0f, 127.0f);

        bytes[i] = (quint8) WSplatMath::castI8(WSplatMath::round(scaled));
    }

    words[0] = bytes[0]  | (bytes[1]  << 8) | (bytes[2]  << 16) | (bytes[3]  << 24);
    words[1] = bytes[4]  | (bytes[5]  << 8) | (bytes[6]  << 16) | (bytes[7]  << 24);
    words[2] = bytes[8]  | (bytes[9]  << 8) | (bytes[10] << 16) | (bytes[11] << 24);
    words[3] = bytes[12] | (bytes[13] << 8) | (bytes[14] << 16);
}

// SPARK: encode_sh3_internal
static void encodeSh3Internal(quint32 * words, const float * sh3, float sh3Scale)
{
    words[0] = 0;
    words[1] = 0;
    words[2] = 0;
    words[3] = 0;

    for (int i = 0; i < 21; i++)
    {
        float scaled = WSplatMath::clamp(sh3[i] * sh3Scale, -31.0f, 31.0f);

        quint32 value = (quint32) (WSplatMath::castI8(WSplatMath::round(scaled)) & 0x3f);

        int bitStart     = i * 6;
        int wordStart    = bitStart / 32;
        int wordBitStart = wordStart * 32;
        int bitOffset    = bitStart - wordBitStart;

        words[wordStart] |= value << bitOffset;

        if ((bitStart + 6) > (wordBitStart + 32))
        {
            words[wordStart + 1] |= value >> (32 - bitOffset);
        }
    }
}

//-------------------------------------------------------------------------------------------------
// Ctor / dtor
//-------------------------------------------------------------------------------------------------

WPackedSplats::WPackedSplats()
{
    numSplats = 0;
    maxSplats = 0;

    maxShDegree = 0;
}

//-------------------------------------------------------------------------------------------------
// Interface
//-------------------------------------------------------------------------------------------------

// SPARK: PackedSplats.getNumSh
int WPackedSplats::numSh() const
{
    if (sh1.isEmpty()) return 0;
    if (sh2.isEmpty()) return 1;
    if (sh3.isEmpty()) return 2;

    return 3;
}

//-------------------------------------------------------------------------------------------------
// Static functions
//-------------------------------------------------------------------------------------------------

// SPARK: getTextureSize
/* static */ WSplatTextureSize WPackedSplats::getTextureSize(int numSplats)
{
    WSplatTextureSize size;

    size.width = SPLAT_TEX_WIDTH;

    int height = (int) (((qint64) numSplats + SPLAT_TEX_WIDTH - 1) / SPLAT_TEX_WIDTH);

    if (height > SPLAT_TEX_HEIGHT)     height = SPLAT_TEX_HEIGHT;
    if (height < SPLAT_TEX_MIN_HEIGHT) height = SPLAT_TEX_MIN_HEIGHT;

    size.height = height;

    int layerSize = size.width * size.height;

    size.depth = (int) (((qint64) numSplats + layerSize - 1) / layerSize);

    size.maxSplats = size.width * size.height * size.depth;

    return size;
}

//-------------------------------------------------------------------------------------------------

// SPARK: encode_packed_splat
/* static */ void WPackedSplats::encodePackedSplat(quint32 * packed, const float * center,
                                                   float opacity,
                                                   const float * rgb, const float * scale,
                                                   const float * quat,
                                                   const WSplatEncoding & encoding)
{
    float rgbMin = encoding.rgbMin;
    float rgbMax = encoding.rgbMax;

    quint32 r = floatToU8(rgb[0], rgbMin, rgbMax);
    quint32 g = floatToU8(rgb[1], rgbMin, rgbMax);
    quint32 b = floatToU8(rgb[2], rgbMin, rgbMax);

    quint32 a = floatToU8(opacity, 0.0f, encoding.lodOpacity ? 2.0f : 1.0f);

    quint32 x = floatToHalf(center[0]);
    quint32 y = floatToHalf(center[1]);
    quint32 z = floatToHalf(center[2]);

    quint8 uQuat[3];

    encodeQuatOct888(quat, uQuat);

    float lnScaleMin = encoding.lnScaleMin;
    float lnScaleMax = encoding.lnScaleMax;

    quint32 scaleX = encodeScale8(scale[0], lnScaleMin, lnScaleMax);
    quint32 scaleY = encodeScale8(scale[1], lnScaleMin, lnScaleMax);
    quint32 scaleZ = encodeScale8(scale[2], lnScaleMin, lnScaleMax);

    packed[0] = r | (g << 8) | (b << 16) | (a << 24);
    packed[1] = x | (y << 16);
    packed[2] = z | ((quint32) uQuat[0] << 16) | ((quint32) uQuat[1] << 24);
    packed[3] = scaleX | (scaleY << 8) | (scaleZ << 16) | ((quint32) uQuat[2] << 24);
}

// SPARK: encode_packed_splat_center
/* static */ void WPackedSplats::encodePackedSplatCenter(quint32 * packed, const float * center)
{
    quint32 x = floatToHalf(center[0]);
    quint32 y = floatToHalf(center[1]);
    quint32 z = floatToHalf(center[2]);

    packed[1] = x | (y << 16);
    packed[2] = (packed[2] & 0xffff0000) | z;
}

// SPARK: encode_packed_splat_opacity
/* static */ void WPackedSplats::encodePackedSplatOpacity(quint32 * packed, float opacity,
                                                          const WSplatEncoding & encoding)
{
    quint32 a = floatToU8(opacity, 0.0f, encoding.lodOpacity ? 2.0f : 1.0f);

    packed[0] = (packed[0] & 0x00ffffff) | (a << 24);
}

// SPARK: encode_packed_splat_rgb
/* static */ void WPackedSplats::encodePackedSplatRgb(quint32 * packed, const float * rgb,
                                                      const WSplatEncoding & encoding)
{
    float rgbMin = encoding.rgbMin;
    float rgbMax = encoding.rgbMax;

    quint32 r = floatToU8(rgb[0], rgbMin, rgbMax);
    quint32 g = floatToU8(rgb[1], rgbMin, rgbMax);
    quint32 b = floatToU8(rgb[2], rgbMin, rgbMax);

    packed[0] = (packed[0] & 0xff000000) | r | (g << 8) | (b << 16);
}

// SPARK: encode_packed_splat_rgba
/* static */ void WPackedSplats::encodePackedSplatRgba(quint32 * packed, const float * rgba,
                                                       const WSplatEncoding & encoding)
{
    float rgbMin = encoding.rgbMin;
    float rgbMax = encoding.rgbMax;

    quint32 r = floatToU8(rgba[0], rgbMin, rgbMax);
    quint32 g = floatToU8(rgba[1], rgbMin, rgbMax);
    quint32 b = floatToU8(rgba[2], rgbMin, rgbMax);

    quint32 a = floatToU8(rgba[3], 0.0f, encoding.lodOpacity ? 2.0f : 1.0f);

    packed[0] = r | (g << 8) | (b << 16) | (a << 24);
}

// SPARK: encode_packed_splat_scale
/* static */ void WPackedSplats::encodePackedSplatScale(quint32 * packed, const float * scale,
                                                        const WSplatEncoding & encoding)
{
    float lnScaleMin = encoding.lnScaleMin;
    float lnScaleMax = encoding.lnScaleMax;

    quint32 x = encodeScale8(scale[0], lnScaleMin, lnScaleMax);
    quint32 y = encodeScale8(scale[1], lnScaleMin, lnScaleMax);
    quint32 z = encodeScale8(scale[2], lnScaleMin, lnScaleMax);

    packed[3] = (packed[3] & 0xff000000) | x | (y << 8) | (z << 16);
}

// SPARK: encode_packed_splat_quat
/* static */ void WPackedSplats::encodePackedSplatQuat(quint32 * packed, const float * quat)
{
    quint8 uQuat[3];

    encodeQuatOct888(quat, uQuat);

    packed[2] = (packed[2] & 0x0000ffff) | ((quint32) uQuat[0] << 16)
                                         | ((quint32) uQuat[1] << 24);

    packed[3] = (packed[3] & 0x00ffffff) | ((quint32) uQuat[2] << 24);
}

//-------------------------------------------------------------------------------------------------

// SPARK: encode_sh1_array
/* static */ void WPackedSplats::encodeSh1Array(quint32 * out, const float * sh1, int count,
                                                float sh1Max)
{
    float sh1Scale = 63.0f / sh1Max;

    for (int i = 0; i < count; i++)
    {
        encodeSh1Internal(out + i * 2, sh1 + i * 9, sh1Scale);
    }
}

// SPARK: encode_sh2_array
/* static */ void WPackedSplats::encodeSh2Array(quint32 * out, const float * sh2, int count,
                                                float sh2Max)
{
    float sh2Scale = 127.0f / sh2Max;

    for (int i = 0; i < count; i++)
    {
        encodeSh2Internal(out + i * 4, sh2 + i * 15, sh2Scale);
    }
}

// SPARK: encode_sh3_array
/* static */ void WPackedSplats::encodeSh3Array(quint32 * out, const float * sh3, int count,
                                                float sh3Max)
{
    float sh3Scale = 31.0f / sh3Max;

    for (int i = 0; i < count; i++)
    {
        encodeSh3Internal(out + i * 4, sh3 + i * 21, sh3Scale);
    }
}

//-------------------------------------------------------------------------------------------------

// SPARK: half::f16::from_f32 (f32_to_f16_fallback, round to nearest even)
/* static */ quint16 WPackedSplats::floatToHalf(float value)
{
    quint32 x = floatToBits(value);

    quint32 sign = x & 0x80000000;
    quint32 exp  = x & 0x7f800000;
    quint32 man  = x & 0x007fffff;

    // NOTE: Infinity or NaN, NaN keeps its shifted payload with the quiet bit set.
    if (exp == 0x7f800000)
    {
        quint32 nanBit = (man == 0) ? 0 : 0x0200;

        return (quint16) ((sign >> 16) | 0x7c00 | nanBit | (man >> 13));
    }

    quint32 halfSign = sign >> 16;

    int unbiasedExp = (int) (exp >> 23) - 127;

    int halfExp = unbiasedExp + 15;

    // NOTE: Exponent overflow, this returns infinity.
    if (halfExp >= 0x1f) return (quint16) (halfSign | 0x7c00);

    // NOTE: Underflow, this returns a subnormal or a signed zero.
    if (halfExp <= 0)
    {
        if (14 - halfExp > 24) return (quint16) halfSign;

        man |= 0x00800000;

        quint32 halfMan = man >> (14 - halfExp);

        quint32 roundBit = 1u << (13 - halfExp);

        if ((man & roundBit) != 0
            &&
            (man & (3 * roundBit - 1)) != 0)
        {
            halfMan++;
        }

        return (quint16) (halfSign | halfMan);
    }

    quint32 halfExpBits = (quint32) halfExp << 10;

    quint32 halfMan = man >> 13;

    quint32 roundBit = 0x00001000;

    if ((man & roundBit) != 0
        &&
        (man & (3 * roundBit - 1)) != 0)
    {
        return (quint16) ((halfSign | halfExpBits | halfMan) + 1);
    }
    else return (quint16) (halfSign | halfExpBits | halfMan);
}

// SPARK: half::f16::to_f32 (f16_to_f32_fallback)
/* static */ float WPackedSplats::halfToFloat(quint16 value)
{
    // NOTE: Signed zero.
    if ((value & 0x7fff) == 0) return bitsToFloat((quint32) value << 16);

    quint32 halfSign = value & 0x8000;
    quint32 halfExp  = value & 0x7c00;
    quint32 halfMan  = value & 0x03ff;

    // NOTE: Infinity or NaN, NaN gets the quiet bit.
    if (halfExp == 0x7c00)
    {
        if (halfMan == 0)
        {
            return bitsToFloat((halfSign << 16) | 0x7f800000);
        }
        else return bitsToFloat((halfSign << 16) | 0x7fc00000 | (halfMan << 13));
    }

    quint32 sign = halfSign << 16;

    int unbiasedExp = ((int) halfExp >> 10) - 15;

    // NOTE: Subnormal, normalized by adjusting the exponent.
    if (halfExp == 0)
    {
        int leadingZeros = 16;

        while (leadingZeros > 0 && (halfMan >> (16 - leadingZeros)) != 0)
        {
            leadingZeros--;
        }

        int e = leadingZeros - 6;

        quint32 exp = (quint32) (127 - 15 - e) << 23;
        quint32 man = (halfMan << (14 + e)) & 0x7fffff;

        return bitsToFloat(sign | exp | man);
    }

    quint32 exp = (quint32) (unbiasedExp + 127) << 23;
    quint32 man = (halfMan & 0x03ff) << 13;

    return bitsToFloat(sign | exp | man);
}

//-------------------------------------------------------------------------------------------------
// WSplatReceiver implementation
//-------------------------------------------------------------------------------------------------

/* virtual */ void WPackedSplats::finish()
{
    centers.resize(qsizetype(numSplats) * 3);

    const quint32 * data = packed.constData();

    float * center = centers.data();

    for (int i = 0; i < numSplats; i++)
    {
        center[0] = halfToFloat(data[1] & 0xffff);
        center[1] = halfToFloat(data[1] >> 16);
        center[2] = halfToFloat(data[2] & 0xffff);

        data   += 4;
        center += 3;
    }
}

// SPARK: PackedSplatsData::init_splats
/* virtual */ bool WPackedSplats::initSplats(const WSplatInit & init)
{
    if (init.numSplats < 0
        ||
        init.numSplats > INT_MAX - PACKEDSPLATS_LAYER_SIZE)
    {
        return false;
    }

    WSplatTextureSize size = getSplatTexSize(init.numSplats);

    try
    {
        packed = QVector<quint32>((qsizetype) size.maxSplats * 4, 0);

        if (init.maxShDegree < 1)
        {
            sh1 = QVector<quint32>();
        }
        else sh1 = QVector<quint32>((qsizetype) size.maxSplats * 2, 0);

        if (init.maxShDegree < 2)
        {
            sh2 = QVector<quint32>();
        }
        else sh2 = QVector<quint32>((qsizetype) size.maxSplats * 4, 0);

        if (init.maxShDegree < 3)
        {
            sh3 = QVector<quint32>();
        }
        else sh3 = QVector<quint32>((qsizetype) size.maxSplats * 4, 0);
    }
    catch (const std::bad_alloc &)
    {
        packed = QVector<quint32>();

        sh1 = QVector<quint32>();
        sh2 = QVector<quint32>();
        sh3 = QVector<quint32>();

        numSplats = 0;
        maxSplats = 0;

        maxShDegree = 0;

        textureSize = WSplatTextureSize();

        return false;
    }

    maxSplats = size.maxSplats;
    numSplats = init.numSplats;

    maxShDegree = init.maxShDegree;

    encoding.lodOpacity = init.lodTree;

    // NOTE: This matches the arrays, even for zero splats where getTextureSize alone would
    //       return a zero depth.
    textureSize = getTextureSize(maxSplats);

    return true;
}

// SPARK: PackedSplatsData::set_encoding
// NOTE: SetSplatEncoding fields are optional upstream, ours are all present.
/* virtual */ void WPackedSplats::setEncoding(const WSplatEncoding & encoding)
{
    this->encoding = encoding;
}

// SPARK: PackedSplatsData::set_batch
/* virtual */ void WPackedSplats::setBatch(int base, int count, const WSplatProps & batch)
{
    if (batch.center
        &&
        batch.opacity
        &&
        batch.rgb
        &&
        batch.scale
        &&
        batch.quat)
    {
        Q_ASSERT(base >= 0 && count >= 0 && base + count <= maxSplats);

        quint32 * buffer = packed.data() + (qsizetype) base * 4;

        for (int i = 0; i < count; i++)
        {
            int i3 = i * 3;
            int i4 = i * 4;

            encodePackedSplat(buffer + i4, batch.center + i3, batch.opacity[i],
                              batch.rgb + i3, batch.scale + i3, batch.quat + i4, encoding);
        }
    }
    else
    {
        if (batch.center)  setCenter (base, count, batch.center);
        if (batch.opacity) setOpacity(base, count, batch.opacity);
        if (batch.rgb)     setRgb    (base, count, batch.rgb);
        if (batch.scale)   setScale  (base, count, batch.scale);
        if (batch.quat)    setQuat   (base, count, batch.quat);
    }

    // SPARK: PackedSplatsData::set_sh
    if (batch.sh1) setSh1(base, count, batch.sh1);
    if (batch.sh2) setSh2(base, count, batch.sh2);
    if (batch.sh3) setSh3(base, count, batch.sh3);
}

// SPARK: PackedSplatsData::set_center
/* virtual */ void WPackedSplats::setCenter(int base, int count, const float * center)
{
    Q_ASSERT(base >= 0 && count >= 0 && base + count <= maxSplats);

    quint32 * buffer = packed.data() + (qsizetype) base * 4;

    for (int i = 0; i < count; i++)
    {
        encodePackedSplatCenter(buffer + i * 4, center + i * 3);
    }
}

// SPARK: PackedSplatsData::set_opacity
/* virtual */ void WPackedSplats::setOpacity(int base, int count, const float * opacity)
{
    Q_ASSERT(base >= 0 && count >= 0 && base + count <= maxSplats);

    quint32 * buffer = packed.data() + (qsizetype) base * 4;

    for (int i = 0; i < count; i++)
    {
        encodePackedSplatOpacity(buffer + i * 4, opacity[i], encoding);
    }
}

// SPARK: PackedSplatsData::set_rgb
/* virtual */ void WPackedSplats::setRgb(int base, int count, const float * rgb)
{
    Q_ASSERT(base >= 0 && count >= 0 && base + count <= maxSplats);

    quint32 * buffer = packed.data() + (qsizetype) base * 4;

    for (int i = 0; i < count; i++)
    {
        encodePackedSplatRgb(buffer + i * 4, rgb + i * 3, encoding);
    }
}

// SPARK: PackedSplatsData::set_rgba
/* virtual */ void WPackedSplats::setRgba(int base, int count, const float * rgba)
{
    Q_ASSERT(base >= 0 && count >= 0 && base + count <= maxSplats);

    quint32 * buffer = packed.data() + (qsizetype) base * 4;

    for (int i = 0; i < count; i++)
    {
        encodePackedSplatRgba(buffer + i * 4, rgba + i * 4, encoding);
    }
}

// SPARK: PackedSplatsData::set_scale
/* virtual */ void WPackedSplats::setScale(int base, int count, const float * scale)
{
    Q_ASSERT(base >= 0 && count >= 0 && base + count <= maxSplats);

    quint32 * buffer = packed.data() + (qsizetype) base * 4;

    for (int i = 0; i < count; i++)
    {
        encodePackedSplatScale(buffer + i * 4, scale + i * 3, encoding);
    }
}

// SPARK: PackedSplatsData::set_quat
/* virtual */ void WPackedSplats::setQuat(int base, int count, const float * quat)
{
    Q_ASSERT(base >= 0 && count >= 0 && base + count <= maxSplats);

    quint32 * buffer = packed.data() + (qsizetype) base * 4;

    for (int i = 0; i < count; i++)
    {
        encodePackedSplatQuat(buffer + i * 4, quat + i * 4);
    }
}

//-------------------------------------------------------------------------------------------------

// SPARK: PackedSplatsData::set_sh1
/* virtual */ void WPackedSplats::setSh1(int base, int count, const float * sh1)
{
    if (this->sh1.isEmpty()) return;

    Q_ASSERT(base >= 0 && count >= 0 && base + count <= maxSplats);

    encodeSh1Array(this->sh1.data() + (qsizetype) base * 2, sh1, count, encoding.sh1Max);
}

// SPARK: PackedSplatsData::set_sh2
/* virtual */ void WPackedSplats::setSh2(int base, int count, const float * sh2)
{
    if (this->sh2.isEmpty()) return;

    Q_ASSERT(base >= 0 && count >= 0 && base + count <= maxSplats);

    encodeSh2Array(this->sh2.data() + (qsizetype) base * 4, sh2, count, encoding.sh2Max);
}

// SPARK: PackedSplatsData::set_sh3
/* virtual */ void WPackedSplats::setSh3(int base, int count, const float * sh3)
{
    if (this->sh3.isEmpty()) return;

    Q_ASSERT(base >= 0 && count >= 0 && base + count <= maxSplats);

    encodeSh3Array(this->sh3.data() + (qsizetype) base * 4, sh3, count, encoding.sh3Max);
}
