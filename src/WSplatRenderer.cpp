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

// SPARK v2.3.1: src/SparkRenderer.ts

#include "WSplatRenderer.h"

// Qt includes
#include <QFile>
#include <QtConcurrent>
#include <rhi/qrhi.h>

// Splat includes
#include <WPackedSplats.h>
#include <WSplatSort.h>

//-------------------------------------------------------------------------------------------------
// Static variables

// SPARK: SplatGeometry QUAD_VERTICES / QUAD_INDICES
static const float QUAD_VERTICES[] =
{
    -1.0f, -1.0f, 0.0f,
     1.0f, -1.0f, 0.0f,
     1.0f,  1.0f, 0.0f,
    -1.0f,  1.0f, 0.0f
};

static const quint16 QUAD_INDICES[] = { 0, 1, 2, 0, 2, 3 };

//-------------------------------------------------------------------------------------------------
// Static functions

template <typename T>
static void release(T *& resource)
{
    delete resource;

    resource = NULL;
}

static QShader loadShader(const QString & name)
{
    QFile file(":/splat/shaders/" + name + ".qsb");

    if (file.open(QIODevice::ReadOnly) == false)
    {
        qWarning("WSplatRenderer: Cannot open shader %s.", name.toUtf8().constData());

        return QShader();
    }

    return QShader::fromSerialized(file.readAll());
}

static void copyVector4(float * out, const QVector4D & vector)
{
    out[0] = vector.x();
    out[1] = vector.y();
    out[2] = vector.z();
    out[3] = vector.w();
}

static void copyQuaternion(float * out, const QQuaternion & quaternion)
{
    // NOTE: GLSL quaternions are (x, y, z, w).
    out[0] = quaternion.x();
    out[1] = quaternion.y();
    out[2] = quaternion.z();
    out[3] = quaternion.scalar();
}

static QVector3D cameraPosition(const QMatrix4x4 & matrixWorld)
{
    return matrixWorld.column(3).toVector3D();
}

// NOTE: THREE.Object3D.getWorldDirection for a camera: -Z in world space, normalized.
static QVector3D cameraDirection(const QMatrix4x4 & matrixWorld)
{
    return (-matrixWorld.column(2).toVector3D()).normalized();
}

// NOTE: THREE.Matrix4.compose(position, quaternion, scale) with a uniform scale.
static QMatrix4x4 composeMatrix(const QVector3D & position, const QQuaternion & quaternion,
                                float scale)
{
    QMatrix4x4 matrix;

    matrix.translate(position);
    matrix.rotate   (quaternion);
    matrix.scale    (scale);

    return matrix;
}

//-------------------------------------------------------------------------------------------------
// WSplatReadback
//-------------------------------------------------------------------------------------------------

#include "WSplatRenderer_p.h"

WSplatReadback::~WSplatReadback()
{
    releaseNative();

    qDeleteAll(results);
}

// SPARK: readbackDepth
void WSplatReadback::request(QRhiResourceUpdateBatch * batch, QRhiTexture * texture,
                             const WSplatTextureSize & size, int count, int texelSize,
                             const std::function<void()> & completed)
{
    int layerSize = SPLAT_TEX_WIDTH * SPLAT_TEX_HEIGHT;

    int baseIndex = 0;

    // We can only read back one 2D array layer of pixels at a time, so loop through them and
    // initiate the readback.
    while (baseIndex < count)
    {
        int layer     = baseIndex / layerSize;
        int layerBase = layer * layerSize;

        int layerYEnd = qMin(size.height,
                             (count - layerBase + SPLAT_TEX_WIDTH - 1) / SPLAT_TEX_WIDTH);

        QRhiReadbackResult * result = new QRhiReadbackResult;

        qint64 offset = qint64(layerBase) * texelSize;

        result->completed = [this, result, offset, completed]()
        {
            const QByteArray & bytes = result->data;

            memcpy(data.data() + offset, bytes.constData(),
                   qMin(qint64(bytes.size()), data.size() - offset));

            pendingLayers--;

            if (pendingLayers == 0 && completed) completed();
        };

        results.append(result);

        pendingLayers++;

        QRhiReadbackDescription description(texture);

        description.setLayer(layer);

        description.setRect(QRect(0, 0, SPLAT_TEX_WIDTH, layerYEnd));

        batch->readBackTexture(description, result);

        baseIndex += SPLAT_TEX_WIDTH * layerYEnd;
    }
}

bool WSplatReadback::isPending() const
{
    return (pendingLayers != 0);
}

//-------------------------------------------------------------------------------------------------
// Private
//-------------------------------------------------------------------------------------------------

WSplatRendererPrivate::WSplatRendererPrivate(WSplatRenderer * p) : q_ptr(p) {}

WSplatRendererPrivate::~WSplatRendererPrivate()
{
    releaseResources();

    qDeleteAll(pool);

    delete capture;
}

