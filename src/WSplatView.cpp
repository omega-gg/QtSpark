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

#include "WSplatView.h"

// Qt includes
#include <QQuickWindow>
#include <QScreen>
#include <QtConcurrent>
#include <rhi/qrhi.h>

// Splat includes
#include <WPackedSplats.h>
#include <WSplatLoader.h>

// C++ includes
#include <cmath>

// Private includes
#include "WSplatView_p.h"

//-------------------------------------------------------------------------------------------------
// Static variables

// SPARK: examples/viewer, the camera starts at (0, 0, 1) looking down -Z.
static const QVector3D CAMERA_START(0.0f, 0.0f, 1.0f);

//-------------------------------------------------------------------------------------------------
// Static functions

static QString urlToPath(const QUrl & url)
{
    if (url.isLocalFile()) return url.toLocalFile();

    if (url.scheme() == "qrc") return ':' + url.path();

    return url.toString();
}

// NOTE: Canceling the future aborts the decode within a batch, see WPackedSplats::canceled.
static void loadSplats(QPromise<WSplatLoadResult> & promise, const QString & fileName)
{
    WSplatLoadResult result;

    QElapsedTimer timer;

    timer.start();

    result.splats = WSplatLoader::loadPackedSplats(fileName, &result.error,
                                                   [&promise]() { return promise.isCanceled(); });

    result.loadMs = timer.nsecsElapsed() / 1000000.0;

    if (promise.isCanceled()) return;

    promise.addResult(result);
}

//-------------------------------------------------------------------------------------------------
// WSplatViewRenderer
//-------------------------------------------------------------------------------------------------

class WSplatViewRenderer : public QQuickRhiItemRenderer
{
public: // QQuickRhiItemRenderer implementation
    /* virtual */ void initialize(QRhiCommandBuffer * cb);

    /* virtual */ void synchronize(QQuickRhiItem * item);

    /* virtual */ void render(QRhiCommandBuffer * cb);

public: // Variables
    WSplatRenderer renderer;

    QMatrix4x4 matrixWorld;

    bool moved = false;

    qreal fov;
    qreal zNear;
    qreal zFar;

    QColor clearColor;

    qint64 frames = 0;

    QSharedPointer<WSplatViewStats> stats;

    bool continuous = false;
};

/* virtual */ void WSplatViewRenderer::initialize(QRhiCommandBuffer *)
{
    renderer.initialize(rhi(), renderTarget());
}

/* virtual */ void WSplatViewRenderer::synchronize(QQuickRhiItem * item)
{
    WSplatViewPrivate * d = static_cast<WSplatView *> (item)->d_func();

    WSplatMesh mesh;

    mesh.splats     = d->splats;
    mesh.quaternion = d->meshQuaternion();
    mesh.maxSh      = d->shDegree;

    renderer.setMesh(mesh);

    renderer.setOptions(d->options);

    QMatrix4x4 previous = matrixWorld;

    matrixWorld.setToIdentity();

    matrixWorld.translate(d->camera.position);
    matrixWorld.rotate   (d->camera.quaternion);

    // NOTE: While the camera moves, whoever moves it requests the frames. The timer restarts on
    //       every move and requests one frame once they stop, so a pending sort still completes.
    //       Queued, since the timer lives on the GUI thread.
    moved = (matrixWorld != previous);

    if (moved) QMetaObject::invokeMethod(&(d->motionTimer), "start", Qt::QueuedConnection);

    fov   = d->fov;
    zNear = d->zNear;
    zFar  = d->zFar;

    // NOTE: Qt Quick expects premultiplied alpha.
    const QColor & color = d->backgroundColor;

    float alpha = color.alphaF();

    clearColor = QColor::fromRgbF(color.redF() * alpha, color.greenF() * alpha,
                                  color.blueF() * alpha, alpha);

    stats = d->stats;

    continuous = d->continuous;
}

