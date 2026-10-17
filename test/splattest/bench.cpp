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

// NOTE: The offscreen benchmark and the sort benchmark.

#include "splattest.h"

// Qt includes
#include <QElapsedTimer>
#include <QJsonArray>
#include <QFile>

// Splat includes
#include <WPackedSplats.h>
#include <WSplatLoader.h>
#include <WSplatSort.h>

// Test includes
#include <WSplatBench.h>

// C++ includes
#include <algorithm>
#include <functional>

//-------------------------------------------------------------------------------------------------
// Bench
//-------------------------------------------------------------------------------------------------

// NOTE: The orbit of test/bench.json as a camera, see WSplatBench::orbitPose.
static WSplatCamera orbitCamera(const QJsonObject & path, int frame, int frames,
                                const QSize & size, const QJsonObject & cameraObject)
{
    QVector3D   position;
    QQuaternion rotation;

    WSplatBench::orbitPose(path, frame, frames, &position, &rotation);

    WSplatCamera camera;

    camera.matrixWorld.translate(position);
    camera.matrixWorld.rotate   (rotation);

    camera.projectionMatrix.perspective(cameraObject.value("fov").toDouble(60.0),
                                        float(size.width()) / size.height(),
                                        cameraObject.value("near").toDouble(0.01),
                                        cameraObject.value("far") .toDouble(1000.0));

    return camera;
}

struct BenchSamples
{
    QVector<double> frameMs;
    QVector<double> cpuMs;
    QVector<double> gpuMs;
    QVector<double> readbackMs;
    QVector<double> sortMs;
    QVector<double> loadMs;
    QVector<double> wallFrameMs;

    void append(const BenchSamples & other)
    {
        frameMs     += other.frameMs;
        cpuMs       += other.cpuMs;
        gpuMs       += other.gpuMs;
        readbackMs  += other.readbackMs;
        sortMs      += other.sortMs;
        loadMs      += other.loadMs;
        wallFrameMs += other.wallFrameMs;
    }

    QJsonObject toJson() const
    {
        QJsonObject object;

        object.insert("frameMs",     WSplatBench::summarize(frameMs));
        object.insert("cpuMs",       WSplatBench::summarize(cpuMs));
        object.insert("gpuMs",       WSplatBench::summarize(gpuMs));
        object.insert("readbackMs",  WSplatBench::summarize(readbackMs));
        object.insert("sortMs",      WSplatBench::summarize(sortMs));
        object.insert("loadMs",      WSplatBench::summarize(loadMs));
        object.insert("wallFrameMs", WSplatBench::summarize(wallFrameMs));

        return object;
    }
};

// NOTE: One run: load, settle, warmup, then the measured orbit. An offscreen frame waits for the
//       GPU, so frameMs is the full frame latency, and wallFrameMs the throughput.
static bool benchRun(Context & context, const QJsonObject & scene, const QSize & size,
                     int frames, int warmup, const QString & fileName,
                     const QJsonObject & cameraObject, BenchSamples & samples, int * numSplats)
{
    QElapsedTimer timer;

    timer.start();

    QString error;

    QSharedPointer<WPackedSplats> splats = WSplatLoader::loadPackedSplats(fileName, &error);

    if (splats.isNull())
    {
        qWarning("Cannot load %s: %s", fileName.toUtf8().constData(),
                 error.toUtf8().constData());

        return false;
    }

    samples.loadMs.append(timer.nsecsElapsed() / 1000000.0);

    *numSplats = splats->numSplats;

    Target target;

    if (createTarget(context.rhi, target, size) == false) return false;

    WSplatRenderer renderer;

    WSplatMesh mesh;

    mesh.splats = splats;

    sceneMesh(mesh, scene.value("mesh").toObject());

    renderer.setMesh   (mesh);
    renderer.setOptions(context.options);

    QJsonObject path = scene.value("path").toObject();

    renderer.setCamera(orbitCamera(path, 0, frames, size, cameraObject));

    if (renderer.initialize(context.rhi, target.target) == false) return false;

    settle(context, renderer, target);

    int sorts = 0;

    QElapsedTimer wall;

    for (int i = 0; i < warmup + frames; i++)
    {
        // NOTE: The warmup replays poses 0..warmup-1 (modulo frames), see test/bench.json.
        int pose = (i < warmup) ? i % frames : i - warmup;

        if (i == warmup)
        {
            sorts = renderer.stats().sorts;

            wall.start();
        }

        renderer.setCamera(orbitCamera(path, pose, frames, size, cameraObject));

        QRhiCommandBuffer * cb;

        timer.restart();

        if (context.rhi->beginOffscreenFrame(&cb) != QRhi::FrameOpSuccess) return false;

        QElapsedTimer cpu;

        cpu.start();

        renderer.render(cb, target.target, QColor(0, 0, 0, 255));

        double cpuMs = cpu.nsecsElapsed() / 1000000.0;

        context.rhi->endOffscreenFrame();

        double frameMs = timer.nsecsElapsed() / 1000000.0;

        if (i < warmup) continue;

        samples.frameMs.append(frameMs);
        samples.cpuMs  .append(cpuMs);

        double gpuTime = cb->lastCompletedGpuTime();

        if (gpuTime > 0.0) samples.gpuMs.append(gpuTime * 1000.0);

        WSplatRendererStats stats = renderer.stats();

        if (stats.sorts != sorts)
        {
            sorts = stats.sorts;

            samples.readbackMs.append(stats.metricMs);
            samples.sortMs    .append(stats.sortMs);
        }
    }

    samples.wallFrameMs.append(wall.nsecsElapsed() / 1000000.0 / frames);

    return true;
}