void WSplatRendererPrivate::init()
{
    rhi = NULL;

    supported = false;

    splatsDirty = false;

    meshVersion        = 0;
    meshMappingVersion = 0;

    lastScale = 1.0f;
    lastNumSh = 0;

    packedTexture = NULL;
    sh1Texture    = NULL;
    sh2Texture    = NULL;
    sh3Texture    = NULL;

    emptyRG   = NULL;
    emptyRGBA = NULL;

    sampler = NULL;

    uploads = NULL;

    // SPARK: constructor, the display accumulator plus two free ones.
    for (int i = 0; i < 3; i++)
    {
        pool.append(new WSplatAccumulator);
    }

    display = pool.at(0);
    current = display;

    accumulators.append(pool.at(1));
    accumulators.append(pool.at(2));

    generated = false;

    sortDirty = false;
    sorting   = false;

    sortClock.start();

    lastSortTime = -1;

    job = NULL;

    activeSplats = 0;
    maxSplats    = 0;

    orderingTexture = NULL;
    orderingRows    = 0;

    drawRenderPass = NULL;
    drawPipeline   = NULL;
    drawLayout     = NULL;

    quadVertices = NULL;
    quadIndices  = NULL;
    drawUniforms = NULL;

    captureRequested = false;

    capture = NULL;
}

//-------------------------------------------------------------------------------------------------
// Private functions
//-------------------------------------------------------------------------------------------------

bool WSplatRendererPrivate::checkFeatures()
{
    if (rhi->isFeatureSupported(QRhi::Instancing) == false)
    {
        qWarning("WSplatRenderer: Instancing is not supported.");

        return false;
    }

    if (rhi->isFeatureSupported(QRhi::TexelFetch) == false)
    {
        qWarning("WSplatRenderer: texelFetch is not supported.");

        return false;
    }

    if (rhi->isFeatureSupported(QRhi::TextureArrays) == false)
    {
        qWarning("WSplatRenderer: Texture arrays are not supported.");

        return false;
    }

    if (rhi->resourceLimit(QRhi::TextureSizeMax) < SPLAT_ORDERING_WIDTH)
    {
        qWarning("WSplatRenderer: The maximum texture size is lower than %d.",
                 SPLAT_ORDERING_WIDTH);

        return false;
    }

    // NOTE: The OpenGL backend reports the 32 bit integer formats as unsupported below ES 3.1,
    //       even though ES 3.0 can sample and render them, so this is only a warning.
    if (rhi->isTextureFormatSupported(QRhiTexture::RGBA32UI) == false)
    {
        qWarning("WSplatRenderer: RGBA32UI is reported as unsupported.");
    }

    return true;
}

void WSplatRendererPrivate::createResources()
{
    accumulateVert = loadShader("accumulate.vert");
    accumulateFrag = loadShader("accumulate.frag");
    splatVert      = loadShader("splat.vert");
    splatFrag      = loadShader("splat.frag");

    sampler = rhi->newSampler(QRhiSampler::Nearest, QRhiSampler::Nearest, QRhiSampler::None,
                              QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge);

    sampler->create();

    // NOTE: Bound in place of the absent SH textures.
    emptyRG   = rhi->newTextureArray(QRhiTexture::RG32UI,   1, QSize(1, 1));
    emptyRGBA = rhi->newTextureArray(QRhiTexture::RGBA32UI, 1, QSize(1, 1));

    emptyRG  ->create();
    emptyRGBA->create();

    quadVertices = rhi->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::VertexBuffer,
                                  sizeof(QUAD_VERTICES));

    quadIndices = rhi->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::IndexBuffer,
                                 sizeof(QUAD_INDICES));

    drawUniforms = rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer,
                                  sizeof(WSplatDrawUniforms));

    quadVertices->create();
    quadIndices ->create();
    drawUniforms->create();

    uploads = rhi->nextResourceUpdateBatch();

    uploads->uploadStaticBuffer(quadVertices, QUAD_VERTICES);
    uploads->uploadStaticBuffer(quadIndices,  QUAD_INDICES);

    QByteArray zeros(16, 0);

    uploads->uploadTexture(emptyRG,   QRhiTextureUploadDescription(
                                      QRhiTextureUploadEntry(0, 0,
                                      QRhiTextureSubresourceUploadDescription(zeros.left(8)))));

    uploads->uploadTexture(emptyRGBA, QRhiTextureUploadDescription(
                                      QRhiTextureUploadEntry(0, 0,
                                      QRhiTextureSubresourceUploadDescription(zeros))));

    ensureOrdering(1);
}

