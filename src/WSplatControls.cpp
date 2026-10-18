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

// SPARK v2.3.1: src/controls.ts

#include "WSplatControls.h"

// Qt includes
#include <QKeyEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QtMath>

// C++ includes
#include <cmath>

//-------------------------------------------------------------------------------------------------
// Static variables

static const float DEFAULT_MOVEMENT_SPEED   = 1.0f;
static const float DEFAULT_ROLL_SPEED       = 2.0f;
static const float DEFAULT_ROTATE_SPEED     = 0.002f;
static const float DEFAULT_SLIDE_SPEED      = 0.006f;
static const float DEFAULT_SCROLL_SPEED     = 0.0015f;
static const float DEFAULT_ROLL_SPRING      = 0.0f;
static const float DEFAULT_ROTATE_INERTIA   = 0.15f;
static const float DEFAULT_MOVE_INERTIA     = 0.15f;
static const float DEFAULT_FPS_ROTATE_SPEED = 2.0f;

// NOTE: The stick threshold, pointer roll scale, press move, dual press and double press
//       constants are out of scope.

static const float MOVEMENT_THRESHOLD = 1.0e-4f;

// NOTE: Chrome reports deltaY = 100 pixels for one wheel notch, Qt reports 120 angle units.
static const float WHEEL_ANGLE_TO_PIXEL = 100.0f / 120.0f;

//-------------------------------------------------------------------------------------------------
// Static functions
//-------------------------------------------------------------------------------------------------

// NOTE: A THREE.Euler in radians, applied in "YXZ" order.
struct WSplatControlsEuler
{
    double x;
    double y;
    double z;
};

// NOTE: three.js Euler.setFromQuaternion(q, "YXZ"), through Matrix4.makeRotationFromQuaternion.
//       Qt's QQuaternion::toEulerAngles works in degrees with its own gimbal lock handling.
static WSplatControlsEuler eulerFromQuaternion(const QQuaternion & q)
{
    double x = q.x();
    double y = q.y();
    double z = q.z();
    double w = q.scalar();

    double x2 = x + x;
    double y2 = y + y;
    double z2 = z + z;

    double xx = x * x2;
    double xy = x * y2;
    double xz = x * z2;
    double yy = y * y2;
    double yz = y * z2;
    double zz = z * z2;
    double wx = w * x2;
    double wy = w * y2;
    double wz = w * z2;

    double m11 = 1 - (yy + zz);
    double m13 = xz + wy;
    double m21 = xy + wz;
    double m22 = 1 - (xx + zz);
    double m23 = yz - wx;
    double m31 = xz - wy;
    double m33 = 1 - (xx + yy);

    WSplatControlsEuler euler;

    euler.x = std::asin(-qBound(-1.0, m23, 1.0));

    if (std::abs(m23) < 0.9999999)
    {
        euler.y = std::atan2(m13, m33);
        euler.z = std::atan2(m21, m22);
    }
    else
    {
        euler.y = std::atan2(-m31, m11);
        euler.z = 0;
    }

    return euler;
}

// NOTE: three.js Quaternion.setFromEuler(euler) for the "YXZ" order. The Qt constructor takes
//       the scalar first.
static QQuaternion quaternionFromEuler(const WSplatControlsEuler & euler)
{
    double c1 = std::cos(euler.x / 2);
    double c2 = std::cos(euler.y / 2);
    double c3 = std::cos(euler.z / 2);

    double s1 = std::sin(euler.x / 2);
    double s2 = std::sin(euler.y / 2);
    double s3 = std::sin(euler.z / 2);

    return QQuaternion(c1 * c2 * c3 + s1 * s2 * s3,
                       s1 * c2 * c3 + c1 * s2 * s3,
                       c1 * s2 * c3 - s1 * c2 * s3,
                       c1 * c2 * s3 - s1 * s2 * c3);
}

// NOTE: The controlled object has a fixed +Y up and no parent, so setFromUnitVectors from
//       EULER_YAW_AXIS to up is the identity. We keep the calls to preserve the composition order.
// SPARK: yawAxisToUp
static QQuaternion yawAxisToUp()
{
    return QQuaternion();
}

// NOTE: three.js Quaternion.invert() is the conjugate, QQuaternion::inverted() also divides by
//       the squared length. A three.js q.premultiply(p) is p * q with Qt's operator*.
// SPARK: eulersAboutUp
static WSplatControlsEuler eulersAboutUp(const WSplatControlsObject & control)
{
    return eulerFromQuaternion(yawAxisToUp().conjugated() * control.quaternion);
}

