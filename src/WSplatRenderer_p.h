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

#ifndef WSPLATRENDERER_P_H
#define WSPLATRENDERER_P_H

/*  W A R N I N G
    -------------

    This file is not part of the Splat API. It exists purely as an
    implementation detail. This header file may change from version to
    version without notice, or even be removed.

    We mean it.
*/

// Qt includes
#include <QFuture>
#include <QElapsedTimer>
#include <QVector>
#include <rhi/qrhi.h>

// C++ includes
#include <functional>

// Splat includes
#include <WSplatRenderer.h>
#include <WSplatAccumulator.h>

//-------------------------------------------------------------------------------------------------
// WSplatDrawUniforms
//-------------------------------------------------------------------------------------------------

// NOTE: Mirrors the std140 'buf' block of splatUniforms.glsl (228 bytes).
struct WSplatDrawUniforms
{
    float projectionMatrix   [16];
    float clipSpaceCorrMatrix[16];

    float renderToViewQuat[4];
    float renderToViewPos [3];
    float maxStdDev;

    float renderSize[2];
    float minPixelRadius;
    float maxPixelRadius;

    float minAlpha;
    float blurAmount;
    float preBlurAmount;
    float focalDistance;

    float apertureAngle;
    float clipXY;
    float focalAdjustment;
    float falloff;

    qint32 isOrthographicValue;
    qint32 lodInflateValue;
    qint32 encodeLinearValue;
    qint32 disableFalloffValue;

    qint32 tightBoundsValue;
};

//-------------------------------------------------------------------------------------------------
// WSplatReadback
//-------------------------------------------------------------------------------------------------

// SPARK: readbackDepth
// NOTE: Reads back the first 'count' texels of a texture array into 'data', one request per array
//       layer (only the rows in use). The results must stay alive until their completed callbacks
//       ran, so the owner keeps the readback until isPending() is false.
struct WSplatReadback
{
    ~WSplatReadback();

    void request(QRhiResourceUpdateBatch * batch, QRhiTexture * texture,
                 const WSplatTextureSize & size, int count, int texelSize,
                 const std::function<void()> & completed = std::function<void()>());

    // NOTE: The readback without a stall, like Spark's, on the backends where QRhi blocks:
    //       Direct3D 11 (WSplatReadbackD3D11.cpp) and OpenGL (WSplatReadbackGL.cpp). Returns
    //       false on the others, which use request(). pollNative is called every frame,
    //       releaseNative drops the pending copies.
    bool requestNative(QRhi * rhi, QRhiCommandBuffer * cb, QRhiTexture * texture,
                       const WSplatTextureSize & size, int count, int texelSize,
                       const std::function<void()> & completed);

    void pollNative(QRhiCommandBuffer * cb);

    void releaseNative();

    bool requestD3D11(QRhi * rhi, QRhiCommandBuffer * cb, QRhiTexture * texture,
                      const WSplatTextureSize & size, int count, int texelSize);

    bool requestGL(QRhi * rhi, QRhiCommandBuffer * cb, QRhiTexture * texture,
                   const WSplatTextureSize & size, int count, int texelSize);

    bool pollD3D11();
    bool pollGL(QRhiCommandBuffer * cb);

    void releaseD3D11();
    void releaseGL();

    bool isPending() const;

    QVector<QRhiReadbackResult *> results;

    int pendingLayers = 0;

    QByteArray data;

    // NOTE: One copy per layer: a Direct3D 11 staging texture, or an OpenGL pixel buffer. The
    //       native objects are kept as plain values, out of this header.
    struct Native
    {
        void *  staging;
        quint32 buffer;

        int    layer;
        qint64 offset;
        int    rows;
    };

    QVector<Native> natives;

    QRhi * nativeRhi = NULL;

    void * nativeContext = NULL;
    void * nativeFence   = NULL;

    int nativeTexelSize = 0;

    std::function<void()> nativeCompleted;
};

//-------------------------------------------------------------------------------------------------
// WSplatSortJob
//-------------------------------------------------------------------------------------------------

// NOTE: One driveSort() cycle: the metric (GPU readback or CPU), then the sort on a worker thread.
struct WSplatSortJob
{
    int numSplats;
    int maxSplats;
    int mappingVersion;
    int orderingRows;

    // NOTE: The metric: maxSplats words of float bits.
    WSplatReadback metric;

    // NOTE: For the CPU sort metric, the splats and the transform of the accumulator.
    QSharedPointer<WPackedSplats> splats;

    WSplatAccumulateUniforms uniforms;

    bool fastSort;
    int  threads;

    // NOTE: The result, orderingRows * 16384 words.
    QByteArray ordering;

    int activeSplats;

    bool sortStarted;

