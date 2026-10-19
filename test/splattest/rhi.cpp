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
*/
//=================================================================================================

// NOTE: The offscreen QRhi, its render target and the frame loop.

#include "splattest.h"

// Qt includes
#include <QElapsedTimer>
#include <QImage>
#include <QOffscreenSurface>

//-------------------------------------------------------------------------------------------------
// Rhi
//-------------------------------------------------------------------------------------------------

bool createRhi(Context & context, const QString & backend, bool timestamps)
{
    context.backend = backend;

    QRhi::Flags flags;

    if (timestamps) flags |= QRhi::EnableTimestamps;

    if (backend == "gl")
    {
        QSurfaceFormat format;

        format.setVersion(4, 3);

        format.setProfile(QSurfaceFormat::CoreProfile);

        context.surface.reset(QRhiGles2InitParams::newFallbackSurface(format));

        QRhiGles2InitParams params;

        params.format          = format;
        params.fallbackSurface = context.surface.get();

        context.rhi = QRhi::create(QRhi::OpenGLES2, &params, flags);
    }
#if QT_CONFIG(vulkan) && __has_include(<vulkan/vulkan.h>)
    else if (backend == "vulkan")
    {
        context.vulkan.reset(new QVulkanInstance);

        context.vulkan->setExtensions(QRhiVulkanInitParams::preferredInstanceExtensions());

        if (context.vulkan->create() == false)
        {
            qWarning("Cannot create the Vulkan instance (VkResult %d).",
                     int(context.vulkan->errorCode()));

            return false;
        }

        QRhiVulkanInitParams params;

        params.inst = context.vulkan.get();

        context.rhi = QRhi::create(QRhi::Vulkan, &params, flags);
    }
#endif
#ifdef Q_OS_WIN
    else if (backend == "d3d11")
    {
        QRhiD3D11InitParams params;

        context.rhi = QRhi::create(QRhi::D3D11, &params, flags);
    }
    else if (backend == "d3d12")
    {
        QRhiD3D12InitParams params;

        context.rhi = QRhi::create(QRhi::D3D12, &params, flags);
    }
#endif

    return (context.rhi != NULL);
}

bool createTarget(QRhi * rhi, Target & target, const QSize & size)
{
    target.texture = rhi->newTexture(QRhiTexture::RGBA8, size, 1,
                                     QRhiTexture::RenderTarget
                                     |
                                     QRhiTexture::UsedAsTransferSource);

    if (target.texture->create() == false) return false;

    target.target = rhi->newTextureRenderTarget({ target.texture });

    target.renderPass = target.target->newCompatibleRenderPassDescriptor();

    target.target->setRenderPassDescriptor(target.renderPass);

    return target.target->create();
}

//-------------------------------------------------------------------------------------------------
// Rendering
//-------------------------------------------------------------------------------------------------

bool renderFrame(Context & context, WSplatRenderer & renderer, Target & target,
                 QRhiReadbackResult * readback)
{
    QRhiCommandBuffer * cb;

    if (context.rhi->beginOffscreenFrame(&cb) != QRhi::FrameOpSuccess) return false;

    // NOTE: THREE.WebGLRenderer clears to opaque black by default.
    renderer.render(cb, target.target, QColor(0, 0, 0, 255));

    if (readback)
    {
        QRhiResourceUpdateBatch * batch = context.rhi->nextResourceUpdateBatch();

        batch->readBackTexture({ target.texture }, readback);

        cb->resourceUpdate(batch);
    }

    context.rhi->endOffscreenFrame();

    return true;
}

QImage renderOptions(Context & context, const WSplatMesh & mesh,
                     const WSplatCamera & camera, Target & target,
                     const WSplatRenderOptions & options)
{
    WSplatRenderer renderer;

    renderer.setMesh   (mesh);
    renderer.setCamera (camera);
    renderer.setOptions(options);

    renderer.initialize(context.rhi, target.target);

    settle(context, renderer, target);

    return readImage(context, renderer, target);
}

// NOTE: Renders until the renderer is settled (sorted for the current view), like the Spark
//       harness settle().
bool settle(Context & context, WSplatRenderer & renderer, Target & target)
{
    // NOTE: Time based, an empty frame can be much faster than the sort.
    QElapsedTimer timer;

    timer.start();

    while (timer.elapsed() < 30000)
    {
        if (renderFrame(context, renderer, target) == false) return false;

        if (renderer.isSettled() && renderer.needsUpdate() == false) return true;
    }

    return false;
}

QImage readImage(Context & context, WSplatRenderer & renderer, Target & target)
{
    QRhiReadbackResult result;

    renderFrame(context, renderer, target, &result);

    QImage image(reinterpret_cast<const uchar *> (result.data.constData()),
                 result.pixelSize.width(), result.pixelSize.height(),
                 QImage::Format_RGBA8888);

    // NOTE: The OpenGL readback is bottom up.
    if (context.rhi->isYUpInFramebuffer()) return image.flipped(Qt::Vertical);
    else                                   return image.copy();
}