/* virtual */ void WSplatViewRenderer::render(QRhiCommandBuffer * cb)
{
    QRhiRenderTarget * target = renderTarget();

    QSize size = target->pixelSize();

    WSplatCamera camera;

    camera.matrixWorld = matrixWorld;

    // NOTE: THREE.PerspectiveCamera, vertical field of view in degrees, OpenGL convention.
    camera.projectionMatrix.perspective(fov, float(size.width()) / qMax(1, size.height()),
                                        zNear, zFar);

    renderer.setCamera(camera);

    renderer.render(cb, target, clearColor);

    frames++;

    if (stats)
    {
        QMutexLocker locker(&(stats->mutex));

        stats->renderer    = renderer.stats();
        stats->frames      = frames;
        stats->backendName = rhi()->backendName();

        // NOTE: Zero unless the window enables timestamps.
        stats->renderer.gpuMs = cb->lastCompletedGpuTime() * 1000.0;
    }

    // NOTE: The readback and sort complete over the next frames, keep them coming, and so does
    //       continuous mode. Not while the camera moves: the next frame is already requested
    //       along with its movement, and one more would render the same pose twice.
    if (moved == false && (continuous || renderer.needsUpdate())) update();
}

//-------------------------------------------------------------------------------------------------
// Private
//-------------------------------------------------------------------------------------------------

WSplatViewPrivate::WSplatViewPrivate(WSplatView * p) : q_ptr(p) {}

void WSplatViewPrivate::init()
{
    Q_Q(WSplatView);

    status = WSplatView::Null;


    loadMs = 0.0;

    backgroundColor = Qt::black;

    orientation = WSplatView::OpenCV;

    // NOTE: THREE.PerspectiveCamera defaults of the Spark examples.
    fov   = 60.0;
    zNear = 0.01;
    zFar  = 1000.0;

    controlsEnabled = true;

    controlMode = WSplatView::Fly;

    shDegree = 3;

    animationLast = 0;

    animating = false;

    continuous = false;

    statsFrames = 0;
    statsTime   = 0;

    fps = 0.0;

    stats = QSharedPointer<WSplatViewStats>(new WSplatViewStats);

    camera.position = CAMERA_START;

    animationTimer.start();

    q->setFlag(QQuickItem::ItemHasContents);

    q->setAcceptedMouseButtons(Qt::AllButtons);

    q->setActiveFocusOnTab(true);

    QObject::connect(&watcher, SIGNAL(finished()), q, SLOT(onLoaded()));

    statsTimer.setInterval(500);

    QObject::connect(&statsTimer, SIGNAL(timeout()), q, SLOT(onStats()));

    statsTimer.start();

    motionTimer.setSingleShot(true);

    motionTimer.setInterval(100);

    QObject::connect(&motionTimer, SIGNAL(timeout()), q, SLOT(update()));
}

//-------------------------------------------------------------------------------------------------
// Private functions
//-------------------------------------------------------------------------------------------------

void WSplatViewPrivate::setStatus(WSplatView::Status status, const QString & error)
{
    Q_Q(WSplatView);

    if (this->status == status && this->error == error) return;

    this->status = status;
    this->error  = error;

    emit q->statusChanged();
}

QQuaternion WSplatViewPrivate::meshQuaternion() const
{
    // NOTE: QQuaternion takes the scalar (w) first.
    if (orientation == WSplatView::OpenCV)
    {
        // SPARK: splatMesh.quaternion.set(1, 0, 0, 0)
        return QQuaternion(0.0f, 1.0f, 0.0f, 0.0f);
    }
    else if (orientation == WSplatView::ZUp)
    {
        // SPARK: examples/editor "Z-up", quaternion (-1, 0, 0, 1) normalized.
        return QQuaternion(1.0f, -1.0f, 0.0f, 0.0f).normalized();
    }
    else return QQuaternion();
}

void WSplatViewPrivate::startAnimation()
{
    Q_Q(WSplatView);

    if (animating == false)
    {
        animating = true;

        animationLast = animationTimer.nsecsElapsed();
    }

    q->update();
}

//-------------------------------------------------------------------------------------------------
// Private slots
//-------------------------------------------------------------------------------------------------

