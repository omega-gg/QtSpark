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

// NOTE: One scene of test/scenes.json: load, render, dump and compare.

#include "splattest.h"

// Qt includes
#include <QJsonDocument>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QImage>
#include <QFile>
#include <QDir>

// Splat includes
#include <WPackedSplats.h>
#include <WSplatLoader.h>

// Test includes
#include <WSplatBench.h>

//-------------------------------------------------------------------------------------------------
// Scene
//-------------------------------------------------------------------------------------------------

WSplatCamera sceneCamera(const QJsonObject & object, const QSize & size)
{
    QVector3D position = WSplatBench::toVector3D(object.value("position"));

    QQuaternion rotation;

    if (object.contains("lookAt") && object.value("lookAt").isArray())
    {
        QVector3D target = WSplatBench::toVector3D(object.value("lookAt"));

        rotation = WSplatBench::lookAtRotation(position, target);
    }
    else rotation = WSplatBench::toQuaternion(object.value("quaternion"));

    WSplatCamera camera;

    camera.matrixWorld.translate(position);
    camera.matrixWorld.rotate   (rotation);

    camera.projectionMatrix.perspective(object.value("fov").toDouble(60.0),
                                        float(size.width()) / size.height(),
                                        object.value("near").toDouble(0.01),
                                        object.value("far") .toDouble(1000.0));

    return camera;
}

void sceneMesh(WSplatMesh & mesh, const QJsonObject & object)
{
    mesh.position = WSplatBench::toVector3D(object.value("position"));

    if (object.contains("quaternion"))
    {
        mesh.quaternion = WSplatBench::toQuaternion(object.value("quaternion"));
    }

    mesh.scale = object.value("scale").toDouble(1.0);
}

//-------------------------------------------------------------------------------------------------
// Files
//-------------------------------------------------------------------------------------------------

QByteArray readFile(const QString & fileName)
{
    QFile file(fileName);

    if (file.open(QIODevice::ReadOnly) == false) return QByteArray();

    return file.readAll();
}

void writeFile(const QString & fileName, const QByteArray & data)
{
    QFile file(fileName);

    if (file.open(QIODevice::WriteOnly)) file.write(data);
}

QByteArray wordsToBytes(const QVector<quint32> & words, int count)
{
    count = qMin(count, int(words.count()));

    return QByteArray(reinterpret_cast<const char *> (words.constData()),
                      qint64(count) * sizeof(quint32));
}

QJsonArray matrixToJson(const QMatrix4x4 & matrix)
{
    QJsonArray array;

    const float * data = matrix.constData();

    for (int i = 0; i < 16; i++) array.append(data[i]);

    return array;
}

//-------------------------------------------------------------------------------------------------
// Test
//-------------------------------------------------------------------------------------------------