// NOTE: control.quaternion.setFromEuler(eulers).premultiply(yawAxisToUp(control)).
static void applyEulers(WSplatControlsObject & control, const WSplatControlsEuler & eulers)
{
    control.quaternion = yawAxisToUp() * quaternionFromEuler(eulers);
}

// NOTE: three.js Vector3.applyQuaternion, equal to QQuaternion::rotatedVector for a unit
//       quaternion.
static QVector3D applyQuaternion(const QVector3D & vector, const QQuaternion & quaternion)
{
    return quaternion.rotatedVector(vector);
}

// NOTE: Sums the directions of the held keys, like the Object.entries loops.
static QVector3D sumKeys(const QHash<int, QVector3D> & mapping, const QSet<int> & keys)
{
    QVector3D result;

    QHashIterator<int, QVector3D> i(mapping);

    while (i.hasNext())
    {
        i.next();

        if (keys.contains(i.key())) result += i.value();
    }

    return result;
}

static bool hasKeys(const QHash<int, QVector3D> & mapping, const QSet<int> & keys)
{
    for (int key : keys)
    {
        if (mapping.contains(key)) return true;
    }

    return false;
}

// NOTE: Maps a Qt button to the browser MouseEvent.button value.
static int getButton(Qt::MouseButton button)
{
    switch (button)
    {
    case Qt::LeftButton:    return 0;
    case Qt::MiddleButton:  return 1;
    case Qt::RightButton:   return 2;
    case Qt::BackButton:    return 3;
    case Qt::ForwardButton: return 4;
    default:                return -1;
    }
}

//-------------------------------------------------------------------------------------------------
// WSplatFpsMovement
//-------------------------------------------------------------------------------------------------

//-------------------------------------------------------------------------------------------------
// Ctor / dtor

// SPARK: FpsMovement.constructor
WSplatFpsMovement::WSplatFpsMovement()
{
    enable = true;

    moveSpeed   = DEFAULT_MOVEMENT_SPEED;
    rollSpeed   = DEFAULT_ROLL_SPEED;
    rotateSpeed = DEFAULT_FPS_ROTATE_SPEED;

    // NOTE: Browser event.code values are physical keys, Qt::Key values follow the keyboard
    //       layout. So on AZERTY the forward key is Z, not the physical W key.

    // NOTE: WASD_KEYCODE_MOVE, standard WASD movement keys with E and Q for up and down.
    keycodeMoveMapping.insert(Qt::Key_W, QVector3D( 0,  0, -1));
    keycodeMoveMapping.insert(Qt::Key_S, QVector3D( 0,  0,  1));
    keycodeMoveMapping.insert(Qt::Key_A, QVector3D(-1,  0,  0));
    keycodeMoveMapping.insert(Qt::Key_D, QVector3D( 1,  0,  0));
    keycodeMoveMapping.insert(Qt::Key_E, QVector3D( 0,  1,  0));
    keycodeMoveMapping.insert(Qt::Key_Q, QVector3D( 0, -1,  0));

    // NOTE: ARROW_KEYCODE_MOVE, arrow key movement with PageUp and PageDown.
    keycodeMoveMapping.insert(Qt::Key_Up,       QVector3D( 0,  0, -1));
    keycodeMoveMapping.insert(Qt::Key_Down,     QVector3D( 0,  0,  1));
    keycodeMoveMapping.insert(Qt::Key_Left,     QVector3D(-1,  0,  0));
    keycodeMoveMapping.insert(Qt::Key_Right,    QVector3D( 1,  0,  0));
    keycodeMoveMapping.insert(Qt::Key_PageUp,   QVector3D( 0,  1,  0));
    keycodeMoveMapping.insert(Qt::Key_PageDown, QVector3D( 0, -1,  0));

    // NOTE: QE_KEYCODE_ROTATE (Q and E roll) is commented out upstream, they move vertically.

    // NOTE: ARROW_KEYCODE_ROTATE, Home, End, Insert and Delete for rotation.
    keycodeRotateMapping.insert(Qt::Key_Home,   QVector3D( 0, -1, 0));
    keycodeRotateMapping.insert(Qt::Key_End,    QVector3D( 0,  1, 0));
    keycodeRotateMapping.insert(Qt::Key_Insert, QVector3D(-1,  0, 0));
    keycodeRotateMapping.insert(Qt::Key_Delete, QVector3D( 1,  0, 0));

    // NOTE: The gamepad mapping, the stick threshold and the WebXR manager are out of scope.

    capsMultiplier  = 10.0f;
    shiftMultiplier = 5.0f;
    ctrlMultiplier  = 1.0f / 5.0f;

    capsLock = false;
}

