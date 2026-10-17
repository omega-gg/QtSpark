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

#ifndef WSPLATVIEW_P_H
#define WSPLATVIEW_P_H

/*  W A R N I N G
    -------------

    This file is not part of the Splat API. It exists purely as an
    implementation detail. This header file may change from version to
    version without notice, or even be removed.

    We mean it.
*/

// Qt includes
#include <QFutureWatcher>
#include <QElapsedTimer>
#include <QPointer>
#include <QMutex>
#include <QTimer>

// Splat includes
#include <WSplatView.h>
#include <WSplatRenderer.h>
#include <WSplatControls.h>

// Forward declarations
class QQuickWindow;

//-------------------------------------------------------------------------------------------------
// WSplatLoadResult
//-------------------------------------------------------------------------------------------------

struct WSplatLoadResult
{
    QSharedPointer<WPackedSplats> splats;

    QString error;

    // NOTE: A robust bounding sphere of the splat centers (object space), used by resetCamera.
    QVector3D center;
    float     radius = 0.0f;

    double loadMs = 0.0;
};

//-------------------------------------------------------------------------------------------------
// WSplatViewStats
//-------------------------------------------------------------------------------------------------

// NOTE: Shared by the item and its renderer: written by the render thread at the end of each
//       frame, read by the GUI thread. The renderer can outlive the item, hence the shared
//       pointer.
struct WSplatViewStats
{
    QMutex mutex;

    WSplatRendererStats renderer;

    qint64 frames = 0;

    QString backendName;
};

//-------------------------------------------------------------------------------------------------
// WSplatViewPrivate
//-------------------------------------------------------------------------------------------------

class WSplatViewPrivate
{
public:
    WSplatViewPrivate(WSplatView * p);

    void init();

public: // Functions
    void setStatus(WSplatView::Status status, const QString & error = QString());

    QQuaternion meshQuaternion() const;

    void startAnimation();

public: // Slots
    void onLoaded();
    void onAnimate();
    void onStats();

public: // Variables
    WSplatView * q_ptr;

    QUrl source;

    WSplatView::Status status;

    QString error;

    QFutureWatcher<WSplatLoadResult> watcher;

    QSharedPointer<WPackedSplats> splats;

    QVector3D boundsCenter;
    float     boundsRadius;

    double loadMs;

    QColor backgroundColor;

    WSplatView::Orientation orientation;

    WSplatControls       controls;
    WSplatControlsObject camera;

    qreal fov;
    qreal zNear;
    qreal zFar;

    bool controlsEnabled;

    WSplatRenderOptions options;

    int shDegree;

    QPointer<QQuickWindow> window;

    QElapsedTimer animationTimer;
    qint64        animationLast;

    bool animating;

    QSharedPointer<WSplatViewStats> stats;

    // NOTE: The copy the properties read, refreshed by the stats timer (GUI thread).
    WSplatRendererStats statsSnapshot;

    QString backendName;

    QTimer statsTimer;

    // NOTE: Requests a frame once the camera stopped, see WSplatViewRenderer::synchronize.
    QTimer motionTimer;

    qint64 statsFrames;
    qint64 statsTime;

    qreal fps;

protected:
    Q_DECLARE_PUBLIC(WSplatView)
};

#endif // WSPLATVIEW_P_H