    QFuture<void> future;

    QElapsedTimer timer;

    double metricMs;
    double sortMs;
};

//-------------------------------------------------------------------------------------------------
// WSplatCapture
//-------------------------------------------------------------------------------------------------

// NOTE: The displayed accumulator read back for splattest, see WSplatRenderer::requestCapture.
struct WSplatCapture
{
    WSplatReadback splats;
    WSplatReadback metric;

    QVector3D viewOrigin;

    QByteArray ordering;
};

//-------------------------------------------------------------------------------------------------
// WSplatRendererPrivate
//-------------------------------------------------------------------------------------------------

class WSplatRendererPrivate
{
public:
    WSplatRendererPrivate(WSplatRenderer * p);

    ~WSplatRendererPrivate();

    void init();

public: // Functions
    bool checkFeatures();

    void createResources();
    void createDrawPipeline(QRhiRenderPassDescriptor * renderPass);
    void createAccumulatePipeline();

    void releaseResources();

    void releaseTargets();

    void uploadSplats(QRhiResourceUpdateBatch * batch);

    void createAccumulateBindings();

    void updateMeshVersion();

    WSplatAccumulateUniforms accumulateUniforms() const;

    // SPARK: updateInternal
    void updateInternal(QRhiCommandBuffer * cb);

    // SPARK: driveSort, until the metric is requested.
    void driveSort(QRhiCommandBuffer * cb);

    // SPARK: driveSort, once the metric is available: starts the sort on a worker thread.
    void startSort(WSplatSortJob * job);

    // SPARK: driveSort, after the sort. Uploads the ordering into batch.
    void pollSort(QRhiResourceUpdateBatch * batch);

    void releaseJobs();

    void ensureOrdering(int rows);

    void updateDrawBindings(WSplatAccumulator * accumulator);

    void updateDrawUniforms(QRhiResourceUpdateBatch * batch, QRhiRenderTarget * renderTarget);

    void waitPending();

    bool isSettled() const;

    // NOTE: WSplatRendererCapture.cpp
    void startCapture(QRhiCommandBuffer * cb);

public: // Variables
    WSplatRenderer * q_ptr;

    QRhi * rhi;

    bool supported;

    // Inputs
    WSplatMesh          mesh;
    WSplatCamera        camera;
    WSplatRenderOptions options;

    // SPARK: SplatMesh versions
    QSharedPointer<WPackedSplats> splats;

    bool splatsDirty;

    int meshVersion;
    int meshMappingVersion;

    QVector3D   lastPosition;
    QQuaternion lastQuaternion;
    float       lastScale;
    QVector4D   lastRecolor;
    int         lastNumSh;
    QVector3D   lastViewToObject;

    // Source textures (PackedSplats.getTexture and the SH textures)
    QRhiTexture * packedTexture;
    QRhiTexture * sh1Texture;
    QRhiTexture * sh2Texture;
    QRhiTexture * sh3Texture;

    QRhiTexture * emptyRG;
    QRhiTexture * emptyRGBA;

    QRhiSampler * sampler;

    // NOTE: Uploads for the next render(), submitted before any pass.
    QRhiResourceUpdateBatch * uploads;

    // SPARK: SparkRenderer accumulators
    QVector<WSplatAccumulator *> pool;

    WSplatAccumulator * display;
    WSplatAccumulator * current;

    QVector<WSplatAccumulator *> accumulators;

    WSplatAccumulatePass accumulatePass;

    bool generated;

    // SPARK: SparkRenderer sorting
    bool sortDirty;
    bool sorting;

    QVector3D sortedCenter;
    QVector3D sortedDir;

    QElapsedTimer sortClock;
    qint64        lastSortTime;

    WSplatSortJob * job;

    QVector<WSplatSortJob *> jobs; // Done, waiting for their readback callbacks to be released.

    int activeSplats;
    int maxSplats;

    QRhiTexture * orderingTexture;
    int           orderingRows;

    QByteArray lastOrdering;

    // SPARK: readback32, reused from sort to sort.
    QByteArray readback32;

    // Draw
    QRhiRenderPassDescriptor * drawRenderPass;
    QRhiGraphicsPipeline     * drawPipeline;

    QRhiShaderResourceBindings * drawLayout;

    QRhiBuffer * quadVertices;
    QRhiBuffer * quadIndices;
    QRhiBuffer * drawUniforms;

    QShader accumulateVert;
    QShader accumulateFrag;
    QShader splatVert;
    QShader splatFrag;

    WSplatRendererStats stats;

    // Capture
    bool captureRequested;

    WSplatCapture * capture;

protected:
    Q_DECLARE_PUBLIC(WSplatRenderer)
};

#endif // WSPLATRENDERER_P_H
