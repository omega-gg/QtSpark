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

#ifndef WVIEWERBENCH_H
#define WVIEWERBENCH_H

// Qt includes
#include <QObject>
#include <QJsonObject>
#include <QJsonArray>
#include <QElapsedTimer>
#include <QPointer>
#include <QVector>

// Forward declarations
class QQuickWindow;
class WSplatView;

// NOTE: The windowed benchmark: plays the orbits of test/bench.json in a SplatView, in a real
//       swapchain with vsync off, and writes the result schema of test/reference/README.md.
class WViewerBench : public QObject
{
    Q_OBJECT

public:
    WViewerBench(QQuickWindow * window, WSplatView * view, const QJsonObject & config,
                 const QString & fixtures, int repeats, const QString & output);

public: // Interface
    void start();

private: // Functions
    void nextRun();

    void setPose(int pose);

    void finishRun();

    void finish();

private slots:
    void onStatusChanged();

    void onFrameSwapped();

private: // Variables
    enum State { Idle, Loading, Settling, Running };

    QPointer<QQuickWindow> window;
    QPointer<WSplatView>   view;

    QJsonObject config;

    QString fixtures;
    QString output;

    int repeats;

    int frames;
    int warmup;

    QJsonArray scenes;
    QJsonArray results;

    int sceneIndex;
    int repeat;

    State state;

    int frame;
    int sorts;
    int settled;

    QElapsedTimer timer;
    QElapsedTimer wall;

    qint64 last;

    double loadMs;

    QVector<double> frameMs;
    QVector<double> gpuMs;
    QVector<double> readbackMs;
    QVector<double> sortMs;
    QVector<double> loadTimes;
    QVector<double> wallFrameMs;

    QJsonArray runs;

    int numSplats;
};

#endif // WVIEWERBENCH_H
