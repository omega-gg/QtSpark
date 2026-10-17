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

#include "WViewerBench.h"

// Qt includes
#include <QQuickWindow>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QDateTime>
#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QUrl>

// Splat includes
#include <WSplatView.h>

// Test includes
#include <WSplatBench.h>

//-------------------------------------------------------------------------------------------------
// Ctor / dtor
//-------------------------------------------------------------------------------------------------

WViewerBench::WViewerBench(QQuickWindow * window, WSplatView * view, const QJsonObject & config,
                           const QString & fixtures, int repeats, const QString & output)
    : QObject(view), window(window), view(view), config(config), fixtures(fixtures),
      output(output), repeats(qMax(1, repeats))
{
    frames = config.value("frames").toInt(600);
    warmup = config.value("warmup").toInt(60);

    scenes = config.value("scenes").toArray();

    sceneIndex = 0;
    repeat     = 0;

    state = Idle;

    frame   = 0;
    sorts   = 0;
    settled = 0;

    last = 0;

    loadMs = 0.0;

    numSplats = 0;

    QJsonObject camera = config.value("camera").toObject();

    view->setFov  (camera.value("fov") .toDouble(60.0));
    view->setZNear(camera.value("near").toDouble(0.01));
    view->setZFar (camera.value("far") .toDouble(1000.0));

    view->setControlsEnabled(false);

    connect(view, SIGNAL(statusChanged()), this, SLOT(onStatusChanged()));

    // NOTE: frameSwapped comes from the render thread.
    connect(window, SIGNAL(frameSwapped()), this, SLOT(onFrameSwapped()), Qt::QueuedConnection);
}

//-------------------------------------------------------------------------------------------------
// Interface
//-------------------------------------------------------------------------------------------------

void WViewerBench::start()
{
    timer.start();

    nextRun();
}

//-------------------------------------------------------------------------------------------------
// Private functions
//-------------------------------------------------------------------------------------------------

void WViewerBench::nextRun()
{
    while (sceneIndex < scenes.count())
    {
        QJsonObject scene = scenes.at(sceneIndex).toObject();

        QString fileName = fixtures + '/' + scene.value("file").toString();

        if (QFile::exists(fileName) == false)
        {
            qInfo("%s: skipped, missing %s", scene.value("name").toString().toUtf8().constData(),
                  fileName.toUtf8().constData());

            QJsonObject result;

            result.insert("name",    scene.value("name"));
            result.insert("file",    scene.value("file"));
            result.insert("skipped", "missing file");

            results.append(result);

            sceneIndex++;

            continue;
        }

        // NOTE: The bench meshes are either OpenCV captures (quaternion [1, 0, 0, 0]) or identity.
        QJsonArray quaternion = scene.value("mesh").toObject().value("quaternion").toArray();

        if (quaternion.count() == 4 && quaternion.at(0).toDouble() == 1.0)
        {
            view->setOrientation(WSplatView::OpenCV);
        }
        else view->setOrientation(WSplatView::OpenGL);

        state = Loading;

        setPose(0);

        loadMs = timer.nsecsElapsed() / 1000000.0;

        // NOTE: Reset the source so every repeat loads the file again.
        view->setSource(QUrl());
        view->setSource(QUrl::fromLocalFile(QFileInfo(fileName).absoluteFilePath()));

        return;
    }

    finish();
}

// NOTE: The orbit of test/bench.json, see WSplatBench::orbitPose.
void WViewerBench::setPose(int pose)
{
    QJsonObject path = scenes.at(sceneIndex).toObject().value("path").toObject();

    QVector3D   position;
    QQuaternion rotation;

    WSplatBench::orbitPose(path, pose, frames, &position, &rotation);

    view->setCameraPosition(position);
    view->setCameraRotation(rotation);
}

