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

// SPARK v2.3.1: src/controls.ts (SparkControls, FpsMovement, PointerControls)

#ifndef WSPLATCONTROLS_H
#define WSPLATCONTROLS_H

// Qt includes
#include <QVector3D>
#include <QQuaternion>
#include <QPointF>
#include <QSet>
#include <QHash>

// Splat includes
#include <Splat.h>

// Forward declarations
class QKeyEvent;
class QMouseEvent;
class QWheelEvent;
class QFocusEvent;

//-------------------------------------------------------------------------------------------------
// WSplatControlsObject
//-------------------------------------------------------------------------------------------------

// NOTE: The controlled object, like a THREE.Object3D: a camera looking down -Z with +Y up.
struct WSplatControlsObject
{
    QVector3D   position;
    QQuaternion quaternion;
};

//-------------------------------------------------------------------------------------------------
// WSplatFpsMovement
//-------------------------------------------------------------------------------------------------

// NOTE: Keyboard part of Spark's FpsMovement (gamepad and XR are out of scope).
class SPLAT_EXPORT WSplatFpsMovement
{
public:
    WSplatFpsMovement();

public: // Interface
    void keyPress  (QKeyEvent * event);
    void keyRelease(QKeyEvent * event);

    // NOTE: Releases every key, call it when the focus is lost.
    void clear();

    // NOTE: Returns true while the object is being moved, so the caller keeps animating.
    bool update(float deltaTime, WSplatControlsObject & control);

    bool isActive() const;

public: // Variables
    bool enable;

    float moveSpeed;
    float rollSpeed;
    float rotateSpeed;

    float capsMultiplier;
    float shiftMultiplier;
    float ctrlMultiplier;

    // NOTE: Qt::Key to direction, in place of the browser event.code mappings.
    QHash<int, QVector3D> keycodeMoveMapping;
    QHash<int, QVector3D> keycodeRotateMapping;

    QVector3D extraMove;

    QSet<int> keys;

    bool capsLock;
};

//-------------------------------------------------------------------------------------------------
// WSplatPointerControls
//-------------------------------------------------------------------------------------------------

// NOTE: Mouse part of Spark's PointerControls (touch, double press and pinch are out of scope).
class SPLAT_EXPORT WSplatPointerControls
{
public:
    WSplatPointerControls();

public: // Interface
    void mousePress  (QMouseEvent * event);
    void mouseMove   (QMouseEvent * event);
    void mouseRelease(QMouseEvent * event);

    void wheel(QWheelEvent * event);

    void clear();

    // NOTE: Returns true while the object is moving (drag, scroll or inertia).
    bool update(float deltaTime, WSplatControlsObject & control);

    bool isActive() const;

public: // Variables
    bool enable;

    float rotateSpeed;
    float slideSpeed;
    float scrollSpeed;

    bool swapRotateSlide;
    bool reverseRotate;
    bool reverseSlide;
    bool reverseScroll;

    float moveInertia;
    float rotateInertia;

    // NOTE: Internal state, mirrors PointerControls.
    bool    rotating;
    bool    sliding;
    int     slideButton;
    QPointF last;
    QPointF position;

    QVector3D scroll;

    QVector3D rotateVelocity;
    QVector3D moveVelocity;
};

//-------------------------------------------------------------------------------------------------
// WSplatControls
//-------------------------------------------------------------------------------------------------

// NOTE: SparkControls: FpsMovement then PointerControls.
class SPLAT_EXPORT WSplatControls
{
public: // Interface
    // NOTE: deltaTime is in seconds. Returns true while anything moves or input is held (keys,
    //       drag, inertia, pending scroll), so the caller knows whether to request frames.
    bool update(float deltaTime, WSplatControlsObject & control);

    bool isActive() const;

    void clear();

public: // Variables
    WSplatFpsMovement     fpsMovement;
    WSplatPointerControls pointerControls;
};

#endif // WSPLATCONTROLS_H
