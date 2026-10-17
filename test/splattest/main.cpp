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

// NOTE: Headless test and benchmark tool. It renders the scenes of test/scenes.json with an
//       offscreen QRhi, writes the same dumps as test/reference/reference.mjs, and compares them
//       with the Spark reference. See test/reference/README.md for the file formats.

#include "splattest.h"

// Qt includes
#include <QGuiApplication>
#include <QCommandLineParser>
#include <QJsonDocument>
#include <QJsonArray>
#include <QDateTime>
#include <QDir>

//-------------------------------------------------------------------------------------------------
// Hybrid GPU

#ifdef Q_OS_WIN
// NOTE: Laptops with two GPUs run this executable on the discrete one (NVIDIA Optimus and AMD
//       PowerXpress read these exports).
extern "C"
{
    __declspec(dllexport) unsigned long NvOptimusEnablement                  = 1;
    __declspec(dllexport) int           AmdPowerXpressRequestHighPerformance = 1;
}
#endif

//-------------------------------------------------------------------------------------------------
// Main
//-------------------------------------------------------------------------------------------------

int main(int argc, char * argv[])
{
    QGuiApplication application(argc, argv);

    QCommandLineParser parser;

    parser.setApplicationDescription("Splat test and benchmark tool.");

    parser.addHelpOption();

    QCommandLineOption backendOption("backend", "gl, vulkan, d3d11 or d3d12 (default d3d11).",
                                     "name", "d3d11");

    QCommandLineOption scenesOption("scenes", "Scene list (default test/scenes.json).", "file",
                                    "test/scenes.json");

    QCommandLineOption fixturesOption("fixtures", "Fixture folder (default build/fixtures).",
                                      "path", "build/fixtures");

    QCommandLineOption referenceOption("reference", "Spark reference folder to compare with.",
                                       "path");

    QCommandLineOption outputOption("output", "Output folder (default build/native/<backend>).",
                                    "path");

    QCommandLineOption sceneOption("scene", "Only run this scene.", "name");

    QCommandLineOption psnrOption("min-psnr", "Minimum image PSNR in dB (default 30).", "db",
                                  "30");

    QCommandLineOption benchOption("bench", "Benchmark with this file (test/bench.json).",
                                   "file");

    QCommandLineOption widthOption ("width",  "Bench width (default 1920).",  "px", "1920");
    QCommandLineOption heightOption("height", "Bench height (default 1080).", "px", "1080");

    QCommandLineOption framesOption("frames", "Bench frames (default from bench.json).", "n");

    QCommandLineOption repeatsOption("repeats", "Bench repeats (default 3).", "n", "3");

    QCommandLineOption fastOption("fast", "Enable the Splat performance options.");

    QCommandLineOption enableOption("enable", "Enable some performance options: tightBounds,"
                                    " cpuSortMetric, fastSort (comma separated).", "options");

    QCommandLineOption sortBenchOption("sort-bench", "Time the sorts on random metrics.");

    QCommandLineOption adapterOption("adapter", "D3D adapter or Vulkan physical device index.",
                                     "index");

    parser.addOptions({ backendOption, scenesOption, fixturesOption, referenceOption,
                        outputOption, sceneOption, psnrOption, benchOption, widthOption,
                        heightOption, framesOption, repeatsOption,
                        adapterOption, fastOption, enableOption,
                        sortBenchOption });

    parser.process(application);

    if (parser.isSet(sortBenchOption))
    {
        sortBench();

        return 0;
    }

    QString backend = parser.value(backendOption);

    Context context;

    if (parser.isSet(fastOption))
    {
        context.options.tightBounds   = true;
        context.options.cpuSortMetric = true;
        context.options.fastSort      = true;
    }

    for (const QString & name : parser.value(enableOption).split(',', Qt::SkipEmptyParts))
    {
        if      (name == "tightBounds")   context.options.tightBounds   = true;
        else if (name == "cpuSortMetric") context.options.cpuSortMetric = true;
        else if (name == "fastSort")      context.options.fastSort      = true;
        else
        {
            qWarning("Unknown option %s.", name.toUtf8().constData());

            return 2;
        }
    }

    // NOTE: Read by the Qt D3D11, D3D12 and Vulkan backends when they pick the adapter.
    if (parser.isSet(adapterOption))
    {
        QByteArray index = parser.value(adapterOption).toLatin1();

        qputenv("QT_D3D_ADAPTER_INDEX",        index);
        qputenv("QT_VK_PHYSICAL_DEVICE_INDEX", index);
    }

    bool bench = parser.isSet(benchOption);

    if (createRhi(context, backend, bench) == false)
    {
        qWarning("Cannot create a QRhi for backend %s.", backend.toUtf8().constData());

        return 2;
    }

    qInfo("Backend: %s (%s)", context.rhi->backendName(),
          context.rhi->driverInfo().deviceName.constData());

    QString fixtures = parser.value(fixturesOption);

    int code = 0;

    if (bench)
    {
        QJsonObject config = QJsonDocument::fromJson(readFile(parser.value(benchOption)))
                             .object();

        int frames = config.value("frames").toInt(600);
        int warmup = config.value("warmup").toInt(60);

        if (parser.isSet(framesOption)) frames = parser.value(framesOption).toInt();

        int repeats = qMax(1, parser.value(repeatsOption).toInt());

        QSize size(parser.value(widthOption).toInt(), parser.value(heightOption).toInt());

        QJsonArray scenes;

        for (const QJsonValue & value : config.value("scenes").toArray())
        {
            QJsonObject scene = value.toObject();

            if (parser.isSet(sceneOption)
                &&
                scene.value("name").toString() != parser.value(sceneOption)) continue;

            scenes.append(benchScene(context, scene, size, frames, warmup, repeats, fixtures,
                                     config.value("camera").toObject()));
        }

        QJsonObject output;

        output.insert("tool",      "splattest");
        output.insert("backend",   backend);
        output.insert("ref",       "v" SPLAT_SPARK_VERSION);
        output.insert("gpu",       QString(context.rhi->driverInfo().deviceName));
        output.insert("timestamp", QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
        output.insert("width",     size.width());
        output.insert("height",    size.height());
        output.insert("frames",    frames);
        output.insert("warmup",    warmup);
        output.insert("repeats",   repeats);
        output.insert("scenes",    scenes);

        QDir().mkpath("build/bench");

        QString fileName = QString("build/bench/native-%1-%2.json")
                           .arg(backend)
                           .arg(QDateTime::currentDateTime().toString("yyyyMMdd-hhmmss"));

        writeFile(fileName, QJsonDocument(output).toJson());

        qInfo("Wrote %s", fileName.toUtf8().constData());
    }
    else
    {
        QJsonObject config = QJsonDocument::fromJson(readFile(parser.value(scenesOption)))
                             .object();

        QJsonArray size = config.value("size").toArray();

        QSize renderSize(size.at(0).toInt(256), size.at(1).toInt(256));

        QString output = parser.value(outputOption);

        if (output.isEmpty()) output = "build/native/" + backend;

        double minPsnr = parser.value(psnrOption).toDouble();

        int failures = 0;
        int count    = 0;

        for (const QJsonValue & value : config.value("scenes").toArray())
        {
            QJsonObject scene = value.toObject();

            if (parser.isSet(sceneOption)
                &&
                scene.value("name").toString() != parser.value(sceneOption)) continue;

            count++;

            if (runScene(context, scene, renderSize, fixtures, parser.value(referenceOption),
                         output, minPsnr) == false)
            {
                failures++;
            }
        }

        qInfo("%d / %d scenes passed on %s.", count - failures, count,
              backend.toUtf8().constData());

        if (failures) code = 1;
    }

    delete context.rhi;

    return code;
}