void WSplatRendererPrivate::createDrawPipeline(QRhiRenderPassDescriptor * renderPass)
{
    delete drawPipeline;
    delete drawLayout;

    drawRenderPass = renderPass;

    // NOTE: The layout only, the bindings of each accumulator are layout compatible.
    drawLayout = rhi->newShaderResourceBindings();

    drawLayout->setBindings(
    {
        QRhiShaderResourceBinding::uniformBuffer(0, QRhiShaderResourceBinding::VertexStage
                                                    |
                                                    QRhiShaderResourceBinding::FragmentStage,
                                                 drawUniforms),

        QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::VertexStage,
                                                  orderingTexture, sampler),

        QRhiShaderResourceBinding::sampledTexture(2, QRhiShaderResourceBinding::VertexStage,
                                                  emptyRGBA, sampler)
    });

    drawLayout->create();

    drawPipeline = rhi->newGraphicsPipeline();

    drawPipeline->setShaderStages(
    {
        { QRhiShaderStage::Vertex,   splatVert },
        { QRhiShaderStage::Fragment, splatFrag }
    });

    QRhiVertexInputLayout inputLayout;

    inputLayout.setBindings  ({ { 3 * sizeof(float) } });
    inputLayout.setAttributes({ { 0, 0, QRhiVertexInputAttribute::Float3, 0 } });

    drawPipeline->setVertexInputLayout(inputLayout);

    // SPARK: premultipliedAlpha: true, depthWrite: false. The splats are the only content of
    //        the target, so the depth test is off.
    QRhiGraphicsPipeline::TargetBlend blend;

    blend.enable   = true;
    blend.srcColor = QRhiGraphicsPipeline::One;
    blend.dstColor = QRhiGraphicsPipeline::OneMinusSrcAlpha;
    blend.srcAlpha = QRhiGraphicsPipeline::One;
    blend.dstAlpha = QRhiGraphicsPipeline::OneMinusSrcAlpha;

    drawPipeline->setTargetBlends({ blend });

    drawPipeline->setDepthTest (false);
    drawPipeline->setDepthWrite(false);

    // SPARK: side: DoubleSide
    drawPipeline->setCullMode(QRhiGraphicsPipeline::None);

    drawPipeline->setShaderResourceBindings(drawLayout);

    drawPipeline->setRenderPassDescriptor(renderPass);

    drawPipeline->create();
}

void WSplatRendererPrivate::createAccumulatePipeline()
{
    QRhiGraphicsPipeline * pipeline = rhi->newGraphicsPipeline();

    pipeline->setShaderStages(
    {
        { QRhiShaderStage::Vertex,   accumulateVert },
        { QRhiShaderStage::Fragment, accumulateFrag }
    });

    // NOTE: Two attachments, blending off (the default TargetBlend).
    pipeline->setTargetBlends({ QRhiGraphicsPipeline::TargetBlend(),
                                QRhiGraphicsPipeline::TargetBlend() });

    pipeline->setFlags(QRhiGraphicsPipeline::UsesScissor);

    pipeline->setShaderResourceBindings(accumulatePass.bindings.first());

    pipeline->setRenderPassDescriptor(accumulatePass.renderPass);

    pipeline->create();

    accumulatePass.pipeline = pipeline;
}

void WSplatRendererPrivate::releaseResources()
{
    if (rhi == NULL) return;

    releaseTargets();

    delete accumulatePass.pipeline;
    delete accumulatePass.renderPass;

    qDeleteAll(accumulatePass.uniforms);
    qDeleteAll(accumulatePass.bindings);

    accumulatePass = WSplatAccumulatePass();

    release(packedTexture);
    release(sh1Texture);
    release(sh2Texture);
    release(sh3Texture);

    release(emptyRG);
    release(emptyRGBA);

    release(sampler);

    release(drawPipeline);
    release(drawLayout);

    drawRenderPass = NULL;

    release(quadVertices);
    release(quadIndices);
    release(drawUniforms);

    if (uploads)
    {
        uploads->release();

        uploads = NULL;
    }

    // NOTE: The splats are uploaded again on the next initialize.
    if (splats) splatsDirty = true;

    supported = false;

    rhi = NULL;
}

// NOTE: The accumulators and the ordering only grow while a mesh is shown, like in Spark. Without
//       a mesh nothing needs them, so they are freed with the pending sort.
void WSplatRendererPrivate::releaseTargets()
{
    waitPending();

    for (WSplatAccumulator * accumulator : pool)
    {
        accumulator->dispose();

        accumulator->version        = -1;
        accumulator->mappingVersion = -1;
    }

    release(orderingTexture);

    orderingRows = 0;
    maxSplats    = 0;

    lastOrdering.clear();

    activeSplats = 0;

    display = pool.at(0);
    current = display;

    accumulators = { pool.at(1), pool.at(2) };

    sortDirty = false;
}

