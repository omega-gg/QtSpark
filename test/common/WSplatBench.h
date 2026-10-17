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
*/
//=================================================================================================

// NOTE: Shared by splattest --bench and splatviewer --bench, so both play the same camera path
//       and summarize the samples the same way as test/reference/bench.mjs. Header only.

#ifndef WSPLATBENCH_H
#define WSPLATBENCH_H

// Qt includes
#include <QJsonObject>
#include <QJsonArray>
#include <QVector3D>
#include <QQuaternion>
#include <QVector>

// C++ includes
#include <algorithm>
#include <cmath>

namespace WSplatBench
{

// NOTE: The summary of test/reference/README.md, p95 is nearest rank.
inline QJsonValue summarize(QVector<double> values)
{
    if (values.isEmpty()) return QJsonValue::Null;

    std::sort(values.begin(), values.end());

    int count = values.count();

    double sum = 0.0;

    for (double value : values) sum += value;

    QJsonObject object;

    object.insert("median", values.at(count / 2));
    object.insert("p95",    values.at(qMax(0, int(std::ceil(0.95 * count)) - 1)));
    object.insert("mean",   sum / count);
    object.insert("min",    values.first());
    object.insert("max",    values.last());
    object.insert("count",  count);

    return object;
}

inline QVector3D toVector3D(const QJsonValue & value, const QVector3D & fallback = QVector3D())
{
    QJsonArray array = value.toArray();

    if (array.count() < 3) return fallback;

    return QVector3D(array.at(0).toDouble(), array.at(1).toDouble(), array.at(2).toDouble());
}

// NOTE: JSON quaternions are [x, y, z, w] like THREE.Quaternion.
inline QQuaternion toQuaternion(const QJsonValue & value)
{
    QJsonArray array = value.toArray();

    if (array.count() < 4) return QQuaternion();

    return QQuaternion(array.at(3).toDouble(), array.at(0).toDouble(), array.at(1).toDouble(),
                       array.at(2).toDouble());
}

// NOTE: THREE.Object3D.lookAt for a camera: -Z toward the target, +Y up.
inline QQuaternion lookAtRotation(const QVector3D & eye, const QVector3D & target)
{
    return QQuaternion::fromDirection((eye - target).normalized(), QVector3D(0.0f, 1.0f, 0.0f));
}

// NOTE: The orbit of test/bench.json, see test/reference/README.md: pose i of n looks at the
//       center from angle 2 * PI * turns * i / n, at the given radius and height.
inline void orbitPose(const QJsonObject & path, int pose, int poses, QVector3D * position,
                      QQuaternion * rotation)
{
    QVector3D center = toVector3D(path.value("center"));

    double radius = path.value("radius").toDouble(3.0);
    double height = path.value("height").toDouble(0.0);
    double turns  = path.value("turns") .toDouble(1.0);

    double angle = 2.0 * M_PI * turns * pose / poses;

    *position = center + QVector3D(radius * std::sin(angle), height, radius * std::cos(angle));

    *rotation = lookAtRotation(*position, center);
}

}

#endif // WSPLATBENCH_H
