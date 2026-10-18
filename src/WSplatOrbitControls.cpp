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

    Portions of Splat are ported from three.js (https://github.com/mrdoob/three.js).
    Copyright (C) 2010-2025 three.js authors. MIT License, see LICENSE.three.
*/
//=================================================================================================

// THREE r180: examples/jsm/controls/OrbitControls.js

#include "WSplatOrbitControls.h"

// Qt includes
#include <QMouseEvent>
#include <QWheelEvent>
#include <QtMath>

// C++ includes
#include <cmath>

//-------------------------------------------------------------------------------------------------
// Static variables

// NOTE: OrbitControls _EPS, and the Spherical.makeSafe epsilon.
static const float EPS = 0.000001f;

static const float TWO_PI = 2.0f * float(M_PI);

//-------------------------------------------------------------------------------------------------
// Static functions
//-------------------------------------------------------------------------------------------------

// THREE: OrbitControls._getZoomScale
static float getZoomScale(float zoomSpeed, float delta)
{
    float normalizedDelta = std::abs(delta * 0.01f);

    return std::pow(0.95f, zoomSpeed * normalizedDelta);
}

//-------------------------------------------------------------------------------------------------
// Ctor / dtor
//-------------------------------------------------------------------------------------------------

WSplatOrbitControls::WSplatOrbitControls()
{
    // SPARK: examples/editor, target (0, 0, 0), minDistance 0.1 and maxDistance 10.
    minDistance = 0.1f;
    maxDistance = 10.0f;

    minPolarAngle = 0.0f;
    maxPolarAngle = float(M_PI);

    rotateSpeed = 1.0f;
    zoomSpeed   = 1.0f;
    panSpeed    = 1.0f;

    state = None;

    scale = 1.0f;
}

//-------------------------------------------------------------------------------------------------
// Interface
//-------------------------------------------------------------------------------------------------

// THREE: onMouseDown
void WSplatOrbitControls::mousePress(QMouseEvent * event)
{
    // NOTE: The browser treats a mouse as a single pointer, a second button pressed while one is
    //       held does not start another action.
    if (state != None) return;

    // NOTE: mouseButtons LEFT rotate, MIDDLE dolly, RIGHT pan. Ctrl, Meta or Shift swaps rotate
    //       and pan.
    bool swap = (event->modifiers() & (Qt::ControlModifier | Qt::MetaModifier
                                       | Qt::ShiftModifier));

    Qt::MouseButton button = event->button();

    if      (button == Qt::LeftButton)   state = (swap) ? Pan : Rotate;
    else if (button == Qt::MiddleButton) state = Dolly;
    else if (button == Qt::RightButton)  state = (swap) ? Rotate : Pan;
    else return;

    start = event->position();
}

// THREE: _handleMouseMoveRotate, _handleMouseMoveDolly, _handleMouseMovePan
void WSplatOrbitControls::mouseMove(QMouseEvent * event)
{
    if (state == None) return;

    QPointF position = event->position();

    QPointF delta = position - start;

    start = position;

    // NOTE: A dolly moving down zooms out (_dollyOut), moving up zooms in (_dollyIn).
    if      (state == Rotate)  rotateDelta += delta * rotateSpeed;
    else if (state == Pan)     panDelta    += delta * panSpeed;
    else if (delta.y() > 0.0f) scale /= getZoomScale(zoomSpeed, delta.y());
    else if (delta.y() < 0.0f) scale *= getZoomScale(zoomSpeed, delta.y());
}

// THREE: onPointerUp, the action ends with the last button.
void WSplatOrbitControls::mouseRelease(QMouseEvent * event)
{
    if (event->buttons() == Qt::NoButton) state = None;
}

// THREE: onMouseWheel, _handleMouseWheel
// NOTE: _customWheelEvent zooms 10 times faster for browser pinch gestures, which arrive as wheel
//       events with a synthetic Ctrl. Qt delivers pinches as gesture events, so it is left out.
// SPLAT: The wheel also zooms during a drag, like Spark's PointerControls. three.js ignores it
//        then, so that its start and end events do not split the drag, and we have no events.
void WSplatOrbitControls::wheel(QWheelEvent * event)
{
    float deltaY = WSplatPointerControls::wheelPixels(event).y();

    // NOTE: Scrolling up zooms in (_dollyIn), down zooms out (_dollyOut).
    if      (deltaY < 0.0f) scale *= getZoomScale(zoomSpeed, deltaY);
    else if (deltaY > 0.0f) scale /= getZoomScale(zoomSpeed, deltaY);
}

void WSplatOrbitControls::clear()
{
    state = None;

    rotateDelta = QPointF();
    panDelta    = QPointF();

    scale = 1.0f;
}

// THREE: OrbitControls.update, without damping, auto rotation, azimuth, target radius limits and
//        zoom to cursor (their defaults).
bool WSplatOrbitControls::update(WSplatControlsObject & object, float fov, float height)
{
    // NOTE: object.up is +Y, so the "y-axis-is-up" rotation (_quat) is the identity.
    QVector3D offset = object.position - target;

    // THREE: Spherical.setFromVector3
    float radius = offset.length();
    float theta  = 0.0f;
    float phi    = 0.0f;

    if (radius > 0.0f)
    {
        theta = std::atan2(offset.x(), offset.z());
        phi   = std::acos(qBound(-1.0f, offset.y() / radius, 1.0f));
    }

    if (height > 0.0f)
    {
        // THREE: _rotateLeft, _rotateUp, the angle is relative to the height for both axes.
        theta -= TWO_PI * float(rotateDelta.x()) / height;
        phi   -= TWO_PI * float(rotateDelta.y()) / height;

        // THREE: _pan, _panLeft, _panUp with screenSpacePanning, from the current camera matrix.
        float targetDistance = radius * std::tan(qDegreesToRadians(fov) / 2.0f);

        QVector3D right = object.quaternion.rotatedVector(QVector3D(1.0f, 0.0f, 0.0f));
        QVector3D up    = object.quaternion.rotatedVector(QVector3D(0.0f, 1.0f, 0.0f));

        target += right * (-2.0f * float(panDelta.x()) * targetDistance / height)
                  +
                  up    * ( 2.0f * float(panDelta.y()) * targetDistance / height);
    }

    // THREE: Spherical.makeSafe
    phi = qBound(minPolarAngle, phi, maxPolarAngle);
    phi = qBound(EPS, phi, float(M_PI) - EPS);

    radius = qBound(minDistance, radius * scale, maxDistance);

    // THREE: Vector3.setFromSpherical
    float sinPhiRadius = std::sin(phi) * radius;

    offset = QVector3D(sinPhiRadius * std::sin(theta), std::cos(phi) * radius,
                       sinPhiRadius * std::cos(theta));

    QVector3D position = target + offset;

    // THREE: Object3D.lookAt for a camera: -Z toward the target, +Y up.
    QQuaternion quaternion = QQuaternion::fromDirection(offset.normalized(),
                                                        QVector3D(0.0f, 1.0f, 0.0f));

    rotateDelta = QPointF();
    panDelta    = QPointF();

    scale = 1.0f;

    // NOTE: The update condition of OrbitControls, the target moves with the camera.
    bool moved = ((position - object.position).lengthSquared() > EPS
                  ||
                  8.0f * (1.0f - QQuaternion::dotProduct(object.quaternion, quaternion)) > EPS);

    object.position   = position;
    object.quaternion = quaternion;

    return moved;
}

bool WSplatOrbitControls::isActive() const
{
    return (state != None
            ||
            rotateDelta.isNull() == false
            ||
            panDelta.isNull() == false
            ||
            scale != 1.0f);
}
