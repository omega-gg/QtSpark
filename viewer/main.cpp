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

// Qt includes
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QCommandLineParser>
#include <QSurfaceFormat>
#include <QUrl>
#include <QTimer>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>

// Splat includes
#include <WSplatView.h>

// Viewer includes
#include "WViewerBench.h"

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
// Functions
//-------------------------------------------------------------------------------------------------

static bool setBackend(const QString & name)
{
    if      (name == "opengl") QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
    else if (name == "vulkan") QQuickWindow::setGraphicsApi(QSGRendererInterface::Vulkan);
    else if (name == "d3d11")  QQuickWindow::setGraphicsApi(QSGRendererInterface::Direct3D11);
    else if (name == "d3d12")  QQuickWindow::setGraphicsApi(QSGRendererInterface::Direct3D12);
    else if (name == "metal")  QQuickWindow::setGraphicsApi(QSGRendererInterface::Metal);
    else return false;

    return true;
}

static bool hasArgument(int argc, char * argv[], const char * name)
{
    for (int i = 1; i < argc; i++)
    {
        if (qstrcmp(argv[i], name) == 0) return true;
    }

    return false;
}

int main(int argc, char * argv[])
{
    bool bench = hasArgument(argc, argv, "--bench");

    // NOTE: Desktop OpenGL needs GLSL 4.20 for packHalf2x16, see src/shaders.pri.
    QSurfaceFormat format = QSurfaceFormat::defaultFormat();

    format.setVersion(4, 3);

    format.setProfile(QSurfaceFormat::CoreProfile);

    if (bench)
    {
        // NOTE: Vsync off (QRhiSwapChain::NoVSync), GPU timestamps on, and one pixel per
        //       logical pixel so the window size is the render size.
        format.setSwapInterval(0);

        qputenv("QSG_RHI_PROFILE", "1");

        qputenv("QT_ENABLE_HIGHDPI_SCALING", "0");
    }

    QSurfaceFormat::setDefaultFormat(format);

    QGuiApplication application(argc, argv);

    application.setApplicationName("splatviewer");

    QCommandLineParser parser;

    parser.setApplicationDescription("Gaussian splat viewer (Spark port on Qt RHI).");

    parser.addHelpOption();

    QCommandLineOption backend("backend", "Graphics API: opengl, vulkan, d3d11, d3d12, metal.",
                               "name");

    parser.addOption(backend);

    QCommandLineOption quitAfter("quit-after", "Quit after this many milliseconds.", "ms");

    QCommandLineOption screenshot("screenshot", "Save the window to this file before quitting.",
                                  "file");

    parser.addOption(quitAfter);
    parser.addOption(screenshot);

    QCommandLineOption benchOption("bench", "Windowed benchmark with this file (test/bench.json).",
                                   "file");

    QCommandLineOption fixturesOption("fixtures", "Bench fixtures (default build/fixtures).",
                                      "path", "build/fixtures");

    QCommandLineOption repeatsOption("repeats", "Bench repeats (default 3).", "n", "3");

    QCommandLineOption widthOption ("width",  "Bench width (default 1920).",  "px", "1920");
    QCommandLineOption heightOption("height", "Bench height (default 1080).", "px", "1080");

    QCommandLineOption outputOption("output", "Bench output file.", "file");

    QCommandLineOption faithfulOption("faithful", "Disable the Splat performance options.");

    parser.addOptions({ benchOption, fixturesOption, repeatsOption, widthOption, heightOption,
                        outputOption, faithfulOption });

    parser.addPositionalArgument("file", "A .ply or .spz file to open.");

    parser.process(application);

    if (parser.isSet(backend) && setBackend(parser.value(backend)) == false)
    {
        qWarning("Unknown backend %s.", parser.value(backend).toUtf8().constData());

        return 1;
    }

    qmlRegisterType<WSplatView>("Splat", 1, 0, "SplatView");

    QQmlApplicationEngine engine;

    QUrl source;

    QStringList arguments = parser.positionalArguments();

    if (arguments.isEmpty() == false) source = QUrl::fromUserInput(arguments.first(), ".");

    engine.rootContext()->setContextProperty("initialSource", source);

    engine.load(QUrl("qrc:/qml/Main.qml"));

    if (engine.rootObjects().isEmpty()) return 1;

    if (parser.isSet(faithfulOption))
    {
        QQuickWindow * window = qobject_cast<QQuickWindow *> (engine.rootObjects().first());

        WSplatView * view = window->findChild<WSplatView *>();

        if (view) view->setFaithful(true);
    }

    if (parser.isSet(benchOption))
    {
        QQuickWindow * window = qobject_cast<QQuickWindow *> (engine.rootObjects().first());

        WSplatView * view = window->findChild<WSplatView *>();

        QFile file(parser.value(benchOption));

        if (view == NULL || file.open(QIODevice::ReadOnly) == false)
        {
            qWarning("Cannot start the benchmark.");

            return 1;
        }

        window->resize(parser.value(widthOption).toInt(), parser.value(heightOption).toInt());

        WViewerBench * runner = new WViewerBench(window, view,
                                                 QJsonDocument::fromJson(file.readAll()).object(),
                                                 parser.value(fixturesOption),
                                                 parser.value(repeatsOption).toInt(),
                                                 parser.value(outputOption));

        runner->start();
    }

    // NOTE: For smoke tests: grab the window and quit after a delay.
    if (parser.isSet(quitAfter))
    {
        QQuickWindow * window = qobject_cast<QQuickWindow *> (engine.rootObjects().first());

        QString fileName = parser.value(screenshot);

        QTimer::singleShot(parser.value(quitAfter).toInt(), &application, [window, fileName]()
        {
            if (window && fileName.isEmpty() == false) window->grabWindow().save(fileName);

            QCoreApplication::quit();
        });
    }

    return application.exec();
}
