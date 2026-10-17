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

#include "WSplatAccumulator.h"

// Qt includes
#include <rhi/qrhi.h>

//-------------------------------------------------------------------------------------------------
// Ctor / dtor
//-------------------------------------------------------------------------------------------------

WSplatAccumulator::WSplatAccumulator()
{
    maxSplats = 0;
    numSplats = 0;

    version        = -1;
    mappingVersion = -1;

    target  = NULL;
    target3 = NULL;

    drawBindings = NULL;
    drawOrdering = NULL;

    memset(&uniforms, 0, sizeof(WSplatAccumulateUniforms));
}

WSplatAccumulator::~WSplatAccumulator()
{
    dispose();
}

//-------------------------------------------------------------------------------------------------
// Interface
//-------------------------------------------------------------------------------------------------

// SPARK: dispose
void WSplatAccumulator::dispose()
{
    qDeleteAll(layers);

    layers.clear();

    delete drawBindings;

    drawBindings = NULL;
    drawOrdering = NULL;

    delete target;
    delete target3;

    target  = NULL;
    target3 = NULL;

    maxSplats = 0;
}

// SPARK: ensureGenerate
bool WSplatAccumulator::ensureGenerate(QRhi * rhi, int maxSplats,
                                       QRhiRenderPassDescriptor ** renderPass)
{
    if (target && qMax(maxSplats, 1) <= this->maxSplats) return false;

    dispose();

    // The packed Gsplats are stored in a 2D array texture of max size 2048 x 2048 x 2048, one
    // RGBA32UI pixel = 4 uint32 = one Gsplat.
    size = WPackedSplats::getTextureSize(qMax(maxSplats, 1));

    this->maxSplats = size.maxSplats;

    QSize pixelSize(size.width, size.height);

    target = rhi->newTextureArray(QRhiTexture::RGBA32UI, size.depth, pixelSize, 1,
                                  QRhiTexture::RenderTarget);

    // NOTE: The metric is read back on the CPU for sorting.
    target3 = rhi->newTextureArray(QRhiTexture::RGBA8, size.depth, pixelSize, 1,
                                   QRhiTexture::RenderTarget | QRhiTexture::UsedAsTransferSource);

    if (target->create() == false || target3->create() == false)
    {
        qWarning("WSplatAccumulator::ensureGenerate: Failed to create the targets (%d splats).",
                 this->maxSplats);

        dispose();

        return false;
    }

    // NOTE: One render target per layer. The integer attachment cannot be cleared portably (the
    //       OpenGL clear is undefined on integer buffers), so the contents are preserved and the
    //       fullscreen pass writes every texel of the rows it covers, like Spark.
    for (int layer = 0; layer < size.depth; layer++)
    {
        QRhiColorAttachment attachment0(target);
        QRhiColorAttachment attachment1(target3);

        attachment0.setLayer(layer);
        attachment1.setLayer(layer);

        QRhiTextureRenderTargetDescription description;

        description.setColorAttachments({ attachment0, attachment1 });

        QRhiTextureRenderTarget * renderTarget
            = rhi->newTextureRenderTarget(description,
                                          QRhiTextureRenderTarget::PreserveColorContents);

        // NOTE: Every layer shares one render pass descriptor, they all have the same formats.
        if (*renderPass == NULL)
        {
            *renderPass = renderTarget->newCompatibleRenderPassDescriptor();
        }

        renderTarget->setRenderPassDescriptor(*renderPass);

        renderTarget->create();

        layers.append(renderTarget);
    }

    return true;
}

// SPARK: generate
void WSplatAccumulator::generate(QRhi * rhi, QRhiCommandBuffer * cb, WSplatAccumulatePass & pass,
                                 const WSplatAccumulateUniforms & uniforms, int base, int count)
{
    // Generate the Gsplats in "layer" chunks, in horizontal row ranges, that cover the total
    // count of Gsplats.
    int nextBase = (base + count + SPLAT_TEX_WIDTH - 1) / SPLAT_TEX_WIDTH * SPLAT_TEX_WIDTH;

    int layerSize = SPLAT_TEX_WIDTH * SPLAT_TEX_HEIGHT;

    this->uniforms = uniforms;

    WSplatAccumulateUniforms layerUniforms = uniforms;

    layerUniforms.targetBase  = base;
    layerUniforms.targetCount = count;

    // Keep generating layers until we've reached the next generation's base
    while (base < nextBase)
    {
        int layer = base / layerSize;

        layerUniforms.targetLayer = layer;

        int layerBase   = layer * layerSize;
        int layerYStart = (base - layerBase) / SPLAT_TEX_WIDTH;

        int layerYEnd = qMin(size.height,
                             (nextBase - layerBase + SPLAT_TEX_WIDTH - 1) / SPLAT_TEX_WIDTH);

        QRhiResourceUpdateBatch * batch = rhi->nextResourceUpdateBatch();

        QRhiBuffer * buffer = pass.uniforms.at(layer);

        batch->updateDynamicBuffer(buffer, 0, sizeof(WSplatAccumulateUniforms), &layerUniforms);

        // NOTE: Integer attachments are never cleared, see ensureGenerate.
        cb->beginPass(layers.at(layer), Qt::transparent, { 1.0f, 0 }, batch);

        cb->setGraphicsPipeline(pass.pipeline);

        cb->setViewport(QRhiViewport(0, 0, size.width, size.height));

        // Render the desired portion of the layer.
        // NOTE: QRhiScissor has a bottom left origin. Texel rows match gl_FragCoord.y on every
        //       backend, so the rows are flipped when the framebuffer is not y up.
        int rows = layerYEnd - layerYStart;

        int y;

        if (rhi->isYUpInFramebuffer()) y = layerYStart;
        else                           y = size.height - layerYEnd;

        cb->setScissor(QRhiScissor(0, y, SPLAT_TEX_WIDTH, rows));

        cb->setShaderResources(pass.bindings.at(layer));

        cb->draw(3);

        cb->endPass();

        base += SPLAT_TEX_WIDTH * rows;
    }
}

//-------------------------------------------------------------------------------------------------
// Static functions
//-------------------------------------------------------------------------------------------------

// SPARK: generateMapping
/* static */ int WSplatAccumulator::generateMapping(int numSplats)
{
    // Generation happens in horizontal row chunks, so round up to full width
    return (numSplats + SPLAT_TEX_WIDTH - 1) / SPLAT_TEX_WIDTH * SPLAT_TEX_WIDTH;
}