// SPARK: PackedSplats.getTexture / ensureShTextures
void WSplatRendererPrivate::uploadSplats(QRhiResourceUpdateBatch * batch)
{
    release(packedTexture);
    release(sh1Texture);
    release(sh2Texture);
    release(sh3Texture);

    splatsDirty = false;

    if (splats.isNull() || splats->numSplats == 0)
    {
        releaseTargets();

        return;
    }

    const WSplatTextureSize & size = splats->textureSize;

    QSize pixelSize(size.width, size.height);

    int layerSplats = size.width * size.height;

    struct Source
    {
        QRhiTexture             ** texture;
        QRhiTexture::Format        format;
        const QVector<quint32>   * data;
        int                        words;
    };

    Source sources[] =
    {
        { &packedTexture, QRhiTexture::RGBA32UI, &splats->packed, 4 },
        { &sh1Texture,    QRhiTexture::RG32UI,   &splats->sh1,    2 },
        { &sh2Texture,    QRhiTexture::RGBA32UI, &splats->sh2,    4 },
        { &sh3Texture,    QRhiTexture::RGBA32UI, &splats->sh3,    4 }
    };

    for (const Source & source : sources)
    {
        if (source.data->isEmpty()) continue;

        QRhiTexture * texture = rhi->newTextureArray(source.format, size.depth, pixelSize);

        if (texture->create() == false)
        {
            qWarning("WSplatRenderer: Failed to create a splat texture (%d splats).",
                     size.maxSplats);

            delete texture;

            continue;
        }

        *(source.texture) = texture;

        int layerBytes = layerSplats * source.words * sizeof(quint32);

        const char * data = reinterpret_cast<const char *> (source.data->constData());

        QVector<QRhiTextureUploadEntry> entries;

        for (int layer = 0; layer < size.depth; layer++)
        {
            // NOTE: fromRawData avoids a copy, 'splats' outlives the batch submission.
            QByteArray bytes = QByteArray::fromRawData(data + qint64(layer) * layerBytes,
                                                       layerBytes);

            entries.append(QRhiTextureUploadEntry(layer, 0,
                                                  QRhiTextureSubresourceUploadDescription(bytes)));
        }

        QRhiTextureUploadDescription description;

        description.setEntries(entries.cbegin(), entries.cend());

        batch->uploadTexture(texture, description);
    }

    createAccumulateBindings();
}

void WSplatRendererPrivate::createAccumulateBindings()
{
    qDeleteAll(accumulatePass.bindings);

    accumulatePass.bindings.clear();

    if (packedTexture == NULL) return;

    int depth = splats->textureSize.depth;

    while (accumulatePass.uniforms.count() < depth)
    {
        QRhiBuffer * buffer = rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer,
                                             sizeof(WSplatAccumulateUniforms));

        buffer->create();

        accumulatePass.uniforms.append(buffer);
    }

    QRhiTexture * sh1 = (sh1Texture) ? sh1Texture : emptyRG;
    QRhiTexture * sh2 = (sh2Texture) ? sh2Texture : emptyRGBA;
    QRhiTexture * sh3 = (sh3Texture) ? sh3Texture : emptyRGBA;

    QRhiShaderResourceBinding::StageFlags stage = QRhiShaderResourceBinding::FragmentStage;

    for (int layer = 0; layer < depth; layer++)
    {
        QRhiShaderResourceBindings * bindings = rhi->newShaderResourceBindings();

        bindings->setBindings(
        {
            QRhiShaderResourceBinding::uniformBuffer (0, stage,
                                                      accumulatePass.uniforms.at(layer)),
            QRhiShaderResourceBinding::sampledTexture(1, stage, packedTexture, sampler),
            QRhiShaderResourceBinding::sampledTexture(2, stage, sh1, sampler),
            QRhiShaderResourceBinding::sampledTexture(3, stage, sh2, sampler),
            QRhiShaderResourceBinding::sampledTexture(4, stage, sh3, sampler)
        });

        bindings->create();

        accumulatePass.bindings.append(bindings);
    }
}

// SPARK: SplatMesh.update (transform, viewToObject, recolor, generator) and updateVersion
void WSplatRendererPrivate::updateMeshVersion()
{
    if (splats.isNull()) return;

    bool updated = false;

    if (lastPosition   != mesh.position
        ||
        lastQuaternion != mesh.quaternion
        ||
        lastScale      != mesh.scale)
    {
        lastPosition   = mesh.position;
        lastQuaternion = mesh.quaternion;
        lastScale      = mesh.scale;

        updated = true;
    }

    int numSh = qMin(splats->numSh(), mesh.maxSh);

    if (lastNumSh != numSh)
    {
        lastNumSh = numSh;

        updated = true;
    }

    QMatrix4x4 objectToWorld = composeMatrix(mesh.position, mesh.quaternion, mesh.scale);

    QVector3D viewToObject = objectToWorld.inverted().map(cameraPosition(camera.matrixWorld));

    if (lastViewToObject != viewToObject)
    {
        lastViewToObject = viewToObject;

        // Only trigger update if we have view-dependent spherical harmonics
        if (numSh > 0) updated = true;
    }

    if (lastRecolor != mesh.recolor)
    {
        lastRecolor = mesh.recolor;

        updated = true;
    }

    if (updated) meshVersion++;
}

