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

#ifndef SPLATTEST_H
#define SPLATTEST_H

// Qt includes
#include <QJsonObject>
#include <QStringList>
#include <QSize>
#include <QOffscreenSurface>
#include <QVulkanInstance>
#include <rhi/qrhi.h>

// Splat includes
#include <WSplatRenderer.h>

// C++ includes
#include <memory>

// Forward declarations
class QImage;

//-------------------------------------------------------------------------------------------------
// Structures
//-------------------------------------------------------------------------------------------------

struct Context
{
    QRhi * rhi = NULL;

    WSplatRenderOptions options;

    QString backend;

    std::unique_ptr<QOffscreenSurface> surface;
#if QT_CONFIG(vulkan) && __has_include(<vulkan/vulkan.h>)
    std::unique_ptr<QVulkanInstance> vulkan;
#endif
};

struct Target
{
    QRhiTexture              * texture    = NULL;
    QRhiTextureRenderTarget  * target     = NULL;
    QRhiRenderPassDescriptor * renderPass = NULL;

    ~Target()
    {
        delete target;
        delete renderPass;
        delete texture;
    }
};

struct Result
{
    bool ok = true;

    QStringList lines;

    void check(bool condition, const QString & line)
    {
        if (condition == false) ok = false;

        lines.append(QString(condition ? "  ok    " : "  FAIL  ") + line);
    }

    void info(const QString & line)
    {
        lines.append("        " + line);
    }
};

//-------------------------------------------------------------------------------------------------
// Functions
//-------------------------------------------------------------------------------------------------

// rhi.cpp
bool createRhi(Context & context, const QString & backend, bool timestamps);

bool createTarget(QRhi * rhi, Target & target, const QSize & size);

bool renderFrame(Context & context, WSplatRenderer & renderer, Target & target,
                 QRhiReadbackResult * readback = NULL);

bool settle(Context & context, WSplatRenderer & renderer, Target & target);

QImage readImage(Context & context, WSplatRenderer & renderer, Target & target);

QImage renderOptions(Context & context, const WSplatMesh & mesh, const WSplatCamera & camera,
                     Target & target, const WSplatRenderOptions & options);

// scene.cpp
WSplatCamera sceneCamera(const QJsonObject & object, const QSize & size);

void sceneMesh(WSplatMesh & mesh, const QJsonObject & object);

QByteArray readFile (const QString & fileName);
void       writeFile(const QString & fileName, const QByteArray & data);

bool runScene(Context & context, const QJsonObject & scene, const QSize & size,
              const QString & fixtures, const QString & reference, const QString & output,
              double minPsnr);

// compare.cpp
void compareExact(Result & result, const QString & name, const QByteArray & native,
                  const QByteArray & reference);

void compareAccumulator(Result & result, const QByteArray & native,
                        const QByteArray & reference);

void compareMetric(Result & result, const QByteArray & native, const QByteArray & reference);

void compareOrdering(Result & result, const QByteArray & ordering,
                     const QByteArray & referenceOrdering, const QByteArray & referenceMetric);

void compareSorts(Result & result, const QByteArray & metric);

void compareOptions(Result & result, const QString & name, const QImage & image,
                    const QImage & faithful);

void compareImage(Result & result, const QImage & native, const QImage & reference,
                  double minPsnr);

// bench.cpp
QJsonObject benchScene(Context & context, const QJsonObject & scene, const QSize & size,
                       int frames, int warmup, int repeats, const QString & fixtures,
                       const QJsonObject & cameraObject);

void sortBench();

#endif // SPLATTEST_H