void WSplatViewPrivate::onLoaded()
{
    Q_Q(WSplatView);

    // NOTE: The source was cleared while loading: the load was canceled, it has no result.
    if (status != WSplatView::Loading || watcher.future().resultCount() == 0) return;

    WSplatLoadResult result = watcher.result();

    if (result.splats.isNull())
    {
        setStatus(WSplatView::Error, result.error);

        return;
    }

    splats = result.splats;

    loadMs = result.loadMs;

    emit q->splatCountChanged();

    // NOTE: Each file starts from the start pose, like opening it in Spark's viewer.
    q->resetCamera();

    setStatus(WSplatView::Ready);

    q->update();
}

void WSplatViewPrivate::onAnimate()
{
    Q_Q(WSplatView);

    if (animating == false) return;

    qint64 now = animationTimer.nsecsElapsed();

    float deltaTime = (now - animationLast) / 1000000000.0f;

    animationLast = now;

    // NOTE: With vsync the frames reach the screen on refresh boundaries, but they start a few ms
    //       early or late (a sort result costs a frame more work). Moving by the measured time
    //       would make the motion uneven, so it is rounded to whole refresh intervals, like the
    //       requestAnimationFrame timestamps Spark moves with. A missed refresh still counts.
    QScreen * screen = (window) ? window->screen() : NULL;

    if (screen
        &&
        screen->refreshRate() >= 1.0
        &&
        window->format().swapInterval() != 0)
    {
        float interval = 1.0f / screen->refreshRate();

        deltaTime = qMax(1.0f, std::round(deltaTime / interval)) * interval;
    }

    bool orbit = (controlMode == WSplatView::Orbit);

    bool moved = (orbit) ? orbitControls.update(camera, fov, q->height())
                         : controls.update(deltaTime, camera);

    if (moved) emit q->cameraChanged();

    bool active = (orbit) ? orbitControls.isActive() : controls.isActive();

    // NOTE: This runs just before the sync of the current frame. The item update lands in this
    //       frame, only the window update schedules the next one.
    if (active || moved)
    {
        q->update();

        window->update();
    }
    else animating = false;
}

void WSplatViewPrivate::onStats()
{
    Q_Q(WSplatView);

    QMutexLocker locker(&(stats->mutex));

    qint64 now = animationTimer.elapsed();

    if (statsTime)
    {
        qint64 elapsed = now - statsTime;

        if (elapsed > 0) fps = (stats->frames - statsFrames) * 1000.0 / elapsed;
    }

    statsFrames = stats->frames;
    statsTime   = now;

    statsSnapshot = stats->renderer;
    backendName   = stats->backendName;

    locker.unlock();

    emit q->statsChanged();
}

//-------------------------------------------------------------------------------------------------
// Ctor / dtor
//-------------------------------------------------------------------------------------------------

WSplatView::WSplatView(QQuickItem * parent)
    : QQuickRhiItem(parent), d_ptr(new WSplatViewPrivate(this))
{
    Q_D(WSplatView); d->init();
}

WSplatView::~WSplatView()
{
    Q_D(WSplatView);

    // NOTE: The load runs on its own data, canceling it frees the worker sooner.
    d->watcher.future().cancel();
}

//-------------------------------------------------------------------------------------------------
// Interface
//-------------------------------------------------------------------------------------------------

/* Q_INVOKABLE */ void WSplatView::resetCamera()
{
    Q_D(WSplatView);

    d->camera.position   = CAMERA_START;
    d->camera.quaternion = QQuaternion();

    d->controls.clear();

    d->orbitControls.clear();

    d->orbitControls.target = QVector3D();

    emit cameraChanged();

    update();
}

/* Q_INVOKABLE */ void WSplatView::lookAt(const QVector3D & target)
{
    Q_D(WSplatView);

    QVector3D direction = target - d->camera.position;

    if (direction.isNull()) return;

    // NOTE: THREE.Object3D.lookAt for a camera: -Z toward the target, +Y up.
    d->camera.quaternion = QQuaternion::fromDirection(-direction.normalized(),
                                                      QVector3D(0.0f, 1.0f, 0.0f));

    emit cameraChanged();

    update();
}