WSplatAccumulateUniforms WSplatRendererPrivate::accumulateUniforms() const
{
    WSplatAccumulateUniforms uniforms;

    memset(&uniforms, 0, sizeof(WSplatAccumulateUniforms));

    const WSplatEncoding & encoding = splats->encoding;

    uniforms.numSplats = splats->numSplats;

    uniforms.rgbMinMaxLnScaleMinMax[0] = encoding.rgbMin;
    uniforms.rgbMinMaxLnScaleMinMax[1] = encoding.rgbMax;
    uniforms.rgbMinMaxLnScaleMinMax[2] = encoding.lnScaleMin;
    uniforms.rgbMinMaxLnScaleMinMax[3] = encoding.lnScaleMax;

    uniforms.shMax[0] = encoding.sh1Max;
    uniforms.shMax[1] = encoding.sh2Max;
    uniforms.shMax[2] = encoding.sh3Max;

    uniforms.numSh = qMin(splats->numSh(), mesh.maxSh);

    uniforms.lodOpacity = encoding.lodOpacity;

    uniforms.sortRadial = options.sortRadial;

    copyQuaternion(uniforms.rotate, mesh.quaternion);

    copyVector4(uniforms.translateScale, QVector4D(mesh.position, mesh.scale));

    copyVector4(uniforms.recolor, mesh.recolor);

    copyVector4(uniforms.viewOrigin, QVector4D(lastViewToObject, 0.0f));

    QVector3D viewCenter = cameraPosition (camera.matrixWorld);
    QVector3D viewDir    = cameraDirection(camera.matrixWorld);

    copyVector4(uniforms.viewCenter, QVector4D(viewCenter, 0.0f));
    copyVector4(uniforms.viewDir,    QVector4D(viewDir,    0.0f));

    return uniforms;
}

// SPARK: updateInternal
void WSplatRendererPrivate::updateInternal(QRhiCommandBuffer * cb)
{
    generated = false;

    if (packedTexture == NULL) return;

    updateMeshVersion();

    QVector3D center = cameraPosition (camera.matrixWorld);
    QVector3D dir    = cameraDirection(camera.matrixWorld);

    bool viewChanged = (center.distanceToPoint(sortedCenter) > 0.001f
                        ||
                        QVector3D::dotProduct(dir, sortedDir) < 0.999f);

    // NOTE: prepareGenerate computes the next versions from the current accumulator.
    int version        = meshVersion;
    int mappingVersion = meshMappingVersion;

    bool needsUpdate    = (viewChanged || version != current->version);
    bool mappingUpdated = (mappingVersion != display->mappingVersion);

    // Triggered by auto-update but no change
    if (needsUpdate == false) return;

    // We need to be able to sort the splats because the mapping has changed.
    // Try again next time around.
    if (mappingUpdated && sorting) return;

    WSplatAccumulator * next = accumulators.takeLast();

    // SPARK: SplatAccumulator.prepareGenerate
    next->viewToWorld   = camera.matrixWorld;
    next->viewOrigin    = center;
    next->viewDirection = dir;

    next->numSplats = splats->numSplats;

    next->version        = version;
    next->mappingVersion = mappingVersion;

    int maxSplats = WSplatAccumulator::generateMapping(next->numSplats);

    if (next->ensureGenerate(rhi, maxSplats, &accumulatePass.renderPass))
    {
        // NOTE: The bindings of this accumulator reference the old targets.
        release(next->drawBindings);

        next->drawOrdering = NULL;
    }

    if (next->target == NULL)
    {
        accumulators.append(next);

        return;
    }

    if (accumulatePass.pipeline == NULL) createAccumulatePipeline();

    next->generate(rhi, cb, accumulatePass, accumulateUniforms(), 0, next->numSplats);

    generated = true;

    stats.generates++;

    if (display->mappingVersion == next->mappingVersion)
    {
        // Same splat mapping so let's display it immediately and reuse the sort order
        accumulators.append(display);

        display = next;
    }
    else if (display != current)
    {
        // The previous current is not being displayed, so replace it
        accumulators.append(current);
    }

    current = next;

    sortDirty = true;
}

