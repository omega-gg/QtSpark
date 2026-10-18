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

// SPARK v2.3.1: src/Readback.ts, three.js WebGLRenderer.readRenderTargetPixelsAsync
//
// NOTE: The asynchronous readback of Spark on OpenGL and OpenGL ES 3.0, as three.js does it on
//       WebGL2: glReadPixels into a pixel pack buffer, a fence, the fence polled every frame
//       (probeAsync), then the buffer mapped. The Qt OpenGL backend completes a readback with a
//       blocking glReadPixels instead, which waits for the GPU and stalls the frame.
//
//       Every call runs between beginExternal and endExternal, so Qt's tracked GL state stays
//       valid. Qt binds no pixel pack buffer for its own readbacks, the binding is reset to 0.

#include "WSplatRenderer.h"

// Qt includes
#include <rhi/qrhi.h>

#if QT_CONFIG(opengl)
#include <QOpenGLContext>
#include <QOpenGLExtraFunctions>
#endif

// Private includes
#include "WSplatRenderer_p.h"

#if QT_CONFIG(opengl)

//-------------------------------------------------------------------------------------------------
// WSplatReadback
//-------------------------------------------------------------------------------------------------

bool WSplatReadback::requestGL(QRhi *, QRhiCommandBuffer * cb, QRhiTexture * texture,
                               const WSplatTextureSize & size, int count, int texelSize)
{
    // NOTE: The metric target is RGBA8, read as GL_RGBA / GL_UNSIGNED_BYTE.
    GLuint source = GLuint(texture->nativeTexture().object);

    if (texelSize != 4 || source == 0) return false;

    // NOTE: Runs the commands recorded so far (the accumulator pass), so the reads come after.
    cb->beginExternal();

    QOpenGLExtraFunctions * f = QOpenGLContext::currentContext()->extraFunctions();

    GLint alignment = 4;

    f->glGetIntegerv(GL_PACK_ALIGNMENT, &alignment);

    f->glPixelStorei(GL_PACK_ALIGNMENT, 4);

    GLuint framebuffer = 0;

    f->glGenFramebuffers(1, &framebuffer);

    f->glBindFramebuffer(GL_READ_FRAMEBUFFER, framebuffer);

    int layerSize = SPLAT_TEX_WIDTH * SPLAT_TEX_HEIGHT;

    int baseIndex = 0;

    bool complete = true;

    // NOTE: The layers of WSplatReadback::request, with one pixel buffer each.
    while (baseIndex < count)
    {
        int layer     = baseIndex / layerSize;
        int layerBase = layer * layerSize;

        int layerYEnd = qMin(size.height,
                             (count - layerBase + SPLAT_TEX_WIDTH - 1) / SPLAT_TEX_WIDTH);

        f->glFramebufferTextureLayer(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, source, 0, layer);

        f->glReadBuffer(GL_COLOR_ATTACHMENT0);

        if (f->glCheckFramebufferStatus(GL_READ_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        {
            qWarning("WSplatReadback: Cannot read the metric layer %d.", layer);

            complete = false;

            break;
        }

        GLuint buffer = 0;

        f->glGenBuffers(1, &buffer);

        f->glBindBuffer(GL_PIXEL_PACK_BUFFER, buffer);

        f->glBufferData(GL_PIXEL_PACK_BUFFER, GLsizeiptr(SPLAT_TEX_WIDTH) * layerYEnd * texelSize,
                        NULL, GL_STREAM_READ);

        f->glReadPixels(0, 0, SPLAT_TEX_WIDTH, layerYEnd, GL_RGBA, GL_UNSIGNED_BYTE, NULL);

        Native native;

        native.staging = NULL;
        native.buffer  = buffer;
        native.layer   = layer;
        native.offset  = qint64(layerBase) * texelSize;
        native.rows    = layerYEnd;

        natives.append(native);

        pendingLayers++;

        baseIndex += SPLAT_TEX_WIDTH * layerYEnd;
    }

    f->glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);

    f->glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);

    f->glDeleteFramebuffers(1, &framebuffer);

    f->glPixelStorei(GL_PACK_ALIGNMENT, alignment);

    if (complete)
    {
        nativeFence = f->glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);

        nativeTexelSize = texelSize;

        f->glFlush();
    }
    else releaseGL();

    cb->endExternal();

    return complete;
}

// NOTE: Called every frame on the render thread, like three.js probeAsync. Returns true once
//       every read is in.
bool WSplatReadback::pollGL(QRhiCommandBuffer * cb)
{
    cb->beginExternal();

    QOpenGLExtraFunctions * f = QOpenGLContext::currentContext()->extraFunctions();

    GLenum status = f->glClientWaitSync(GLsync(nativeFence), 0, 0);

    if (status == GL_TIMEOUT_EXPIRED)
    {
        cb->endExternal();

        return false;
    }

    if (status == GL_WAIT_FAILED) qWarning("WSplatReadback: The readback fence failed.");

    qint64 rowBytes = qint64(SPLAT_TEX_WIDTH) * nativeTexelSize;

    for (const Native & native : natives)
    {
        if (status == GL_WAIT_FAILED) break;

        qint64 bytes = qint64(native.rows) * rowBytes;

        f->glBindBuffer(GL_PIXEL_PACK_BUFFER, native.buffer);

        const void * source = f->glMapBufferRange(GL_PIXEL_PACK_BUFFER, 0, GLsizeiptr(bytes),
                                                  GL_MAP_READ_BIT);

        if (source)
        {
            memcpy(data.data() + native.offset, source,
                   qMin(bytes, data.size() - native.offset));

            f->glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
        }
        else qWarning("WSplatReadback: Failed to map a pixel buffer.");
    }

    f->glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);

    releaseGL();

    cb->endExternal();

    return true;
}

// NOTE: Deleting unbound objects leaves the GL state as Qt tracks it.
void WSplatReadback::releaseGL()
{
    if (nativeRhi) nativeRhi->makeThreadLocalNativeContextCurrent();

    QOpenGLContext * context = QOpenGLContext::currentContext();

    if (context)
    {
        QOpenGLExtraFunctions * f = context->extraFunctions();

        for (const Native & native : natives)
        {
            f->glDeleteBuffers(1, &native.buffer);
        }

        if (nativeFence) f->glDeleteSync(GLsync(nativeFence));
    }

    pendingLayers -= natives.count();

    natives.clear();

    nativeFence = NULL;
}

#else

//-------------------------------------------------------------------------------------------------
// WSplatReadback
//-------------------------------------------------------------------------------------------------

bool WSplatReadback::requestGL(QRhi *, QRhiCommandBuffer *, QRhiTexture *,
                               const WSplatTextureSize &, int, int)
{
    return false;
}

bool WSplatReadback::pollGL(QRhiCommandBuffer *)
{
    return true;
}

void WSplatReadback::releaseGL() {}

#endif