/* Q_INVOKABLE */ QVariantMap WSplatView::statistics() const
{
    Q_D(const WSplatView);

    QMutexLocker locker(&(d->stats->mutex));

    const WSplatRendererStats & stats = d->stats->renderer;

    QVariantMap map;

    map.insert("numSplats",    stats.numSplats);
    map.insert("activeSplats", stats.activeSplats);
    map.insert("generates",    stats.generates);
    map.insert("sorts",        stats.sorts);
    map.insert("metricMs",     stats.metricMs);
    map.insert("sortMs",       stats.sortMs);
    map.insert("gpuMs",        stats.gpuMs);
    map.insert("frames",       d->stats->frames);

    return map;
}

//-------------------------------------------------------------------------------------------------
// Protected QQuickRhiItem implementation
//-------------------------------------------------------------------------------------------------

/* virtual */ QQuickRhiItemRenderer * WSplatView::createRenderer()
{
    return new WSplatViewRenderer;
}

//-------------------------------------------------------------------------------------------------
// Protected QQuickItem reimplementation
//-------------------------------------------------------------------------------------------------

/* virtual */ void WSplatView::itemChange(ItemChange change, const ItemChangeData & value)
{
    Q_D(WSplatView);

    if (change == ItemSceneChange)
    {
        if (d->window) disconnect(d->window, NULL, this, NULL);

        d->window = value.window;

        if (d->window)
        {
            connect(d->window, SIGNAL(afterAnimating()), this, SLOT(onAnimate()));
        }
    }

    QQuickRhiItem::itemChange(change, value);
}

/* virtual */ void WSplatView::keyPressEvent(QKeyEvent * event)
{
    Q_D(WSplatView);

    if (d->controlsEnabled == false)
    {
        QQuickRhiItem::keyPressEvent(event);

        return;
    }

    // SPARK: examples/editor, the keys only move the camera in fly mode.
    if (d->controlMode == Orbit)
    {
        QQuickRhiItem::keyPressEvent(event);

        return;
    }

    d->controls.fpsMovement.keyPress(event);

    d->startAnimation();
}

/* virtual */ void WSplatView::keyReleaseEvent(QKeyEvent * event)
{
    Q_D(WSplatView);

    if (d->controlsEnabled == false)
    {
        QQuickRhiItem::keyReleaseEvent(event);

        return;
    }

    if (d->controlMode == Orbit)
    {
        QQuickRhiItem::keyReleaseEvent(event);

        return;
    }

    d->controls.fpsMovement.keyRelease(event);

    d->startAnimation();
}

/* virtual */ void WSplatView::mousePressEvent(QMouseEvent * event)
{
    Q_D(WSplatView);

    forceActiveFocus(Qt::MouseFocusReason);

    if (d->controlsEnabled == false)
    {
        QQuickRhiItem::mousePressEvent(event);

        return;
    }

    if (d->controlMode == Orbit)
    {
        d->orbitControls.mousePress(event);
    }
    else d->controls.pointerControls.mousePress(event);

    d->startAnimation();
}

/* virtual */ void WSplatView::mouseMoveEvent(QMouseEvent * event)
{
    Q_D(WSplatView);

    if (d->controlsEnabled == false) return;

    if (d->controlMode == Orbit)
    {
        d->orbitControls.mouseMove(event);
    }
    else d->controls.pointerControls.mouseMove(event);

    d->startAnimation();
}

/* virtual */ void WSplatView::mouseReleaseEvent(QMouseEvent * event)
{
    Q_D(WSplatView);

    if (d->controlsEnabled == false) return;

    if (d->controlMode == Orbit)
    {
        d->orbitControls.mouseRelease(event);
    }
    else d->controls.pointerControls.mouseRelease(event);

    d->startAnimation();
}

/* virtual */ void WSplatView::wheelEvent(QWheelEvent * event)
{
    Q_D(WSplatView);

    if (d->controlsEnabled == false)
    {
        QQuickRhiItem::wheelEvent(event);

        return;
    }

    if (d->controlMode == Orbit)
    {
        d->orbitControls.wheel(event);
    }
    else d->controls.pointerControls.wheel(event);

    d->startAnimation();
}

