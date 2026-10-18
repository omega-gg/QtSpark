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
//               rust/spark-lib/src/splat_encode.rs (encode_packed_splat, encode_sh*_array)
//               src/utils.ts (getTextureSize)

#ifndef WPACKEDSPLATS_H
#define WPACKEDSPLATS_H

// Qt includes
#include <QVector>

// C++ includes
#include <functional>

// Splat includes
#include <WSplatReceiver.h>

//-------------------------------------------------------------------------------------------------
// WSplatTextureSize
//-------------------------------------------------------------------------------------------------

struct WSplatTextureSize
{
    int width     = 0;
    int height    = 0;
    int depth     = 0;
    int maxSplats = 0;
};

//-------------------------------------------------------------------------------------------------
// WPackedSplats
//-------------------------------------------------------------------------------------------------

// NOTE: CPU side of Spark's PackedSplats. Every splat is 4 x uint32 (16 bytes):
//       word0: r8 g8 b8 a8
//       word1: half(x) | half(y) << 16
//       word2: half(z) | quatU << 16 | quatV << 24
//       word3: scaleX | scaleY << 8 | scaleZ << 16 | quatAngle << 24
//       Arrays are sized to maxSplats (a full 2048 x height x depth texture array) so they can
//       be uploaded as is.
class SPLAT_EXPORT WPackedSplats : public WSplatReceiver
{
public:
    WPackedSplats();

public: // Interface
    int numSh() const;

public: // Static functions
    // SPARK v2.3.1: src/utils.ts getTextureSize
    static WSplatTextureSize getTextureSize(int numSplats);

    // SPARK v2.3.1: rust/spark-lib/src/splat_encode.rs
    static void encodePackedSplat(quint32 * packed, const float * center, float opacity,
                                  const float * rgb, const float * scale, const float * quat,
                                  const WSplatEncoding & encoding);

    static void encodePackedSplatCenter (quint32 * packed, const float * center);
    static void encodePackedSplatOpacity(quint32 * packed, float opacity,
                                         const WSplatEncoding & encoding);
    static void encodePackedSplatRgb    (quint32 * packed, const float * rgb,
                                         const WSplatEncoding & encoding);
    static void encodePackedSplatRgba   (quint32 * packed, const float * rgba,
                                         const WSplatEncoding & encoding);
    static void encodePackedSplatScale  (quint32 * packed, const float * scale,
                                         const WSplatEncoding & encoding);
    static void encodePackedSplatQuat   (quint32 * packed, const float * quat);

    // NOTE: out receives 2 words per splat for sh1 and 4 words per splat for sh2 / sh3.
    static void encodeSh1Array(quint32 * out, const float * sh1, int count, float sh1Max);
    static void encodeSh2Array(quint32 * out, const float * sh2, int count, float sh2Max);
    static void encodeSh3Array(quint32 * out, const float * sh3, int count, float sh3Max);

    static quint16 floatToHalf(float value);
    static float   halfToFloat(quint16 value);

public: // WSplatReceiver implementation
    /* virtual */ bool initSplats(const WSplatInit & init);

    // NOTE: Splat only: decodes the float centers for the CPU sort metric.
    /* virtual */ void finish();

    /* virtual */ void setEncoding(const WSplatEncoding & encoding);

    /* virtual */ void setBatch(int base, int count, const WSplatProps & batch);

    /* virtual */ void setCenter (int base, int count, const float * center);
    /* virtual */ void setOpacity(int base, int count, const float * opacity);
    /* virtual */ void setRgb    (int base, int count, const float * rgb);
    /* virtual */ void setRgba   (int base, int count, const float * rgba);
    /* virtual */ void setScale  (int base, int count, const float * scale);
    /* virtual */ void setQuat   (int base, int count, const float * quat);

    /* virtual */ void setSh1(int base, int count, const float * sh1);
    /* virtual */ void setSh2(int base, int count, const float * sh2);
    /* virtual */ void setSh3(int base, int count, const float * sh3);

private: // Functions
    void checkCanceled() const;

public: // Variables
    // NOTE: Polled while loading, once per batch. When it returns true the receiver functions
    //       throw WSplatCanceled.
    std::function<bool()> canceled;

    int numSplats;
    int maxSplats;

    int maxShDegree;

    WSplatEncoding encoding;

    WSplatTextureSize textureSize;

    QVector<quint32> packed; // maxSplats * 4
    QVector<quint32> sh1;    // maxSplats * 2, empty when maxShDegree < 1
    QVector<quint32> sh2;    // maxSplats * 4, empty when maxShDegree < 2
    QVector<quint32> sh3;    // maxSplats * 4, empty when maxShDegree < 3

    // NOTE: Splat only: the object space centers decoded from the packed half floats, exactly
    //       what the GPU reads (numSplats * 3). Filled by finish().
    QVector<float> centers;
};

#endif // WPACKEDSPLATS_H