//-------------------------------------------------------------------------------------------------
// Interface

// SPARK: FpsMovement.constructor (keydown listener)
void WSplatFpsMovement::keyPress(QKeyEvent * event)
{
    // NOTE: Auto repeat changes nothing upstream, the key is already down.
    if (event->isAutoRepeat()) return;

    int key = event->key();

    // NOTE: This mirrors keydown.CapsLock. Like the browser, Qt reports the Caps Lock key held
    //       state on Windows and Linux and its lock state on macOS. The lock state is not
    //       queried, so the multiplier only applies while the key is down on Windows.
    if (key == Qt::Key_CapsLock) capsLock = true;

    keys.insert(key);
}

// SPARK: FpsMovement.constructor (keyup listener)
void WSplatFpsMovement::keyRelease(QKeyEvent * event)
{
    if (event->isAutoRepeat()) return;

    int key = event->key();

    if (key == Qt::Key_CapsLock) capsLock = false;

    keys.remove(key);
}

// SPARK: FpsMovement.constructor (blur listener)
void WSplatFpsMovement::clear()
{
    keys.clear();

    capsLock = false;
}

// SPARK: FpsMovement.update
bool WSplatFpsMovement::update(float deltaTime, WSplatControlsObject & control)
{
    if (enable == false) return false;

    // NOTE: Gamepad and WebXR sticks are out of scope, they stay at zero.

    // Rotation

    QVector3D rotate = sumKeys(keycodeRotateMapping, keys);

    // NOTE: The gamepad roll buttons are out of scope.

    rotate *= QVector3D(rotateSpeed, rotateSpeed, rollSpeed);

    bool updated = (rotate.length() > MOVEMENT_THRESHOLD);

    // NOTE: Vector3.manhattanLength.
    if (qAbs(rotate.x()) + qAbs(rotate.y()) + qAbs(rotate.z()) > 0.0f)
    {
        rotate *= deltaTime;

        WSplatControlsEuler eulers = eulersAboutUp(control);

        eulers.y -= rotate.x();

        eulers.x = qMax(-M_PI / 2, qMin(M_PI / 2, eulers.x - rotate.y()));
        eulers.z = qMax(-M_PI,     qMin(M_PI,     eulers.z + rotate.z()));

        applyEulers(control, eulers);
    }

    // Movement

    QVector3D moveVector = extraMove;

    moveVector += sumKeys(keycodeMoveMapping, keys);

    float speedMultiplier = 1.0f;

    if (capsLock) speedMultiplier *= capsMultiplier;

    // NOTE: Qt reports both ShiftLeft and ShiftRight as Key_Shift, ControlLeft and ControlRight
    //       as Key_Control. On macOS Key_Control is the Command key.
    if (keys.contains(Qt::Key_Shift))   speedMultiplier *= shiftMultiplier;
    if (keys.contains(Qt::Key_Control)) speedMultiplier *= ctrlMultiplier;

    if (moveVector.length() > MOVEMENT_THRESHOLD) updated = true;

    // NOTE: Apply movement in view direction.
    moveVector = applyQuaternion(moveVector, control.quaternion);

    control.position += moveVector * (moveSpeed * speedMultiplier * deltaTime);

    return updated;
}

bool WSplatFpsMovement::isActive() const
{
    if (enable == false) return false;

    return (hasKeys(keycodeMoveMapping,   keys)
            ||
            hasKeys(keycodeRotateMapping, keys)
            ||
            extraMove.length() > MOVEMENT_THRESHOLD);
}

//-------------------------------------------------------------------------------------------------
// WSplatPointerControls
//-------------------------------------------------------------------------------------------------

//-------------------------------------------------------------------------------------------------
// Ctor / dtor