void WViewerBench::finishRun()
{
    QJsonObject run;

    QVector<double> load = { loadMs };

    double wallMs = wall.nsecsElapsed() / 1000000.0 / frames;

    run.insert("loadMs",      WSplatBench::summarize(load));
    run.insert("wallFrameMs", WSplatBench::summarize({ wallMs }));

    runs.append(run);

    loadTimes  .append(loadMs);
    wallFrameMs.append(wallMs);

    repeat++;

    if (repeat < repeats)
    {
        nextRun();

        return;
    }

    QJsonObject scene = scenes.at(sceneIndex).toObject();

    QJsonObject metrics;

    metrics.insert("frameMs",     WSplatBench::summarize(frameMs));
    metrics.insert("cpuMs",       QJsonValue::Null);
    metrics.insert("gpuMs",       WSplatBench::summarize(gpuMs));
    metrics.insert("readbackMs",  WSplatBench::summarize(readbackMs));
    metrics.insert("sortMs",      WSplatBench::summarize(sortMs));
    metrics.insert("loadMs",      WSplatBench::summarize(loadTimes));
    metrics.insert("wallFrameMs", WSplatBench::summarize(wallFrameMs));

    QJsonObject result;

    result.insert("name",      scene.value("name"));
    result.insert("file",      scene.value("file"));
    result.insert("numSplats", numSplats);
    result.insert("metrics",   metrics);
    result.insert("runs",      runs);

    results.append(result);

    auto median = [&metrics](const char * key) -> double
    {
        return metrics.value(key).toObject().value("median").toDouble(-1.0);
    };

    qInfo("%s: %d splats, frame %.2f ms, wall %.2f ms, gpu %.2f ms, readback %.2f ms,"
          " sort %.2f ms (%d sorts), load %.0f ms",
          scene.value("name").toString().toUtf8().constData(), numSplats, median("frameMs"),
          median("wallFrameMs"), median("gpuMs"), median("readbackMs"), median("sortMs"),
          int(sortMs.count()), median("loadMs"));

    frameMs    .clear();
    gpuMs      .clear();
    readbackMs .clear();
    sortMs     .clear();
    loadTimes  .clear();
    wallFrameMs.clear();

    runs = QJsonArray();

    repeat = 0;

    sceneIndex++;

    nextRun();
}

void WViewerBench::finish()
{
    state = Idle;

    QJsonObject result;

    QSize size = view->effectiveColorBufferSize();

    result.insert("tool",      "viewer");
    result.insert("backend",   view->backendName().toLower());
    result.insert("ref",       "v" SPLAT_SPARK_VERSION);
    result.insert("timestamp", QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    result.insert("width",     size.width());
    result.insert("height",    size.height());
    result.insert("frames",    frames);
    result.insert("warmup",    warmup);
    result.insert("repeats",   repeats);
    result.insert("scenes",    results);

    QString fileName = output;

    if (fileName.isEmpty())
    {
        QDir().mkpath("build/bench");

        fileName = QString("build/bench/viewer-%1-%2.json")
                   .arg(view->backendName().toLower())
                   .arg(QDateTime::currentDateTime().toString("yyyyMMdd-hhmmss"));
    }

    QFile file(fileName);

    if (file.open(QIODevice::WriteOnly)) file.write(QJsonDocument(result).toJson());

    qInfo("Wrote %s", fileName.toUtf8().constData());

    QCoreApplication::quit();
}

//-------------------------------------------------------------------------------------------------
// Private slots
//-------------------------------------------------------------------------------------------------

void WViewerBench::onStatusChanged()
{
    if (state != Loading) return;

    if (view->status() == WSplatView::Error)
    {
        qWarning("Load failed: %s", view->error().toUtf8().constData());

        QCoreApplication::exit(1);

        return;
    }

    if (view->status() != WSplatView::Ready) return;

    loadMs = view->loadTime();

    numSplats = view->splatCount();

    state = Settling;

    settled = 0;

    setPose(0);

    view->update();
}

void WViewerBench::onFrameSwapped()
{
    if (state == Settling)
    {
        QVariantMap statistics = view->statistics();

        // NOTE: Settled once the first sort landed, plus a few frames.
        if (statistics.value("activeSplats").toInt() > 0) settled++;

        if (settled < 10)
        {
            view->update();

            return;
        }

        state = Running;

        frame = 0;

        sorts = statistics.value("sorts").toInt();

        last = timer.nsecsElapsed();
    }

    if (state != Running) return;

    qint64 now = timer.nsecsElapsed();

    if (frame > warmup)
    {
        frameMs.append((now - last) / 1000000.0);

        QVariantMap statistics = view->statistics();

        double gpu = statistics.value("gpuMs").toDouble();

        if (gpu > 0.0) gpuMs.append(gpu);

        int count = statistics.value("sorts").toInt();

        if (count != sorts)
        {
            sorts = count;

            readbackMs.append(statistics.value("metricMs").toDouble());
            sortMs    .append(statistics.value("sortMs")    .toDouble());
        }
    }

    last = now;

    if (frame == warmup) wall.start();

    if (frame == warmup + frames)
    {
        state = Idle;

        finishRun();

        return;
    }

    frame++;

    // NOTE: The warmup replays poses 0..warmup-1 (modulo frames).
    int pose = (frame <= warmup) ? (frame - 1) % frames : frame - warmup - 1;

    setPose(pose);

    view->update();
}