// SPARK: driveSort
void WSplatRendererPrivate::driveSort(QRhiCommandBuffer * cb)
{
    if (sorting || sortDirty == false) return;

    qint64 now = sortClock.elapsed();

    if (lastSortTime >= 0 && now < lastSortTime + options.minSortIntervalMs) return;

    sorting   = true;
    sortDirty = false;

    lastSortTime = now;

    sortedCenter = current->viewOrigin;
    sortedDir    = current->viewDirection;

    int numSplats = current->numSplats;
    int rows      = qMax(1, (current->maxSplats + SPLAT_ORDERING_PER_ROW - 1)
                            / SPLAT_ORDERING_PER_ROW);

    maxSplats = qMax(maxSplats, rows * SPLAT_ORDERING_PER_ROW);

    job = new WSplatSortJob;

    job->numSplats      = numSplats;
    job->maxSplats      = current->maxSplats;
    job->mappingVersion = current->mappingVersion;
    job->orderingRows   = rows;

    job->fastSort = options.fastSort;
    job->threads  = (options.fastSort) ? WSplatSort::idealThreads() : 1;

    job->metric.data = QByteArray(qint64(job->maxSplats) * sizeof(quint32), 0);

    job->ordering = QByteArray(qint64(maxSplats) * sizeof(quint32), 0);

    job->activeSplats = 0;

    job->sortStarted = false;

    job->metricMs = 0.0;
    job->sortMs   = 0.0;

    job->timer.start();

    // NOTE: The CPU sort metric skips the readback, see WSplatRenderOptions::cpuSortMetric.
    if (options.cpuSortMetric
        &&
        splats
        &&
        splats->centers.count() >= qsizetype(numSplats) * 3)
    {
        job->splats   = splats;
        job->uniforms = current->uniforms;

        startSort(job);

        return;
    }

    WSplatSortJob * sortJob = job;

    std::function<void()> completed = [this, sortJob]() { startSort(sortJob); };

    // NOTE: On D3D11 the QRhi readback ends with a blocking Map, so we poll our own copies.
    if (job->metric.requestNative(rhi, cb, current->target3, current->size, numSplats,
                                  sizeof(quint32), completed) == false)
    {
        QRhiResourceUpdateBatch * batch = rhi->nextResourceUpdateBatch();

        job->metric.request(batch, current->target3, current->size, numSplats, sizeof(quint32),
                            completed);

        cb->resourceUpdate(batch);
    }

    if (job->metric.isPending() == false) startSort(job);
}

// NOTE: Called from the readback completed callback on the render thread, or from driveSort for
//       the CPU sort metric.
void WSplatRendererPrivate::startSort(WSplatSortJob * job)
{
    job->metricMs = job->timer.nsecsElapsed() / 1000000.0;

    job->sortStarted = true;

    job->future = QtConcurrent::run([job]()
    {
        QElapsedTimer timer;

        timer.start();

        quint32 * metric = reinterpret_cast<quint32 *> (job->metric.data.data());

        if (job->splats)
        {
            const WSplatAccumulateUniforms & uniforms = job->uniforms;

            WSplatSort::computeMetrics(job->numSplats, job->splats->packed.constData(),
                                       job->splats->centers.constData(),
                                       uniforms.translateScale[3], uniforms.rotate,
                                       uniforms.translateScale, uniforms.viewCenter,
                                       uniforms.viewDir, uniforms.sortRadial, metric,
                                       job->threads);

            job->metricMs = timer.nsecsElapsed() / 1000000.0;

            timer.restart();
        }

        quint32 * ordering = reinterpret_cast<quint32 *> (job->ordering.data());

        if (job->fastSort)
        {
            job->activeSplats = WSplatSort::sort32SplatsFast(job->numSplats, metric, ordering,
                                                             job->threads);
        }
        else job->activeSplats = WSplatSort::sort32Splats(job->numSplats, metric,
                                                         job->maxSplats, ordering);

        job->sortMs = timer.nsecsElapsed() / 1000000.0;
    });
}

// SPARK: driveSort (after the sortSplats32 worker call)
void WSplatRendererPrivate::pollSort(QRhiResourceUpdateBatch * batch)
{
    releaseJobs();

    if (job == NULL || job->sortStarted == false || job->future.isFinished() == false) return;

    WSplatSortJob * sortJob = job;

    job = NULL;

    jobs.append(sortJob);

    activeSplats = sortJob->activeSplats;

    stats.sorts++;

    stats.metricMs = sortJob->metricMs;
    stats.sortMs   = sortJob->sortMs;

    ensureOrdering(sortJob->orderingRows);

    // SPARK: uploadU32DataTextureRows
    int rows = qMax(1, (activeSplats + SPLAT_ORDERING_PER_ROW - 1) / SPLAT_ORDERING_PER_ROW);

    QRhiTextureSubresourceUploadDescription description(sortJob->ordering);

    description.setSourceSize(QSize(SPLAT_ORDERING_WIDTH, qMin(rows, orderingRows)));

    batch->uploadTexture(orderingTexture,
                         QRhiTextureUploadDescription(QRhiTextureUploadEntry(0, 0, description)));

    lastOrdering = sortJob->ordering.left(qint64(activeSplats) * sizeof(quint32));

    if (current->mappingVersion == sortJob->mappingVersion
        &&
        current->mappingVersion != display->mappingVersion)
    {
        accumulators.append(display);

        display = current;
    }

    sorting = false;
}

// NOTE: The finished jobs are deleted once their readback callbacks all ran.
void WSplatRendererPrivate::releaseJobs()
{
    for (int i = jobs.count() - 1; i >= 0; i--)
    {
        WSplatSortJob * sortJob = jobs.at(i);

        if (sortJob->metric.isPending()) continue;

        sortJob->future.waitForFinished();

        delete sortJob;

        jobs.removeAt(i);
    }
}

