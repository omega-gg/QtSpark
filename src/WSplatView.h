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

#ifndef WSPLATVIEW_H
#define WSPLATVIEW_H

// Qt includes
#include <QQuickRhiItem>
#include <QUrl>
#include <QColor>
#include <QVector3D>
#include <QQuaternion>
#include <QVariantMap>

// Splat includes
#include <Splat.h>

// Forward declarations
class WSplatViewPrivate;

class SPLAT_EXPORT WSplatView : public QQuickRhiItem
{
    Q_OBJECT

    Q_ENUMS(Status)
    Q_ENUMS(Orientation)
    Q_ENUMS(ControlMode)

    Q_PROPERTY(QUrl source READ source WRITE setSource NOTIFY sourceChanged)

    Q_PROPERTY(Status  status READ status NOTIFY statusChanged)
    Q_PROPERTY(QString error  READ error  NOTIFY statusChanged)

    Q_PROPERTY(int splatCount READ splatCount NOTIFY splatCountChanged)

    Q_PROPERTY(int    activeSplats READ activeSplats NOTIFY statsChanged)
    Q_PROPERTY(qreal  fps          READ fps          NOTIFY statsChanged)
    Q_PROPERTY(qreal  sortTime     READ sortTime     NOTIFY statsChanged)
    Q_PROPERTY(qreal  metricTime   READ metricTime   NOTIFY statsChanged)
    Q_PROPERTY(qreal  loadTime     READ loadTime     NOTIFY statusChanged)
    Q_PROPERTY(QString backendName READ backendName  NOTIFY statsChanged)

    Q_PROPERTY(QColor backgroundColor READ backgroundColor WRITE setBackgroundColor
               NOTIFY backgroundColorChanged)

    Q_PROPERTY(Orientation orientation READ orientation WRITE setOrientation
               NOTIFY orientationChanged)

    Q_PROPERTY(QVector3D   cameraPosition READ cameraPosition WRITE setCameraPosition
               NOTIFY cameraChanged)
    Q_PROPERTY(QQuaternion cameraRotation READ cameraRotation WRITE setCameraRotation
               NOTIFY cameraChanged)

    Q_PROPERTY(qreal fov   READ fov   WRITE setFov   NOTIFY fovChanged)
    Q_PROPERTY(qreal zNear READ zNear WRITE setZNear NOTIFY fovChanged)
    Q_PROPERTY(qreal zFar  READ zFar  WRITE setZFar  NOTIFY fovChanged)

    Q_PROPERTY(bool  controlsEnabled READ controlsEnabled WRITE setControlsEnabled
               NOTIFY controlsEnabledChanged)

    Q_PROPERTY(ControlMode controlMode READ controlMode WRITE setControlMode
               NOTIFY controlModeChanged)
    Q_PROPERTY(qreal moveSpeed READ moveSpeed WRITE setMoveSpeed NOTIFY moveSpeedChanged)

    Q_PROPERTY(qreal maxStdDev  READ maxStdDev  WRITE setMaxStdDev  NOTIFY renderOptionsChanged)
    Q_PROPERTY(qreal blurAmount READ blurAmount WRITE setBlurAmount NOTIFY renderOptionsChanged)
    Q_PROPERTY(qreal minAlpha   READ minAlpha   WRITE setMinAlpha   NOTIFY renderOptionsChanged)
    Q_PROPERTY(int   shDegree   READ shDegree   WRITE setShDegree   NOTIFY renderOptionsChanged)

    // NOTE: Enables the Splat performance options (CPU sort metric, fast sort, empty quad
    //       culling). Off by default, which is the exact Spark code path. The image is the same.
    Q_PROPERTY(bool fast READ isFast WRITE setFast NOTIFY renderOptionsChanged)

    // NOTE: Draws every frame, like Spark's animation loop, instead of only when something
    //       changed. Off by default. On, the GPU stays at speed, so on laptops there is no hitch
    //       when a movement starts, at the cost of power.
    Q_PROPERTY(bool continuous READ isContinuous WRITE setContinuous NOTIFY continuousChanged)

public:
    enum Status { Null, Loading, Ready, Error };