bool runScene(Context & context, const QJsonObject & scene, const QSize & size,
              const QString & fixtures, const QString & reference, const QString & output,
              double minPsnr)
{
    QString name = scene.value("name").toString();

    Result result;

    QString error;

    QString fileName = fixtures + '/' + scene.value("file").toString();

    QSharedPointer<WPackedSplats> splats = WSplatLoader::loadPackedSplats(fileName, &error);

    if (splats.isNull())
    {
        qWarning("%s: FAIL, cannot load %s: %s", name.toUtf8().constData(),
                 fileName.toUtf8().constData(), error.toUtf8().constData());

        return false;
    }

    Target target;

    if (createTarget(context.rhi, target, size) == false)
    {
        qWarning("%s: FAIL, cannot create the render target.", name.toUtf8().constData());

        return false;
    }

    WSplatRenderer renderer;

    WSplatMesh mesh;

    mesh.splats = splats;

    sceneMesh(mesh, scene.value("mesh").toObject());

    WSplatCamera camera = sceneCamera(scene.value("camera").toObject(), size);

    renderer.setMesh   (mesh);
    renderer.setCamera (camera);
    renderer.setOptions(context.options);

    if (renderer.initialize(context.rhi, target.target) == false)
    {
        qWarning("%s: FAIL, unsupported backend.", name.toUtf8().constData());

        return false;
    }

    if (settle(context, renderer, target) == false)
    {
        WSplatRendererStats stats = renderer.stats();

        qWarning("%s: FAIL, the renderer did not settle (%d active, %d generates, %d sorts,"
                 " needsUpdate %d).", name.toUtf8().constData(), stats.activeSplats,
                 stats.generates, stats.sorts, int(renderer.needsUpdate()));

        return false;
    }

    renderer.requestCapture();

    QElapsedTimer timer;

    timer.start();

    while (renderer.captureReady() == false && timer.elapsed() < 10000)
    {
        renderFrame(context, renderer, target);
    }

    QImage image = readImage(context, renderer, target);

    // NOTE: The Splat performance options must not change the image: the scene is rendered
    //       with the faithful options, then with each option alone.
    {
        WSplatRenderOptions faithful = context.options;

        faithful.tightBounds   = false;
        faithful.cpuSortMetric = false;
        faithful.fastSort      = false;

        QImage faithfulImage = renderOptions(context, mesh, camera, target, faithful);

        WSplatRenderOptions options = faithful;

        options.tightBounds = true;

        compareOptions(result, "tightBounds", renderOptions(context, mesh, camera, target,
                                                            options), faithfulImage);

        options = faithful;

        options.cpuSortMetric = true;
        options.fastSort      = true;

        compareOptions(result, "cpuSortMetric + fastSort",
                       renderOptions(context, mesh, camera, target, options), faithfulImage);

        renderer.initialize(context.rhi, target.target);
    }

    // Dumps

    QString directory = output + '/' + name;

    QDir().mkpath(directory);

    int numSplats = splats->numSplats;

    QByteArray packed = wordsToBytes(splats->packed, numSplats * 4);
    QByteArray sh1    = wordsToBytes(splats->sh1,    numSplats * 2);
    QByteArray sh2    = wordsToBytes(splats->sh2,    numSplats * 4);
    QByteArray sh3    = wordsToBytes(splats->sh3,    numSplats * 4);

    writeFile(directory + "/packed.bin", packed);

    if (sh1.isEmpty() == false) writeFile(directory + "/sh1.bin", sh1);
    if (sh2.isEmpty() == false) writeFile(directory + "/sh2.bin", sh2);
    if (sh3.isEmpty() == false) writeFile(directory + "/sh3.bin", sh3);

    QByteArray accum    = renderer.capturedSplats  ();
    QByteArray metric   = renderer.capturedMetric  ();
    QByteArray ordering = renderer.capturedOrdering();

    writeFile(directory + "/accum.bin",    accum);
    writeFile(directory + "/metric.bin",   metric);
    writeFile(directory + "/ordering.bin", ordering);

    image.save(directory + "/render.png");

    WSplatRendererStats stats = renderer.stats();

    QVector3D viewOrigin = renderer.capturedViewOrigin();

    QJsonObject meta;

    meta.insert("numSplats",    numSplats);
    meta.insert("maxSplats",    splats->maxSplats);
    meta.insert("numSh",        splats->numSh());
    meta.insert("activeSplats", stats.activeSplats);
    meta.insert("viewOrigin",   QJsonArray({ viewOrigin.x(), viewOrigin.y(), viewOrigin.z() }));

    QJsonObject cameraObject;

    cameraObject.insert("matrixWorld",      matrixToJson(camera.matrixWorld));
    cameraObject.insert("projectionMatrix", matrixToJson(camera.projectionMatrix));

    meta.insert("camera",     cameraObject);
    meta.insert("renderSize", QJsonArray({ size.width(), size.height() }));
    meta.insert("backend",    context.backend);

    writeFile(directory + "/meta.json", QJsonDocument(meta).toJson());

    // Comparison

    compareSorts(result, metric);

    if (reference.isEmpty() == false)
    {
        QString base = reference + '/' + name + '/';

        if (QFile::exists(base + "packed.bin") == false)
        {
            result.info("no reference in " + base);
        }
        else
        {
            compareExact(result, "packed", packed, readFile(base + "packed.bin"));

            if (QFile::exists(base + "sh1.bin") || sh1.isEmpty() == false)
            {
                compareExact(result, "sh1", sh1, readFile(base + "sh1.bin"));
            }

            if (QFile::exists(base + "sh2.bin") || sh2.isEmpty() == false)
            {
                compareExact(result, "sh2", sh2, readFile(base + "sh2.bin"));
            }

            if (QFile::exists(base + "sh3.bin") || sh3.isEmpty() == false)
            {
                compareExact(result, "sh3", sh3, readFile(base + "sh3.bin"));
            }

            QByteArray referenceMetric = readFile(base + "metric.bin");

            compareAccumulator(result, accum, readFile(base + "accum.bin"));

            compareMetric(result, metric, referenceMetric);

            compareOrdering(result, ordering, readFile(base + "ordering.bin"), referenceMetric);

            compareImage(result, image, QImage(base + "render.png"), minPsnr);
        }
    }

    qInfo("%s: %s (%d splats, %d active, %d generates, %d sorts)", name.toUtf8().constData(),
          result.ok ? "ok" : "FAIL", numSplats, stats.activeSplats, stats.generates, stats.sorts);

    for (const QString & line : result.lines)
    {
        qInfo("%s", line.toUtf8().constData());
    }

    return result.ok;
}