/* virtual */ void WSplatView::focusOutEvent(QFocusEvent * event)
{
    Q_D(WSplatView);

    // SPARK: FpsMovement releases every key on window blur.
    d->controls.clear();

    d->orbitControls.clear();

    QQuickRhiItem::focusOutEvent(event);
}

//-------------------------------------------------------------------------------------------------
// Properties
//-------------------------------------------------------------------------------------------------

QUrl WSplatView::source() const
{
    Q_D(const WSplatView); return d->source;
}

void WSplatView::setSource(const QUrl & url)
{
    Q_D(WSplatView);

    if (d->source == url) return;

    d->source = url;

    // NOTE: A load in progress is aborted. The current splats are freed before loading, so two
    //       scenes never live together, and the renderer frees their textures and targets on the
    //       next frame.
    d->watcher.future().cancel();

    d->splats.clear();

    if (url.isEmpty() == false)
    {
        d->setStatus(Loading);

        QString fileName = urlToPath(url);

        d->watcher.setFuture(QtConcurrent::run(loadSplats, fileName));
    }
    else d->setStatus(Null);

    emit splatCountChanged();

    update();

    emit sourceChanged();
}

WSplatView::Status WSplatView::status() const
{
    Q_D(const WSplatView); return d->status;
}

QString WSplatView::error() const
{
    Q_D(const WSplatView); return d->error;
}

int WSplatView::splatCount() const
{
    Q_D(const WSplatView);

    if (d->splats) return d->splats->numSplats;
    else           return 0;
}

int WSplatView::activeSplats() const
{
    Q_D(const WSplatView); return d->statsSnapshot.activeSplats;
}

qreal WSplatView::fps() const
{
    Q_D(const WSplatView); return d->fps;
}

qreal WSplatView::sortTime() const
{
    Q_D(const WSplatView); return d->statsSnapshot.sortMs;
}

qreal WSplatView::metricTime() const
{
    Q_D(const WSplatView); return d->statsSnapshot.metricMs;
}

qreal WSplatView::loadTime() const
{
    Q_D(const WSplatView); return d->loadMs;
}

QString WSplatView::backendName() const
{
    Q_D(const WSplatView); return d->backendName;
}

//-------------------------------------------------------------------------------------------------

QColor WSplatView::backgroundColor() const
{
    Q_D(const WSplatView); return d->backgroundColor;
}

void WSplatView::setBackgroundColor(const QColor & color)
{
    Q_D(WSplatView);

    if (d->backgroundColor == color) return;

    d->backgroundColor = color;

    // NOTE: A translucent background must be blended with the content below.
    setAlphaBlending(color.alpha() < 255);

    emit backgroundColorChanged();

    update();
}

WSplatView::Orientation WSplatView::orientation() const
{
    Q_D(const WSplatView); return d->orientation;
}

void WSplatView::setOrientation(Orientation orientation)
{
    Q_D(WSplatView);

    if (d->orientation == orientation) return;

    d->orientation = orientation;

    emit orientationChanged();

    update();
}

//-------------------------------------------------------------------------------------------------

QVector3D WSplatView::cameraPosition() const
{
    Q_D(const WSplatView); return d->camera.position;
}

void WSplatView::setCameraPosition(const QVector3D & position)
{
    Q_D(WSplatView);

    if (d->camera.position == position) return;

    d->camera.position = position;

    emit cameraChanged();

    update();
}

QQuaternion WSplatView::cameraRotation() const
{
    Q_D(const WSplatView); return d->camera.quaternion;
}

void WSplatView::setCameraRotation(const QQuaternion & rotation)
{
    Q_D(WSplatView);

    if (d->camera.quaternion == rotation) return;

    d->camera.quaternion = rotation;

    emit cameraChanged();

    update();
}

//-------------------------------------------------------------------------------------------------

qreal WSplatView::fov() const
{
    Q_D(const WSplatView); return d->fov;
}

void WSplatView::setFov(qreal fov)
{
    Q_D(WSplatView);

    if (d->fov == fov) return;

    d->fov = fov;

    emit fovChanged();

    update();
}

qreal WSplatView::zNear() const
{
    Q_D(const WSplatView); return d->zNear;
}

