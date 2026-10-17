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

// SPARK v2.3.1: rust/spark-lib/src/decoder.rs
//               (SplatInit, SplatEncoding, SplatProps, SplatReceiver)

#ifndef WSPLATRECEIVER_H
#define WSPLATRECEIVER_H

// Splat includes
#include <Splat.h>

//-------------------------------------------------------------------------------------------------
// WSplatInit
//-------------------------------------------------------------------------------------------------

struct WSplatInit
{
    int numSplats   = 0;
    int maxShDegree = 0;

    bool lodTree = false;
};

//-------------------------------------------------------------------------------------------------
// WSplatEncoding
//-------------------------------------------------------------------------------------------------

struct WSplatEncoding
{
    float rgbMin = 0.0f;
    float rgbMax = 1.0f;

    float lnScaleMin = SPLAT_LN_SCALE_MIN;
    float lnScaleMax = SPLAT_LN_SCALE_MAX;

    float sh1Max = 1.0f;
    float sh2Max = 1.0f;
    float sh3Max = 1.0f;

    bool lodOpacity = false;
};

//-------------------------------------------------------------------------------------------------
// WSplatProps
//-------------------------------------------------------------------------------------------------

// NOTE: A batch of 'count' splats. Each array is NULL when absent. Layouts follow Spark:
//       center / rgb / scale: 3 floats per splat, quat: 4 floats per splat (x, y, z, w),
//       opacity: 1 float per splat, sh1 / sh2 / sh3: 9 / 15 / 21 floats per splat, coefficient
//       major with rgb interleaved (sh[k * 3 + channel]).
struct WSplatProps
{
    const float * center  = NULL;
    const float * opacity = NULL;
    const float * rgb     = NULL;
    const float * scale   = NULL;
    const float * quat    = NULL;

    const float * sh1 = NULL;
    const float * sh2 = NULL;
    const float * sh3 = NULL;
};

//-------------------------------------------------------------------------------------------------
// WSplatReceiver
//-------------------------------------------------------------------------------------------------

class SPLAT_EXPORT WSplatReceiver
{
public:
    virtual ~WSplatReceiver() {}

public: // Interface
    virtual bool initSplats(const WSplatInit & init) = 0;

    virtual void finish() {}

    virtual void setEncoding(const WSplatEncoding & encoding) = 0;

    virtual void setBatch(int base, int count, const WSplatProps & batch) = 0;

    virtual void setCenter (int base, int count, const float * center)  = 0;
    virtual void setOpacity(int base, int count, const float * opacity) = 0;
    virtual void setRgb    (int base, int count, const float * rgb)     = 0;
    virtual void setRgba   (int base, int count, const float * rgba)    = 0;
    virtual void setScale  (int base, int count, const float * scale)   = 0;
    virtual void setQuat   (int base, int count, const float * quat)    = 0;

    virtual void setSh1(int base, int count, const float * sh1) = 0;
    virtual void setSh2(int base, int count, const float * sh2) = 0;
    virtual void setSh3(int base, int count, const float * sh3) = 0;
};

#endif // WSPLATRECEIVER_H