// SPARK: PointerControls.constructor
WSplatPointerControls::WSplatPointerControls()
{
    enable = true;

    rotateSpeed = DEFAULT_ROTATE_SPEED;
    slideSpeed  = DEFAULT_SLIDE_SPEED;
    scrollSpeed = DEFAULT_SCROLL_SPEED;

    swapRotateSlide = false;

    // NOTE: Upstream defaults reverseRotate to true on Android and iOS.
    reverseRotate = false;

    reverseSlide  = false;
    reverseScroll = false;

    moveInertia   = DEFAULT_MOVE_INERTIA;
    rotateInertia = DEFAULT_ROTATE_INERTIA;

    // NOTE: reverseSwipe and pointerRollScale (touch), double press and press move are out of
    //       scope.

    rotating = false;
    sliding  = false;

    slideButton = -1;
}

//-------------------------------------------------------------------------------------------------
// Interface

// SPARK: PointerControls.constructor (pointerdown listener)
void WSplatPointerControls::mousePress(QMouseEvent * event)
{
    // NOTE: The browser treats a mouse as a single pointer, a second button pressed while one is
    //       held fires a pointermove, not a pointerdown.
    if (event->buttons() != event->button()) return;

    QPointF point = event->position();

    int button = getButton(event->button());

    // NOTE: Determine if we're starting a rotation pointer action.
    bool isRotate = ((swapRotateSlide == false
                      &&
                      rotating == false
                      &&
                      button == 0)
                     ||
                     (swapRotateSlide
                      &&
                      sliding
                      &&
                      rotating == false
                      &&
                      button == 1));

    // NOTE: Qt grabs the mouse implicitly on press, like setPointerCapture.
    if (isRotate)
    {
        rotating = true;

        last     = point;
        position = point;
    }
    else if (sliding == false)
    {
        // NOTE: If it's not a rotation action and we're not yet sliding, the next pointer
        //       activates a sliding action.
        sliding = true;

        slideButton = button;

        last     = point;
        position = point;
    }

    // NOTE: Dual press (two fingers) and double or triple press detection are out of scope.
}

// SPARK: PointerControls.constructor (pointermove listener)
void WSplatPointerControls::mouseMove(QMouseEvent * event)
{
    // NOTE: The position is in device independent pixels, like clientX minus the canvas left.
    if (rotating || sliding) position = event->position();
}

// SPARK: PointerControls.constructor (pointerUp)
void WSplatPointerControls::mouseRelease(QMouseEvent * event)
{
    // NOTE: The browser fires pointerup once every mouse button is released.
    if (event->buttons() != Qt::NoButton) return;

    // NOTE: The dual press release is out of scope.
    if (rotating)
    {
        rotating = false;
    }
    else sliding = false;

    // NOTE: The double press callback is out of scope.
}

// SPARK: PointerControls.constructor (wheel listener)
void WSplatPointerControls::wheel(QWheelEvent * event)
{
    QPointF pixels = wheelPixels(event);

    scroll += QVector3D(pixels.x(), pixels.y(), 0);
}

// NOTE: Like a pointercancel, it also drops the pending scroll and the inertia.
void WSplatPointerControls::clear()
{
    rotating = false;
    sliding  = false;

    scroll = QVector3D();

    rotateVelocity = QVector3D();
    moveVelocity   = QVector3D();
}

