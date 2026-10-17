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

// NOTE: The comparisons against the Spark reference.

#include "splattest.h"

// Qt includes
#include <QImage>

// Splat includes
#include <WPackedSplats.h>
#include <WSplatSort.h>

// C++ includes
#include <cmath>
#include <cstring>

//-------------------------------------------------------------------------------------------------
// Comparison
//-------------------------------------------------------------------------------------------------

void compareExact(Result & result, const QString & name, const QByteArray & native,
                  const QByteArray & reference)
{
    if (reference.isNull())
    {
        result.info(name + ": no reference");

        return;
    }

    if (native.size() != reference.size())
    {
        result.check(false, QString("%1: size %2 != reference %3").arg(name)
                                                                  .arg(native.size())
                                                                  .arg(reference.size()));

        return;
    }

    const quint32 * a = reinterpret_cast<const quint32 *> (native   .constData());
    const quint32 * b = reinterpret_cast<const quint32 *> (reference.constData());

    int count = native.size() / 4;

    int mismatches = 0;
    int first      = -1;

    for (int i = 0; i < count; i++)
    {
        if (a[i] == b[i]) continue;

        if (first < 0) first = i;

        mismatches++;
    }

    if (mismatches == 0)
    {
        result.check(true, QString("%1: %2 words identical").arg(name).arg(count));
    }
    else result.check(false, QString("%1: %2 / %3 words differ, first at word %4"
                                     " (0x%5 != 0x%6)").arg(name).arg(mismatches).arg(count)
                                                       .arg(first)
                                                       .arg(a[first], 8, 16, QChar('0'))
                                                       .arg(b[first], 8, 16, QChar('0')));
}

// SPARK: decodeQuatOctXy88R8 (splatDefines.glsl)
static QQuaternion decodeQuatOctXy88R8(quint32 encoded)
{
    float u = float(encoded & 0xff) / 255.0f * 2.0f - 1.0f;
    float v = float((encoded >> 8) & 0xff) / 255.0f * 2.0f - 1.0f;

    QVector3D axis(u, v, 1.0f - std::fabs(u) - std::fabs(v));

    float t = qMax(-axis.z(), 0.0f);

    axis.setX(axis.x() + ((axis.x() >= 0.0f) ? -t : t));
    axis.setY(axis.y() + ((axis.y() >= 0.0f) ? -t : t));

    axis.normalize();

    float theta = float(encoded >> 16) / 255.0f * float(M_PI);

    return QQuaternion(std::cos(theta * 0.5f), axis * std::sin(theta * 0.5f));
}

static QQuaternion packedQuaternion(const quint32 * packed)
{
    return decodeQuatOctXy88R8(((packed[2] >> 16) & 0xffff) | ((packed[3] >> 8) & 0xff0000));
}