QJsonObject benchScene(Context & context, const QJsonObject & scene, const QSize & size,
                       int frames, int warmup, int repeats, const QString & fixtures,
                       const QJsonObject & cameraObject)
{
    QJsonObject object;

    QString name = scene.value("name").toString();

    object.insert("name", name);
    object.insert("file", scene.value("file"));

    QString fileName = fixtures + '/' + scene.value("file").toString();

    if (QFile::exists(fileName) == false)
    {
        qInfo("%s: skipped, missing %s", name.toUtf8().constData(),
              fileName.toUtf8().constData());

        object.insert("skipped", "missing file");

        return object;
    }

    BenchSamples pooled;

    QJsonArray runs;

    int numSplats = 0;

    for (int i = 0; i < repeats; i++)
    {
        BenchSamples samples;

        if (benchRun(context, scene, size, frames, warmup, fileName, cameraObject, samples,
                     &numSplats) == false)
        {
            object.insert("skipped", "run failed");

            return object;
        }

        runs.append(samples.toJson());

        pooled.append(samples);
    }

    QJsonObject metrics = pooled.toJson();

    object.insert("numSplats", numSplats);
    object.insert("metrics",   metrics);
    object.insert("runs",      runs);

    auto median = [&metrics](const char * key) -> double
    {
        return metrics.value(key).toObject().value("median").toDouble(-1.0);
    };

    qInfo("%s: %d splats, frame %.2f ms (p95 %.2f), wall %.2f ms, cpu %.2f ms, gpu %.2f ms,"
          " readback %.2f ms, sort %.2f ms (%d sorts), load %.0f ms", name.toUtf8().constData(),
          numSplats, median("frameMs"),
          metrics.value("frameMs").toObject().value("p95").toDouble(), median("wallFrameMs"),
          median("cpuMs"), median("gpuMs"), median("readbackMs"), median("sortMs"),
          int(pooled.sortMs.count()), median("loadMs"));

    return object;
}

//-------------------------------------------------------------------------------------------------
// Sort bench
//-------------------------------------------------------------------------------------------------

// NOTE: Times the upstream sort32 against the fast sort on random metrics, median of 20 runs.
void sortBench()
{
    for (int count : { 322410, 2000000, 6000000 })
    {
        QVector<quint32> keys(count);

        quint32 seed = 12345;

        for (int i = 0; i < count; i++)
        {
            seed = seed * 1664525u + 1013904223u;

            float metric = float(seed >> 8) / float(1 << 24) * 20.0f;

            memcpy(&keys[i], &metric, sizeof(float));
        }

        QVector<quint32> ordering(count);

        auto time = [&](const std::function<void()> & function) -> double
        {
            QVector<double> times;

            for (int i = 0; i < 20; i++)
            {
                QElapsedTimer timer;

                timer.start();

                function();

                times.append(timer.nsecsElapsed() / 1000000.0);
            }

            std::sort(times.begin(), times.end());

            return times.at(times.count() / 2);
        };

        double upstream = time([&]()
        {
            WSplatSort::sort32Splats(count, keys.constData(), count, ordering.data());
        });

        double fast1 = time([&]()
        {
            WSplatSort::sort32SplatsFast(count, keys.constData(), ordering.data(), 1);
        });

        int threads = WSplatSort::idealThreads();

        double fastN = time([&]()
        {
            WSplatSort::sort32SplatsFast(count, keys.constData(), ordering.data(), threads);
        });

        qInfo("%d splats: upstream %.2f ms, fast %.2f ms (1 thread), %.2f ms (%d threads)",
              count, upstream, fast1, fastN, threads);
    }
}
