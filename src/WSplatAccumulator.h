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

// SPARK v2.3.1: src/SplatAccumulator.ts

#ifndef WSPLATACCUMULATOR_H
#define WSPLATACCUMULATOR_H

// Qt includes
#include <QMatrix4x4>
#include <QVector3D>
#include <QVector>

// Splat includes
#include <WPackedSplats.h>

// Forward declarations
class QRhi;
class QRhiCommandBuffer;
class QRhiTexture;
class QRhiTextureRenderTarget;
class QRhiRenderPassDescriptor;
class QRhiGraphicsPipeline;
class QRhiShaderResourceBindings;
class QRhiBuffer;

//-------------------------------------------------------------------------------------------------
// WSplatAccumulateUniforms
//-------------------------------------------------------------------------------------------------

// NOTE: Mirrors the std140 'buf' block of accumulate.frag.
struct WSplatAccumulateUniforms
{
    qint32 targetLayer;
    qint32 targetBase;
    qint32 targetCount;
    qint32 numSplats;

    float rgbMinMaxLnScaleMinMax[4];

    float  shMax[4];
    qint32 numSh;
    qint32 lodOpacity;
    qint32 sortRadial;
    qint32 padding;

    float rotate[4];
    float translateScale[4];
    float recolor[4];
    float viewOrigin[4];
    float viewCenter[4];
    float viewDir[4];
};

//-------------------------------------------------------------------------------------------------
// WSplatAccumulatePass
//-------------------------------------------------------------------------------------------------

// NOTE: Shared resources to generate an accumulator, owned by WSplatRenderer. There is one
//       uniform buffer and binding set per array layer, since every layer is a separate pass.
struct WSplatAccumulatePass
{
    QRhiRenderPassDescriptor * renderPass = NULL;
    QRhiGraphicsPipeline     * pipeline   = NULL;

    QVector<QRhiBuffer                 *> uniforms;
    QVector<QRhiShaderResourceBindings *> bindings;
};

//-------------------------------------------------------------------------------------------------
// WSplatAccumulator
//-------------------------------------------------------------------------------------------------

class SPLAT_EXPORT WSplatAccumulator
{
public:
    WSplatAccumulator();

    ~WSplatAccumulator();

public: // Interface
    void dispose();

    // SPARK: ensureGenerate. Returns true when the targets were (re)allocated.
    bool ensureGenerate(QRhi * rhi, int maxSplats, QRhiRenderPassDescriptor ** renderPass);

    // SPARK: generate. Records one pass per array layer covering [base, base + count).
    void generate(QRhi * rhi, QRhiCommandBuffer * cb, WSplatAccumulatePass & pass,
                  const WSplatAccumulateUniforms & uniforms, int base, int count);

public: // Static functions
    // SPARK: generateMapping (a single SplatMesh). Returns maxSplats.
    static int generateMapping(int numSplats);

public: // Variables
    QMatrix4x4 viewToWorld;

    QVector3D viewOrigin;
    QVector3D viewDirection;

    int maxSplats;
    int numSplats;

    int version;
    int mappingVersion;

    WSplatTextureSize size;

    // NOTE: The uniforms of the last generate, the CPU sort metric uses the same transform.
    WSplatAccumulateUniforms uniforms;

    // NOTE: The two MRT attachments: the RGBA32UI splats (target) and the RGBA8 float bits of the
    //       sort metric (target3), as named in computeUvec4_Vec4.glsl.
    QRhiTexture * target;
    QRhiTexture * target3;

    QVector<QRhiTextureRenderTarget *> layers;

    // NOTE: The draw bindings reading this accumulator, rebuilt when the ordering texture or the
    //       targets change.
    QRhiShaderResourceBindings * drawBindings;
    QRhiTexture                * drawOrdering;
};

#endif // WSPLATACCUMULATOR_H