// NOTE: The accumulator output is computed on the GPU, so it is compared field by field after
//       decoding: centers within half float precision, 8 bit fields within one step, and the
//       quaternions by rotation (the octahedral bytes jump across the fold for close rotations).
void compareAccumulator(Result & result, const QByteArray & native,
                        const QByteArray & reference)
{
    if (reference.isNull())
    {
        result.info("accum: no reference");

        return;
    }

    if (native.size() != reference.size())
    {
        result.check(false, QString("accum: size %1 != reference %2").arg(native.size())
                                                                     .arg(reference.size()));

        return;
    }

    const quint32 * a = reinterpret_cast<const quint32 *> (native   .constData());
    const quint32 * b = reinterpret_cast<const quint32 *> (reference.constData());

    int count = native.size() / 16;

    int bytesOff  = 0;
    int centerOff = 0;
    int quatOff   = 0;

    float minDot = 1.0f;

    float maxCenter = 0.0f;
    int   maxByte   = 0;

    for (int i = 0; i < count; i++)
    {
        const quint32 * pa = a + i * 4;
        const quint32 * pb = b + i * 4;

        // Bytes: rgba (word0) and scales (word3).
        quint8 ba[7] =
        {
            quint8(pa[0]), quint8(pa[0] >> 8), quint8(pa[0] >> 16), quint8(pa[0] >> 24),
            quint8(pa[3]), quint8(pa[3] >> 8), quint8(pa[3] >> 16)
        };

        quint8 bb[7] =
        {
            quint8(pb[0]), quint8(pb[0] >> 8), quint8(pb[0] >> 16), quint8(pb[0] >> 24),
            quint8(pb[3]), quint8(pb[3] >> 8), quint8(pb[3] >> 16)
        };

        bool off = false;

        for (int j = 0; j < 7; j++)
        {
            int diff = qAbs(int(ba[j]) - int(bb[j]));

            maxByte = qMax(maxByte, diff);

            if (diff > 1) off = true;
        }

        if (off) bytesOff++;

        // NOTE: q and -q are the same rotation.
        float dot = std::fabs(QQuaternion::dotProduct(packedQuaternion(pa),
                                                      packedQuaternion(pb)));

        minDot = qMin(minDot, dot);

        if (dot < 0.99f) quatOff++;

        float ca[3] = { WPackedSplats::halfToFloat(pa[1] & 0xffff),
                        WPackedSplats::halfToFloat(pa[1] >> 16),
                        WPackedSplats::halfToFloat(pa[2] & 0xffff) };

        float cb[3] = { WPackedSplats::halfToFloat(pb[1] & 0xffff),
                        WPackedSplats::halfToFloat(pb[1] >> 16),
                        WPackedSplats::halfToFloat(pb[2] & 0xffff) };

        bool centerBad = false;

        for (int j = 0; j < 3; j++)
        {
            float diff = std::fabs(ca[j] - cb[j]);

            // NOTE: Two half float steps (relative 2^-10 each).
            float tolerance = std::fabs(cb[j]) * 0.002f + 1e-4f;

            if (diff > tolerance) centerBad = true;

            maxCenter = qMax(maxCenter, diff);
        }

        if (centerBad) centerOff++;
    }

    // NOTE: A few splats can land on the other side of a rounding boundary, allow 0.5%.
    int limit = qMax(1, count / 200);

    result.check(bytesOff <= limit, QString("accum bytes: %1 / %2 splats beyond 1 step"
                                            " (max step %3)").arg(bytesOff).arg(count)
                                                             .arg(maxByte));

    result.check(centerOff <= limit, QString("accum centers: %1 / %2 splats beyond tolerance"
                                             " (max diff %3)").arg(centerOff).arg(count)
                                                              .arg(maxCenter));

    result.check(quatOff <= limit, QString("accum rotations: %1 / %2 splats with |dot| < 0.99"
                                           " (min %3)").arg(quatOff).arg(count).arg(minDot));
}

static float toFloat(quint32 bits)
{
    float value;

    memcpy(&value, &bits, sizeof(float));

    return value;
}

void compareMetric(Result & result, const QByteArray & native,
                   const QByteArray & reference)
{
    if (reference.isNull())
    {
        result.info("metric: no reference");

        return;
    }

    if (native.size() != reference.size())
    {
        result.check(false, QString("metric: size %1 != reference %2").arg(native.size())
                                                                      .arg(reference.size()));

        return;
    }

    const quint32 * a = reinterpret_cast<const quint32 *> (native   .constData());
    const quint32 * b = reinterpret_cast<const quint32 *> (reference.constData());

    int count = native.size() / 4;

    int   off     = 0;
    float maxDiff = 0.0f;

    for (int i = 0; i < count; i++)
    {
        float fa = toFloat(a[i]);
        float fb = toFloat(b[i]);

        if (std::isinf(fa) || std::isinf(fb))
        {
            if (fa != fb) off++;

            continue;
        }

        float diff = std::fabs(fa - fb);

        maxDiff = qMax(maxDiff, diff);

        if (diff > std::fabs(fb) * 1e-4f + 1e-5f) off++;
    }

    result.check(off <= qMax(1, count / 1000), QString("metric: %1 / %2 beyond 1e-4 relative"
                                                       " (max diff %3)").arg(off).arg(count)
                                                                        .arg(maxDiff));
}

// NOTE: Ties and float noise make the exact order differ, so the native ordering is checked to
//       be back to front on the reference metric.
void compareOrdering(Result & result, const QByteArray & ordering,
                     const QByteArray & referenceOrdering,
                     const QByteArray & referenceMetric)
{
    if (referenceMetric.isNull())
    {
        result.info("ordering: no reference");

        return;
    }

    const quint32 * order  = reinterpret_cast<const quint32 *> (ordering       .constData());
    const quint32 * metric = reinterpret_cast<const quint32 *> (referenceMetric.constData());

    int count       = ordering.size() / 4;
    int metricCount = referenceMetric.size() / 4;

    if (referenceOrdering.isNull() == false)
    {
        int referenceCount = referenceOrdering.size() / 4;

        result.check(count == referenceCount, QString("activeSplats: %1 (reference %2)")
                                              .arg(count).arg(referenceCount));
    }

    int inversions = 0;

    float previous = INFINITY;

    for (int i = 0; i < count; i++)
    {
        quint32 index = order[i];

        if (int(index) >= metricCount)
        {
            inversions++;

            continue;
        }

        float value = toFloat(metric[index]);

        if (value > previous * (1.0f + 1e-4f) + 1e-5f) inversions++;

        previous = value;
    }

    result.check(inversions <= qMax(1, count / 1000),
                 QString("ordering: %1 / %2 inversions on the reference metric").arg(inversions)
                                                                                .arg(count));
}

