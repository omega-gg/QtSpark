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

#ifndef WSPLATRENDERER_H
#define WSPLATRENDERER_H

// Qt includes
#include <QSharedPointer>
#include <QMatrix4x4>
#include <QQuaternion>
#include <QVector4D>
#include <QColor>
#include <QScopedPointer>

// Splat includes
#include <Splat.h>

// Forward declarations
class QRhi;
class QRhiCommandBuffer;
class QRhiRenderTarget;
class WPackedSplats;
class WSplatRendererPrivate;

//-------------------------------------------------------------------------------------------------
// WSplatMesh
//-------------------------------------------------------------------------------------------------

// NOTE: The SplatMesh properties the renderer reads. The scale is uniform, as in Spark where the
//       SplatTransformer averages the three axes.
struct WSplatMesh
{
    QSharedPointer<WPackedSplats> splats;

    QVector3D   position;
    QQuaternion quaternion;

    float scale = 1.0f;

    // NOTE: SplatMesh.recolor (rgb) and SplatMesh.opacity (w).
    QVector4D recolor = QVector4D(1.0f, 1.0f, 1.0f, 1.0f);

    // NOTE: SplatMesh.maxSh
    int maxSh = 3;
};

//-------------------------------------------------------------------------------------------------
// WSplatCamera
//-------------------------------------------------------------------------------------------------

struct WSplatCamera
{
    // NOTE: THREE.Camera.matrixWorld, the camera to world transform (looking down -Z).
    QMatrix4x4 matrixWorld;

    // NOTE: THREE.Camera.projectionMatrix, in OpenGL convention.
    QMatrix4x4 projectionMatrix;

    bool isOrthographic = false;
};

//-------------------------------------------------------------------------------------------------
// WSplatRenderOptions
//-------------------------------------------------------------------------------------------------

// NOTE: SparkRendererOptions with the upstream defaults.
struct WSplatRenderOptions
{
    float maxStdDev      = 2.8284271247461903f; // Math.sqrt(8)
    float minPixelRadius = 0.0f;
    float maxPixelRadius = 512.0f;
    float minAlpha       = 0.5f / 255.0f;

    bool enable2DGS = false;

    float preBlurAmount = 0.0f;
    float blurAmount    = 0.3f;

    float focalDistance = 0.0f;
    float apertureAngle = 0.0f;

    float falloff = 1.0f;
    float clipXY  = 1.4f;

    float focalAdjustment = 1.0f;

    bool lodInflate = false;
    bool sortRadial = true;

    int minSortIntervalMs = 0;

    // NOTE: Splat performance options, off by default: the defaults are the exact Spark code
    //       path. Each option keeps the image, see implementation.md "Performance options".

    // NOTE: Empty quad culling: quads whose bounds hold no pixel center are not rasterized (most
    //       splats of large scenes are below a pixel). The output is bit identical.
    bool tightBounds = false;

    // NOTE: The sort metric is computed on the CPU from the splat centers, with the transform of
    //       the accumulator, instead of being read back from the GPU. The sort starts at once and
    //       the GPU readback stall (OpenGL, Direct3D 11) is gone. The order is the same.
    bool cpuSortMetric = false;

    // NOTE: A faster radix sort (11 bit digits, several threads) and a multithreaded CPU metric.
    //       The ordering is identical to the upstream sort.
    bool fastSort = false;
};

//-------------------------------------------------------------------------------------------------
// WSplatRendererStats
//-------------------------------------------------------------------------------------------------

struct WSplatRendererStats
{
    int numSplats    = 0;
    int activeSplats = 0;

    int generates = 0;
    int sorts     = 0;

    // NOTE: Timings in milliseconds of the last sort cycle: the metric latency (GPU readback
    //       request to completion, or the CPU metric) and the CPU sort.
    double metricMs = 0.0;
    double sortMs     = 0.0;

    // NOTE: GPU time of the last completed frame, filled by the caller (QRhi timestamps).
    double gpuMs = 0.0;
};

//-------------------------------------------------------------------------------------------------
// WSplatRenderer
//-------------------------------------------------------------------------------------------------

class SPLAT_EXPORT WSplatRenderer
{
public:
    WSplatRenderer();

    ~WSplatRenderer();

public: // Interface
    // NOTE: Inputs, set before render(). In a QQuickRhiItem they are set from synchronize().
    void setMesh   (const WSplatMesh          & mesh);
    void setCamera (const WSplatCamera        & camera);
    void setOptions(const WSplatRenderOptions & options);

    // NOTE: Creates the pipelines for the render target's render pass. Call it whenever the
    //       render target changes. Safe to call every frame.
    bool initialize(QRhi * rhi, QRhiRenderTarget * renderTarget);

    // NOTE: Records the accumulator passes when the view changed, drives the readback and sort,
    //       then records the main pass into renderTarget, cleared with clearColor (premultiplied).
    void render(QRhiCommandBuffer * cb, QRhiRenderTarget * renderTarget,
                const QColor & clearColor);

    void releaseResources();

    // NOTE: True while a generate, readback or sort is pending. The caller must keep rendering
    //       frames until it returns false, otherwise the sort never completes.
    bool needsUpdate() const;

    // NOTE: True when the displayed frame matches the latest sorted view (no pending work).
    bool isSettled() const;

    WSplatRendererStats stats() const;

public: // Debug (WSplatRendererCapture.cpp)
    // NOTE: Requests a readback of the displayed accumulator (splats and metric) once the
    //       renderer is settled. Used by splattest to compare against Spark.
    void requestCapture();

    bool captureReady() const;

    // NOTE: numSplats * 4 words, numSplats words, activeSplats words.
    QByteArray capturedSplats  () const;
    QByteArray capturedMetric  () const;
    QByteArray capturedOrdering() const;

    QVector3D capturedViewOrigin() const;

private:
    Q_DISABLE_COPY(WSplatRenderer)

    QScopedPointer<WSplatRendererPrivate> d_ptr;

    Q_DECLARE_PRIVATE(WSplatRenderer)
};

#endif // WSPLATRENDERER_H