void WSplatRendererPrivate::ensureOrdering(int rows)
{
    if (orderingTexture && rows <= orderingRows) return;

    // NOTE: The bindings referencing the old texture are rebuilt lazily.
    delete orderingTexture;

    orderingRows = rows;

    orderingTexture = rhi->newTexture(QRhiTexture::RGBA32UI,
                                      QSize(SPLAT_ORDERING_WIDTH, orderingRows));

    orderingTexture->create();
}

void WSplatRendererPrivate::updateDrawBindings(WSplatAccumulator * accumulator)
{
    if (accumulator->drawBindings && accumulator->drawOrdering == orderingTexture) return;

    delete accumulator->drawBindings;

    QRhiShaderResourceBindings * bindings = rhi->newShaderResourceBindings();

    bindings->setBindings(
    {
        QRhiShaderResourceBinding::uniformBuffer(0, QRhiShaderResourceBinding::VertexStage
                                                    |
                                                    QRhiShaderResourceBinding::FragmentStage,
                                                 drawUniforms),

        QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::VertexStage,
                                                  orderingTexture, sampler),

        QRhiShaderResourceBinding::sampledTexture(2, QRhiShaderResourceBinding::VertexStage,
                                                  accumulator->target, sampler)
    });

    bindings->create();

    accumulator->drawBindings = bindings;
    accumulator->drawOrdering = orderingTexture;
}

// SPARK: SparkRenderer.onBeforeRender (uniforms)
void WSplatRendererPrivate::updateDrawUniforms(QRhiResourceUpdateBatch * batch,
                                               QRhiRenderTarget        * renderTarget)
{
    WSplatDrawUniforms uniforms;

    memset(&uniforms, 0, sizeof(WSplatDrawUniforms));

    memcpy(uniforms.projectionMatrix, camera.projectionMatrix.constData(), 16 * sizeof(float));

    memcpy(uniforms.clipSpaceCorrMatrix, rhi->clipSpaceCorrMatrix().constData(),
           16 * sizeof(float));

    // NOTE: accumToWorld is a translation to the display accumulator's view origin.
    QMatrix4x4 accumToWorld;

    accumToWorld.translate(display->viewOrigin);

    QMatrix4x4 accumToCamera = camera.matrixWorld.inverted() * accumToWorld;

    QMatrix3x3 rotation = accumToCamera.toGenericMatrix<3, 3>();

    copyQuaternion(uniforms.renderToViewQuat,
                   QQuaternion::fromRotationMatrix(rotation).normalized());

    QVector3D position = accumToCamera.column(3).toVector3D();

    uniforms.renderToViewPos[0] = position.x();
    uniforms.renderToViewPos[1] = position.y();
    uniforms.renderToViewPos[2] = position.z();

    QSize size = renderTarget->pixelSize();

    uniforms.renderSize[0] = size.width ();
    uniforms.renderSize[1] = size.height();

    uniforms.maxStdDev      = options.maxStdDev;
    uniforms.minPixelRadius = options.minPixelRadius;
    uniforms.maxPixelRadius = options.maxPixelRadius;
    uniforms.minAlpha       = options.minAlpha;

    uniforms.blurAmount    = options.blurAmount;
    uniforms.preBlurAmount = options.preBlurAmount;
    uniforms.focalDistance = options.focalDistance;
    uniforms.apertureAngle = options.apertureAngle;

    uniforms.clipXY          = options.clipXY;
    uniforms.focalAdjustment = options.focalAdjustment;
    uniforms.falloff         = options.falloff;

    uniforms.isOrthographicValue = camera.isOrthographic;
    uniforms.lodInflateValue     = options.lodInflate;

    // NOTE: The Qt Quick target is RGBA8 without sRGB encoding, like a THREE.SRGBColorSpace
    //       output, so encodeLinear is false.
    uniforms.encodeLinearValue   = 0;
    uniforms.disableFalloffValue = 0;

    uniforms.tightBoundsValue = options.tightBounds;

    batch->updateDynamicBuffer(drawUniforms, 0, sizeof(WSplatDrawUniforms), &uniforms);
}

void WSplatRendererPrivate::waitPending()
{
    bool pending = (capture && (capture->splats.isPending() || capture->metric.isPending()));

    if (job && job->metric.isPending()) pending = true;

    for (WSplatSortJob * sortJob : jobs)
    {
        if (sortJob->metric.isPending()) pending = true;
    }

    // NOTE: finish() completes the pending readbacks, so every completed callback runs while its
    //       readback is still alive.
    if (pending) rhi->finish();

    if (job)
    {
        job->metric.pollNative(true);

        jobs.append(job);

        job = NULL;
    }

    releaseJobs();

    sorting = false;

    release(capture);
}

bool WSplatRendererPrivate::isSettled() const
{
    return (packedTexture
            &&
            splatsDirty == false && sorting == false && sortDirty == false
            &&
            job == NULL && display == current && generated == false);
}

