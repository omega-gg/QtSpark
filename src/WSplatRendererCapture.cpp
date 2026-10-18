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

// NOTE: The capture of the displayed accumulator, used by splattest to compare against Spark.

#include "WSplatRenderer.h"

// Qt includes
#include <rhi/qrhi.h>

// Private includes
#include "WSplatRenderer_p.h"

//-------------------------------------------------------------------------------------------------
// Private functions
//-------------------------------------------------------------------------------------------------

void WSplatRendererPrivate::startCapture(QRhiCommandBuffer * cb)
{
    captureRequested = false;

    delete capture;

    capture = new WSplatCapture;

    WSplatAccumulator * accumulator = display;

    int numSplats = accumulator->numSplats;

    capture->viewOrigin = accumulator->viewOrigin;
    capture->ordering   = lastOrdering.left(qint64(activeSplats) * sizeof(quint32));

    capture->splats.data = QByteArray(qint64(numSplats) * 4 * sizeof(quint32), 0);
    capture->metric.data = QByteArray(qint64(numSplats)     * sizeof(quint32), 0);

    if (accumulator->target == NULL || numSplats == 0) return;

    QRhiResourceUpdateBatch * batch = rhi->nextResourceUpdateBatch();

    capture->splats.request(batch, accumulator->target, accumulator->size, numSplats,
                            4 * sizeof(quint32));

    capture->metric.request(batch, accumulator->target3, accumulator->size, numSplats,
                            sizeof(quint32));

    cb->resourceUpdate(batch);
}

//-------------------------------------------------------------------------------------------------
// Debug
//-------------------------------------------------------------------------------------------------

void WSplatRenderer::requestCapture()
{
    Q_D(WSplatRenderer);

    delete d->capture;

    d->capture = NULL;

    d->captureRequested = true;
}

bool WSplatRenderer::captureReady() const
{
    Q_D(const WSplatRenderer);

    return (d->capture
            &&
            d->capture->splats.isPending() == false
            &&
            d->capture->metric.isPending() == false);
}

QByteArray WSplatRenderer::capturedSplats() const
{
    Q_D(const WSplatRenderer);

    if (d->capture) return d->capture->splats.data;
    else            return QByteArray();
}

QByteArray WSplatRenderer::capturedMetric() const
{
    Q_D(const WSplatRenderer);

    if (d->capture) return d->capture->metric.data;
    else            return QByteArray();
}

QByteArray WSplatRenderer::capturedOrdering() const
{
    Q_D(const WSplatRenderer);

    if (d->capture) return d->capture->ordering;
    else            return QByteArray();
}

QVector3D WSplatRenderer::capturedViewOrigin() const
{
    Q_D(const WSplatRenderer);

    if (d->capture) return d->capture->viewOrigin;
    else            return QVector3D();
}