// SPARK: PointerControls.update
bool WSplatPointerControls::update(float deltaTime, WSplatControlsObject & control)
{
    // NOTE: Velocities divide by the frame time.
    if (enable == false
        ||
        (deltaTime > 0.0f) == false)
    {
        return false;
    }

    bool updated = false;

    // NOTE: The dual press branch (two finger slide, pinch and roll) is out of scope. We're in
    //       rotating or FPS mode.

    QVector3D rotate;

    if (rotating)
    {
        QPointF delta = position - last;

        last = position;

        rotate = QVector3D(delta.x(), delta.y(), 0);

        rotate *= rotateSpeed * (reverseRotate ? -1 : 1);

        // NOTE: Update rotation velocity from last delta.
        rotateVelocity = rotate * (1 / deltaTime);

        if (rotate.length() > MOVEMENT_THRESHOLD) updated = true;
    }
    else
    {
        // NOTE: Continue to rotate with inertia.
        rotateVelocity *= std::exp(-deltaTime / rotateInertia);

        rotate += rotateVelocity * deltaTime;

        if (rotateVelocity.length() * 0.1f > MOVEMENT_THRESHOLD) updated = true;
    }

    // NOTE: Apply rotation in Euler angles space.
    WSplatControlsEuler eulers = eulersAboutUp(control);

    eulers.y -= rotate.x();

    eulers.x = qMax(-M_PI / 2, qMin(M_PI / 2, eulers.x - rotate.y()));

    eulers.z *= std::exp(-DEFAULT_ROLL_SPRING * deltaTime);

    applyEulers(control, eulers);

    if (sliding)
    {
        QPointF delta = position - last;

        last = position;

        // NOTE: Slide on plane depending on center / right mouse button.
        QVector3D slide = (slideButton != 2) ? QVector3D(delta.x(), 0, delta.y())
                                             : QVector3D(delta.x(), -delta.y(), 0);

        slide *= slideSpeed * (reverseSlide ? -1 : 1);

        slide = applyQuaternion(slide, control.quaternion);

        control.position += slide;

        // NOTE: Update movement velocity from last delta.
        moveVelocity = slide * (1 / deltaTime);

        if (slide.length() > MOVEMENT_THRESHOLD) updated = true;
    }
    else
    {
        // NOTE: Press move is out of scope (pressMoveSpeed defaults to 0), the target stays at
        //       zero.
        QVector3D target;

        // NOTE: Continue to move with inertia.
        float s = std::exp(-deltaTime / moveInertia);

        // NOTE: Vector3.lerpVectors(target, moveVelocity, s).
        moveVelocity = target + (moveVelocity - target) * s;

        control.position += moveVelocity * deltaTime;

        if (moveVelocity.length() * 0.1f > MOVEMENT_THRESHOLD) updated = true;
    }

    QVector3D move = scroll * scrollSpeed;

    move = QVector3D(move.x(), move.z(), move.y());

    if (reverseScroll) move *= -1;

    move = applyQuaternion(move, control.quaternion);

    control.position += move;

    if (move.length() > MOVEMENT_THRESHOLD) updated = true;

    scroll = QVector3D();

    return updated;
}

bool WSplatPointerControls::isActive() const
{
    if (enable == false) return false;

    return (rotating
            ||
            sliding
            ||
            scroll.isNull() == false
            ||
            rotateVelocity.length() * 0.1f > MOVEMENT_THRESHOLD
            ||
            moveVelocity.length() * 0.1f > MOVEMENT_THRESHOLD);
}

//-------------------------------------------------------------------------------------------------
// Static functions
//-------------------------------------------------------------------------------------------------

// NOTE: The browser WheelEvent deltas are pixels, positive when scrolling down or right. Chrome
//       reports deltaY = 100 per notch. Qt reports 120 angle units per notch, positive when the
//       wheel turns away from the user (up) or to the left. So a notch maps to
//       -angleDelta * 100 / 120. When the device reports pixels (trackpads) we use -pixelDelta as
//       is. deltaZ has no Qt equivalent, and Chrome's Shift + wheel horizontal scrolling is not
//       reproduced.
/* static */ QPointF WSplatPointerControls::wheelPixels(const QWheelEvent * event)
{
    QPoint pixels = event->pixelDelta();

    if (pixels.isNull() == false) return QPointF(-pixels.x(), -pixels.y());

    QPoint angle = event->angleDelta();

    return QPointF(-angle.x() * WHEEL_ANGLE_TO_PIXEL, -angle.y() * WHEEL_ANGLE_TO_PIXEL);
}

//-------------------------------------------------------------------------------------------------
// WSplatControls
//-------------------------------------------------------------------------------------------------

//-------------------------------------------------------------------------------------------------
// Interface

// NOTE: The caller measures deltaTime, in seconds, in place of performance.now() and lastTime.
//       The camera argument only serves pinch and press move, which are out of scope.
// SPARK: SparkControls.update
bool WSplatControls::update(float deltaTime, WSplatControlsObject & control)
{
    bool updated = fpsMovement.update(deltaTime, control);

    if (pointerControls.update(deltaTime, control)) updated = true;

    // NOTE: Upstream only reports motion above the threshold, we also report held input (a still
    //       drag, keys down) and pending scroll so the caller keeps requesting frames.
    return (updated || isActive());
}

bool WSplatControls::isActive() const
{
    return (fpsMovement.isActive() || pointerControls.isActive());
}

void WSplatControls::clear()
{
    fpsMovement    .clear();
    pointerControls.clear();
}