    // NOTE: The frame the file is authored in. Spark's examples rotate 3DGS captures (OpenCV,
    //       +Y down) by 180 degrees about X.
    enum Orientation { OpenCV, OpenGL, ZUp };

    // NOTE: Fly is Spark's SparkControls. Orbit is the three.js OrbitControls of Spark's editor,
    //       around the origin. Switching resets the camera.
    enum ControlMode { Fly, Orbit };

public:
    explicit WSplatView(QQuickItem * parent = NULL);

    ~WSplatView();

public: // Interface
    Q_INVOKABLE void resetCamera();

    Q_INVOKABLE void lookAt(const QVector3D & target);

    // NOTE: Renderer counters for benchmarks: numSplats, activeSplats, generates, sorts,
    //       metricMs, sortMs, gpuMs (needs QQuickGraphicsConfiguration::setTimestamps) and
    //       frames.
    Q_INVOKABLE QVariantMap statistics() const;

protected: // QQuickRhiItem implementation
    /* virtual */ QQuickRhiItemRenderer * createRenderer();

protected: // QQuickItem reimplementation
    /* virtual */ void itemChange(ItemChange change, const ItemChangeData & value);

    /* virtual */ void keyPressEvent  (QKeyEvent * event);
    /* virtual */ void keyReleaseEvent(QKeyEvent * event);

    /* virtual */ void mousePressEvent  (QMouseEvent * event);
    /* virtual */ void mouseMoveEvent   (QMouseEvent * event);
    /* virtual */ void mouseReleaseEvent(QMouseEvent * event);

    /* virtual */ void wheelEvent(QWheelEvent * event);

    /* virtual */ void focusOutEvent(QFocusEvent * event);

signals:
    void sourceChanged();
    void statusChanged();

    void splatCountChanged();

    void statsChanged();

    void backgroundColorChanged();
    void orientationChanged();

    void cameraChanged();

    void fovChanged();

    void controlsEnabledChanged();

    void controlModeChanged();
    void moveSpeedChanged();

    void renderOptionsChanged();

    void continuousChanged();

public: // Properties
    QUrl source() const;
    void setSource(const QUrl & url);

    Status  status() const;
    QString error () const;

    int splatCount() const;

    int     activeSplats() const;
    qreal   fps         () const;
    qreal   sortTime    () const;
    qreal   metricTime  () const;
    qreal   loadTime    () const;
    QString backendName () const;

    QColor backgroundColor() const;
    void   setBackgroundColor(const QColor & color);

    Orientation orientation() const;
    void        setOrientation(Orientation orientation);

    QVector3D cameraPosition() const;
    void      setCameraPosition(const QVector3D & position);

    QQuaternion cameraRotation() const;
    void        setCameraRotation(const QQuaternion & rotation);

    qreal fov() const;
    void  setFov(qreal fov);

    qreal zNear() const;
    void  setZNear(qreal zNear);

    qreal zFar() const;
    void  setZFar(qreal zFar);

    bool controlsEnabled() const;
    void setControlsEnabled(bool enabled);

    ControlMode controlMode() const;
    void        setControlMode(ControlMode mode);

    qreal moveSpeed() const;
    void  setMoveSpeed(qreal speed);

    qreal maxStdDev() const;
    void  setMaxStdDev(qreal value);

    qreal blurAmount() const;
    void  setBlurAmount(qreal value);

    qreal minAlpha() const;
    void  setMinAlpha(qreal value);

    int  shDegree() const;
    void setShDegree(int degree);

    bool isFast() const;
    void setFast(bool fast);

    bool isContinuous() const;
    void setContinuous(bool continuous);

private:
    Q_DISABLE_COPY(WSplatView)

    QScopedPointer<WSplatViewPrivate> d_ptr;

    Q_DECLARE_PRIVATE(WSplatView)

    Q_PRIVATE_SLOT(d_func(), void onLoaded())
    Q_PRIVATE_SLOT(d_func(), void onAnimate())
    Q_PRIVATE_SLOT(d_func(), void onStats())

    friend class WSplatViewRenderer;
};

#endif // WSPLATVIEW_H