// NOTE: The fast radix sort must give exactly the upstream ordering, on one and several
//       threads.
void compareSorts(Result & result, const QByteArray & metric)
{
    int count = metric.size() / 4;

    if (count == 0) return;

    const quint32 * keys = reinterpret_cast<const quint32 *> (metric.constData());

    QVector<quint32> upstream(count);
    QVector<quint32> single  (count);
    QVector<quint32> multi   (count);

    int a = WSplatSort::sort32Splats(count, keys, count, upstream.data());

    // NOTE: Below a million keys the fast sort is the upstream one, the key array is repeated
    //       to exercise the threads.
    QVector<quint32> large;

    for (int i = 0; i < 1000000 / count + 1; i++) large += QVector<quint32>(keys, keys + count);

    QVector<quint32> largeUpstream(large.count());
    QVector<quint32> largeFast    (large.count());

    int b = WSplatSort::sort32SplatsFast(count, keys, single.data(), 1);

    int c = WSplatSort::sort32Splats(large.count(), large.constData(), large.count(),
                                     largeUpstream.data());

    int d = WSplatSort::sort32SplatsFast(large.count(), large.constData(), largeFast.data(),
                                         WSplatSort::idealThreads());

    bool same = (a == b
                 &&
                 memcmp(upstream.constData(), single.constData(), a * 4) == 0
                 &&
                 c == d
                 &&
                 memcmp(largeUpstream.constData(), largeFast.constData(), c * 4) == 0);

    result.check(same, QString("fast sort: %1 (1 and %2 threads)")
                       .arg(same ? "identical to upstream" : "differs")
                       .arg(WSplatSort::idealThreads()));
}

// NOTE: Pixels that differ between the default and the faithful options. Two sorts of the same
//       metrics can order equal keys alike, so the images should be identical.
void compareOptions(Result & result, const QString & name, const QImage & image,
                    const QImage & faithful)
{
    QImage a = image   .convertToFormat(QImage::Format_RGBA8888);
    QImage b = faithful.convertToFormat(QImage::Format_RGBA8888);

    int differ  = 0;
    int maxDiff = 0;

    for (int y = 0; y < a.height(); y++)
    {
        const uchar * la = a.constScanLine(y);
        const uchar * lb = b.constScanLine(y);

        for (int x = 0; x < a.width(); x++)
        {
            int diff = 0;

            for (int c = 0; c < 4; c++)
            {
                diff = qMax(diff, qAbs(int(la[x * 4 + c]) - int(lb[x * 4 + c])));
            }

            if (diff) differ++;

            maxDiff = qMax(maxDiff, diff);
        }
    }

    // NOTE: The CPU and GPU metrics can round ties apart, a handful of pixels may blend in
    //       another order.
    int limit = a.width() * a.height() / 1000;

    result.check(differ <= limit && maxDiff <= 8,
                 QString("%1: %2 pixels differ from the faithful render (max diff %3)")
                 .arg(name).arg(differ).arg(maxDiff));
}

void compareImage(Result & result, const QImage & native, const QImage & reference,
                  double minPsnr)
{
    if (reference.isNull())
    {
        result.info("render: no reference");

        return;
    }

    QImage a = native   .convertToFormat(QImage::Format_RGBA8888);
    QImage b = reference.convertToFormat(QImage::Format_RGBA8888);

    if (a.size() != b.size())
    {
        result.check(false, "render: size mismatch");

        return;
    }

    double sum     = 0.0;
    int    maxDiff = 0;

    int count = a.width() * a.height() * 3;

    for (int y = 0; y < a.height(); y++)
    {
        const uchar * la = a.constScanLine(y);
        const uchar * lb = b.constScanLine(y);

        for (int x = 0; x < a.width(); x++)
        {
            for (int c = 0; c < 3; c++)
            {
                int diff = int(la[x * 4 + c]) - int(lb[x * 4 + c]);

                sum += diff * diff;

                maxDiff = qMax(maxDiff, qAbs(diff));
            }
        }
    }

    double mse = sum / count;

    double psnr = (mse == 0.0) ? 99.0 : 10.0 * std::log10(255.0 * 255.0 / mse);

    result.check(psnr >= minPsnr, QString("render: PSNR %1 dB (min %2), max diff %3")
                                  .arg(psnr, 0, 'f', 2).arg(minPsnr).arg(maxDiff));
}
