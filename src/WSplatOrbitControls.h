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

#ifndef WSPLATORBITCONTROLS_H
#define WSPLATORBITCONTROLS_H

// Qt includes
#include <QPointF>

// Splat includes
#include <WSplatControls.h>

// Forward declarations
class QMouseEvent;
class QWheelEvent;

//-------------------------------------------------------------------------------------------------
// WSplatOrbitControls
//-------------------------------------------------------------------------------------------------

// NOTE: The OrbitControls of Spark's editor: a perspective camera with +Y up, mouse and wheel
//       input. Damping, auto rotation, zoom to cursor, keys and touch are out of scope. The events
//       accumulate and update() applies them, once per frame.
class SPLAT_EXPORT WSplatOrbitControls
{
public:
    WSplatOrbitControls();

public: // Interface
    void mousePress  (QMouseEvent * event);
    void mouseMove   (QMouseEvent * event);
    void mouseRelease(QMouseEvent * event);

    void wheel(QWheelEvent * event);

    void clear();

    // NOTE: fov is the vertical field of view in degrees, height the view height in the units of
    //       the mouse events. Returns true when the object moved.
    bool update(WSplatControlsObject & object, float fov, float height);

    // NOTE: Returns true while a drag or a pending wheel step is in progress.
    bool isActive() const;

public: // Variables
    QVector3D target;

    float minDistance;
    float maxDistance;

    float minPolarAngle;
    float maxPolarAngle;

    float rotateSpeed;
    float zoomSpeed;
    float panSpeed;

    // NOTE: Internal state, mirrors OrbitControls.
    enum State { None, Rotate, Dolly, Pan };

    State state;

    QPointF start;

    // NOTE: The pending deltas in pixels, already scaled by rotateSpeed and panSpeed.
    QPointF rotateDelta;
    QPointF panDelta;

    float scale;
};

#endif // WSPLATORBITCONTROLS_H