//-------------------------------------------------------------------------------------------------
// Ctor / dtor
//-------------------------------------------------------------------------------------------------

WSplatRenderer::WSplatRenderer() : d_ptr(new WSplatRendererPrivate(this))
{
    Q_D(WSplatRenderer); d->init();
}

WSplatRenderer::~WSplatRenderer() {}

//-------------------------------------------------------------------------------------------------
// Interface
//-------------------------------------------------------------------------------------------------

void WSplatRenderer::setMesh(const WSplatMesh & mesh)
{
    Q_D(WSplatRenderer);

    if (d->mesh.splats != mesh.splats)
    {
        d->splats = mesh.splats;

        d->splatsDirty = true;

        // SPARK: updateMappingVersion, the splat count changed.
        d->meshMappingVersion++;
        d->meshVersion++;

        d->activeSplats = 0;
    }

    d->mesh = mesh;
}

void WSplatRenderer::setCamera(const WSplatCamera & camera)
{
    Q_D(WSplatRenderer); d->camera = camera;
}

void WSplatRenderer::setOptions(const WSplatRenderOptions & options)
{
    Q_D(WSplatRenderer);

    // NOTE: The sort metric depends on sortRadial, so the splats are generated again.
    if (d->options.sortRadial != options.sortRadial) d->meshVersion++;

    d->options = options;
}

bool WSplatRenderer::initialize(QRhi * rhi, QRhiRenderTarget * renderTarget)
{
    Q_D(WSplatRenderer);

    if (d->rhi != rhi)
    {
        d->releaseResources();

        d->rhi = rhi;

        d->supported = d->checkFeatures();

        if (d->supported) d->createResources();
    }

    if (d->supported == false) return false;

    QRhiRenderPassDescriptor * renderPass = renderTarget->renderPassDescriptor();

    // NOTE: A compatible render pass keeps the pipeline.
    if (d->drawPipeline == NULL
        ||
        d->drawRenderPass->isCompatible(renderPass) == false)
    {
        d->createDrawPipeline(renderPass);
    }
    else d->drawRenderPass = renderPass;

    return true;
}

void WSplatRenderer::render(QRhiCommandBuffer * cb, QRhiRenderTarget * renderTarget,
                            const QColor & clearColor)
{
    Q_D(WSplatRenderer);

    if (d->supported == false)
    {
        cb->beginPass(renderTarget, clearColor, { 1.0f, 0 });
        cb->endPass();

        return;
    }

    // NOTE: The uploads (static buffers, splat textures, ordering) land before any pass.
    QRhiResourceUpdateBatch * uploads = d->uploads;

    d->uploads = NULL;

    if (uploads == NULL) uploads = d->rhi->nextResourceUpdateBatch();

    if (d->splatsDirty) d->uploadSplats(uploads);

    // SPARK: three.js probeAsync, the readback fence is polled every frame.
    if (d->job) d->job->metric.pollNative();

    d->pollSort(uploads);

    cb->resourceUpdate(uploads);

    d->updateInternal(cb);

    d->driveSort(cb);

    if (d->captureRequested && d->isSettled()) d->startCapture(cb);

    QRhiResourceUpdateBatch * batch = d->rhi->nextResourceUpdateBatch();

    d->updateDrawUniforms(batch, renderTarget);

    cb->beginPass(renderTarget, clearColor, { 1.0f, 0 }, batch);

    WSplatAccumulator * display = d->display;

    if (d->activeSplats > 0 && display->target && d->packedTexture)
    {
        d->updateDrawBindings(display);

        QSize size = renderTarget->pixelSize();

        cb->setGraphicsPipeline(d->drawPipeline);

        cb->setViewport(QRhiViewport(0, 0, size.width(), size.height()));

        cb->setShaderResources(display->drawBindings);

        const QRhiCommandBuffer::VertexInput vertexInput(d->quadVertices, 0);

        cb->setVertexInput(0, 1, &vertexInput, d->quadIndices, 0,
                           QRhiCommandBuffer::IndexUInt16);

        cb->drawIndexed(6, d->activeSplats);
    }

    cb->endPass();

    d->stats.numSplats    = (d->splats) ? d->splats->numSplats : 0;
    d->stats.activeSplats = d->activeSplats;
}

void WSplatRenderer::releaseResources()
{
    Q_D(WSplatRenderer); d->releaseResources();
}

bool WSplatRenderer::needsUpdate() const
{
    Q_D(const WSplatRenderer);

    if (d->sorting || d->sortDirty || d->splatsDirty || d->captureRequested) return true;

    return (d->capture && captureReady() == false);
}

bool WSplatRenderer::isSettled() const
{
    Q_D(const WSplatRenderer); return d->isSettled();
}

WSplatRendererStats WSplatRenderer::stats() const
{
    Q_D(const WSplatRenderer); return d->stats;
}
