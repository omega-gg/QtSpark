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
// NOTE: The asynchronous readback of Spark on Direct3D 11. WebGL2 copies the pixels into a
//       buffer, sets a fence and polls it every frame (three.js probeAsync), so the readback never
//       blocks. The Qt D3D11 backend completes a readback with a blocking Map at the next frame
//       instead. The CPU runs a few frames ahead, so that Map waits for all of them and the
//       frame rate stutters. Here the copy goes into staging textures through the native device,
//       and Map is polled with D3D11_MAP_FLAG_DO_NOT_WAIT.

#include "WSplatRenderer.h"

// Qt includes
#include <rhi/qrhi.h>
#include <rhi/qrhi_platform.h>

// Private includes
#include "WSplatRenderer_p.h"

#ifdef Q_OS_WIN

// Windows includes
#include <d3d11.h>

//-------------------------------------------------------------------------------------------------
// WSplatReadback
//-------------------------------------------------------------------------------------------------

bool WSplatReadback::requestNative(QRhi * rhi, QRhiCommandBuffer * cb, QRhiTexture * texture,
                                   const WSplatTextureSize & size, int count, int texelSize,
                                   const std::function<void()> & completed)
{
    if (rhi->backend() != QRhi::D3D11) return false;

    const QRhiD3D11NativeHandles * handles
        = static_cast<const QRhiD3D11NativeHandles *> (rhi->nativeHandles());

    ID3D11Device        * device  = static_cast<ID3D11Device        *> (handles->dev);
    ID3D11DeviceContext * context = static_cast<ID3D11DeviceContext *> (handles->context);

    ID3D11Texture2D * source = reinterpret_cast<ID3D11Texture2D *>
                               (quintptr(texture->nativeTexture().object));

    if (device == NULL || context == NULL || source == NULL) return false;

    D3D11_TEXTURE2D_DESC sourceDescription;

    source->GetDesc(&sourceDescription);

    int layerSize = SPLAT_TEX_WIDTH * SPLAT_TEX_HEIGHT;

    int baseIndex = 0;

    // NOTE: The layers of WSplatReadback::request, with one staging texture each. They are all
    //       created before any copy, so a failure falls back to the QRhi readback.
    while (baseIndex < count)
    {
        int layer     = baseIndex / layerSize;
        int layerBase = layer * layerSize;

        int layerYEnd = qMin(size.height,
                             (count - layerBase + SPLAT_TEX_WIDTH - 1) / SPLAT_TEX_WIDTH);

        D3D11_TEXTURE2D_DESC description = {};

        description.Width            = SPLAT_TEX_WIDTH;
        description.Height           = layerYEnd;
        description.MipLevels        = 1;
        description.ArraySize        = 1;
        description.Format           = sourceDescription.Format;
        description.SampleDesc.Count = 1;
        description.Usage            = D3D11_USAGE_STAGING;
        description.CPUAccessFlags   = D3D11_CPU_ACCESS_READ;

        ID3D11Texture2D * staging = NULL;

        if (FAILED(device->CreateTexture2D(&description, NULL, &staging)))
        {
            qWarning("WSplatReadback: Failed to create a staging texture.");

            releaseNative();

            return false;
        }

        Native native;

        native.staging = staging;
        native.layer   = layer;
        native.offset  = qint64(layerBase) * texelSize;
        native.rows    = layerYEnd;

        natives.append(native);

        pendingLayers++;

        baseIndex += SPLAT_TEX_WIDTH * layerYEnd;
    }

    nativeContext   = context;
    nativeTexelSize = texelSize;
    nativeCompleted = completed;

    // NOTE: Runs the commands recorded so far (the accumulator pass), so the copies come after.
    cb->beginExternal();

    for (const Native & native : natives)
    {
        D3D11_BOX box = { 0, 0, 0, UINT(SPLAT_TEX_WIDTH), UINT(native.rows), 1 };

        context->CopySubresourceRegion(static_cast<ID3D11Texture2D *> (native.staging), 0, 0, 0,
                                       0, source, D3D11CalcSubresource(0, native.layer, 1), &box);
    }

    // NOTE: Submits the copies, so the polls see them complete.
    context->Flush();

    cb->endExternal();

    return true;
}

// NOTE: Called every frame on the render thread, like three.js probeAsync.
void WSplatReadback::pollNative(bool wait)
{
    if (natives.isEmpty()) return;

    ID3D11DeviceContext * context = static_cast<ID3D11DeviceContext *> (nativeContext);

    UINT flags = (wait) ? 0 : D3D11_MAP_FLAG_DO_NOT_WAIT;

    int rowBytes = SPLAT_TEX_WIDTH * nativeTexelSize;

    for (int i = natives.count() - 1; i >= 0; i--)
    {
        const Native & native = natives.at(i);

        ID3D11Texture2D * staging = static_cast<ID3D11Texture2D *> (native.staging);

        D3D11_MAPPED_SUBRESOURCE mapped;

        HRESULT result = context->Map(staging, 0, D3D11_MAP_READ, flags, &mapped);

        if (result == DXGI_ERROR_WAS_STILL_DRAWING) continue;

        if (SUCCEEDED(result))
        {
            const char * source = static_cast<const char *> (mapped.pData);

            for (int row = 0; row < native.rows; row++)
            {
                qint64 offset = native.offset + qint64(row) * rowBytes;

                if (offset + rowBytes > data.size()) break;

                memcpy(data.data() + offset, source + qint64(row) * mapped.RowPitch, rowBytes);
            }

            context->Unmap(staging, 0);
        }
        else qWarning("WSplatReadback: Failed to map a staging texture.");

        staging->Release();

        natives.removeAt(i);

        pendingLayers--;
    }

    if (pendingLayers == 0 && nativeCompleted)
    {
        std::function<void()> completed = nativeCompleted;

        nativeCompleted = std::function<void()>();

        completed();
    }
}

void WSplatReadback::releaseNative()
{
    for (const Native & native : natives)
    {
        static_cast<ID3D11Texture2D *> (native.staging)->Release();

        pendingLayers--;
    }

    natives.clear();

    nativeCompleted = std::function<void()>();
}

#else

//-------------------------------------------------------------------------------------------------
// WSplatReadback
//-------------------------------------------------------------------------------------------------

bool WSplatReadback::requestNative(QRhi *, QRhiCommandBuffer *, QRhiTexture *,
                                   const WSplatTextureSize &, int, int,
                                   const std::function<void()> &)
{
    return false;
}

void WSplatReadback::pollNative(bool) {}

void WSplatReadback::releaseNative() {}

#endif