void WSplatView::setZNear(qreal zNear)
{
    Q_D(WSplatView);

    if (d->zNear == zNear) return;

    d->zNear = zNear;

    emit fovChanged();

    update();
}

qreal WSplatView::zFar() const
{
    Q_D(const WSplatView); return d->zFar;
}

void WSplatView::setZFar(qreal zFar)
{
    Q_D(WSplatView);

    if (d->zFar == zFar) return;

    d->zFar = zFar;

    emit fovChanged();

    update();
}

//-------------------------------------------------------------------------------------------------

bool WSplatView::controlsEnabled() const
{
    Q_D(const WSplatView); return d->controlsEnabled;
}

void WSplatView::setControlsEnabled(bool enabled)
{
    Q_D(WSplatView);

    if (d->controlsEnabled == enabled) return;

    d->controlsEnabled = enabled;

    if (enabled == false)
    {
        d->controls.clear();

        d->orbitControls.clear();
    }

    emit controlsEnabledChanged();
}

WSplatView::ControlMode WSplatView::controlMode() const
{
    Q_D(const WSplatView); return d->controlMode;
}

void WSplatView::setControlMode(ControlMode mode)
{
    Q_D(WSplatView);

    if (d->controlMode == mode) return;

    d->controlMode = mode;

    // NOTE: Each mode starts from the start pose, and orbits around the origin.
    resetCamera();

    emit controlModeChanged();
}

qreal WSplatView::moveSpeed() const
{
    Q_D(const WSplatView); return d->controls.fpsMovement.moveSpeed;
}

void WSplatView::setMoveSpeed(qreal speed)
{
    Q_D(WSplatView);

    if (d->controls.fpsMovement.moveSpeed == speed) return;

    d->controls.fpsMovement.moveSpeed = speed;

    emit moveSpeedChanged();
}

//-------------------------------------------------------------------------------------------------

qreal WSplatView::maxStdDev() const
{
    Q_D(const WSplatView); return d->options.maxStdDev;
}

void WSplatView::setMaxStdDev(qreal value)
{
    Q_D(WSplatView);

    if (d->options.maxStdDev == float(value)) return;

    d->options.maxStdDev = value;

    emit renderOptionsChanged();

    update();
}

qreal WSplatView::blurAmount() const
{
    Q_D(const WSplatView); return d->options.blurAmount;
}

void WSplatView::setBlurAmount(qreal value)
{
    Q_D(WSplatView);

    if (d->options.blurAmount == float(value)) return;

    d->options.blurAmount = value;

    emit renderOptionsChanged();

    update();
}

qreal WSplatView::minAlpha() const
{
    Q_D(const WSplatView); return d->options.minAlpha;
}

void WSplatView::setMinAlpha(qreal value)
{
    Q_D(WSplatView);

    if (d->options.minAlpha == float(value)) return;

    d->options.minAlpha = value;

    emit renderOptionsChanged();

    update();
}

int WSplatView::shDegree() const
{
    Q_D(const WSplatView); return d->shDegree;
}

void WSplatView::setShDegree(int degree)
{
    Q_D(WSplatView);

    degree = qBound(0, degree, 3);

    if (d->shDegree == degree) return;

    d->shDegree = degree;

    emit renderOptionsChanged();

    update();
}

bool WSplatView::isFast() const
{
    Q_D(const WSplatView);

    return (d->options.tightBounds
            &&
            d->options.cpuSortMetric
            &&
            d->options.fastSort);
}

void WSplatView::setFast(bool fast)
{
    Q_D(WSplatView);

    if (isFast() == fast) return;

    d->options.tightBounds   = fast;
    d->options.cpuSortMetric = fast;
    d->options.fastSort      = fast;

    emit renderOptionsChanged();

    update();
}

bool WSplatView::isContinuous() const
{
    Q_D(const WSplatView); return d->continuous;
}

void WSplatView::setContinuous(bool continuous)
{
    Q_D(WSplatView);

    if (d->continuous == continuous) return;

    d->continuous = continuous;

    emit continuousChanged();

    update();
}

#include "moc_WSplatView.cpp"
