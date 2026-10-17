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

    Portions of Splat are ported from Spark (https://github.com/sparkjsdev/spark).
    Copyright (C) 2025 World Labs Technologies, Inc. MIT License, see LICENSE.spark.
*/
//=================================================================================================

// SPARK v2.3.1: rust/spark-rs/src/sort.rs
//               rust/spark-rs/src/lib.rs (sort32_splats)

#include "WSplatSort.h"

// Qt includes
#include <QThreadPool>
#include <QSemaphore>

// C++ includes
#include <cstring>
#include <cmath>
#include <thread>
#include <algorithm>
#include <vector>

//-------------------------------------------------------------------------------------------------
// Static variables

static const quint32 DEPTH_INFINITY_F32 = 0x7f800000;

// NOTE: The fast sort and the CPU metric only use threads from this many splats.
static const int WSPLATSORT_PARALLEL_MIN = 1000000;

// 16-bit radix (2 passes)
static const quint32 RADIX_BITS = 16;
static const int     RADIX_BASE = 1 << RADIX_BITS; // 65536
static const quint32 RADIX_MASK = RADIX_BASE - 1;

//-------------------------------------------------------------------------------------------------
// Static functions
//-------------------------------------------------------------------------------------------------

// SPARK: prefix_sum_exclusive
static quint32 prefixSumExclusive(quint32 * buckets, int count)
{
    quint32 sum = 0;

    for (int i = 0; i < count; i++)
    {
        quint32 tmp = buckets[i];

        buckets[i] = sum;

        sum += tmp;
    }

    return sum;
}

// NOTE: The threads of the parallel functions, kept alive between calls: creating threads costs
//       more than sorting a few hundred thousand keys. A pool of its own, so the sort never waits
//       behind the QtConcurrent tasks (the sort job itself runs in one of them).
static QThreadPool * sortPool()
{
    static QThreadPool * pool = []()
    {
        QThreadPool * pool = new QThreadPool;

        pool->setMaxThreadCount(WSplatSort::idealThreads());

        pool->setExpiryTimeout(-1);

        return pool;
    }();

    return pool;
}

// NOTE: Runs function(begin, end, thread) on 'threads' contiguous ranges of [0, count), the
//       last range on the calling thread.
template <typename T>
static void parallelFor(int count, int threads, const T & function)
{
    threads = qBound(1, threads, qMax(1, count));

    QSemaphore done;

    for (int i = 0; i < threads - 1; i++)
    {
        int begin = qint64(count) * i       / threads;
        int end   = qint64(count) * (i + 1) / threads;

        sortPool()->start([&function, &done, begin, end, i]()
        {
            function(begin, end, i);

            done.release();
        });
    }

    function(qint64(count) * (threads - 1) / threads, count, threads - 1);

    done.acquire(threads - 1);
}

//-------------------------------------------------------------------------------------------------
// WSplatSort32Buffers
//-------------------------------------------------------------------------------------------------

// SPARK: Sort32Buffers::ensure_size
void WSplatSort32Buffers::ensureSize(int maxSplats)
{
    if (readback.size() < maxSplats) readback.resize(maxSplats);
    if (ordering.size() < maxSplats) ordering.resize(maxSplats);
    if (scratch .size() < maxSplats) scratch .resize(maxSplats);

    if (buckets16lo.size() < RADIX_BASE) buckets16lo.resize(RADIX_BASE);
    if (buckets16hi.size() < RADIX_BASE) buckets16hi.resize(RADIX_BASE);
}

//-------------------------------------------------------------------------------------------------
// Static functions
//-------------------------------------------------------------------------------------------------

// SPARK: sort32_internal
/* static */ int WSplatSort::sort32Internal(WSplatSort32Buffers & buffers, int maxSplats,
                                            int numSplats, QString * error)
{
    // make sure our buffers can hold `max_splats`
    buffers.ensureSize(maxSplats);

    const quint32 * keys = buffers.readback.constData();

    quint32 * ordering    = buffers.ordering   .data();
    quint32 * buckets16lo = buffers.buckets16lo.data();
    quint32 * buckets16hi = buffers.buckets16hi.data();
    quint64 * scratch     = buffers.scratch    .data();

    // tally low and high buckets (branchless)
    std::memset(buckets16lo, 0, RADIX_BASE * sizeof(quint32));
    std::memset(buckets16hi, 0, RADIX_BASE * sizeof(quint32));

    for (int i = 0; i < numSplats; i++)
    {
        quint32 key = keys[i];

        quint32 valid = (key < DEPTH_INFINITY_F32);
        quint32 inv   = ~key;

        buckets16lo[inv & RADIX_MASK]  += valid;
        buckets16hi[inv >> RADIX_BITS] += valid;
    }

    // exclusive prefix sum, starting offsets
    quint32 activeSplats = prefixSumExclusive(buckets16lo, RADIX_BASE);

    prefixSumExclusive(buckets16hi, RADIX_BASE);

    // Pass #1: bucket by inv(low 16 bits)

    // scatter into scratch by low bits of inv
    for (int i = 0; i < numSplats; i++)
    {
        quint32 key = keys[i];

        if (key >= DEPTH_INFINITY_F32) continue;

        quint32 inv = ~key;
        quint32 lo  = inv & RADIX_MASK;

        quint32 pos = buckets16lo[lo];

        scratch[pos] = (quint64(inv) << 32) | quint64(i);

        buckets16lo[lo]++;
    }

    // Pass #2: bucket by inv(high 16 bits)

    // scatter into final ordering by high bits of inv
    for (quint32 i = 0; i < activeSplats; i++)
    {
        quint64 invIdx = scratch[i];

        quint32 idx = quint32(invIdx);
        quint32 hi  = quint32((invIdx >> 48) & RADIX_MASK);

        quint32 pos = buckets16hi[hi];

        ordering[pos] = idx;

        buckets16hi[hi]++;
    }

    // sanity check: last bucket should have consumed all entries
    if (buckets16hi[RADIX_BASE - 1] != activeSplats)
    {
        if (error)
        {
            *error = QString("Expected %1 active splats but got %2")
                     .arg(activeSplats).arg(buckets16hi[RADIX_BASE - 1]);
        }

        return -1;
    }

    return activeSplats;
}

// SPARK: TransformGsplat (center) and OutputSplatDepth (dyno/splats.ts, dyno/output.ts)
/* static */ void WSplatSort::computeMetrics(int numSplats, const quint32 * packed,
                                             const float * centers, float scale,
                                             const float * rotate, const float * translate,
                                             const float * viewCenter, const float * viewDir,
                                             bool sortRadial, quint32 * metrics, int threads)
{
    float qx = rotate[0];
    float qy = rotate[1];
    float qz = rotate[2];
    float qw = rotate[3];

    if (numSplats < WSPLATSORT_PARALLEL_MIN) threads = 1;

    parallelFor(numSplats, threads, [&](int begin, int end, int)
    {
        for (int i = begin; i < end; i++)
        {
            // NOTE: readPackedSplat: zero scales mark an inactive splat.
            if ((packed[i * 4 + 3] & 0xffffff) == 0)
            {
                metrics[i] = DEPTH_INFINITY_F32;

                continue;
            }

            const float * center = centers + i * 3;

            float x = center[0] * scale;
            float y = center[1] * scale;
            float z = center[2] * scale;

            // quatVec: t = 2 * cross(q.xyz, v), v + q.w * t + cross(q.xyz, t)
            float tx = 2.0f * (qy * z - qz * y);
            float ty = 2.0f * (qz * x - qx * z);
            float tz = 2.0f * (qx * y - qy * x);

            float wx = x + qw * tx + (qy * tz - qz * ty) + translate[0] - viewCenter[0];
            float wy = y + qw * ty + (qz * tx - qx * tz) + translate[1] - viewCenter[1];
            float wz = z + qw * tz + (qx * ty - qy * tx) + translate[2] - viewCenter[2];

            float metric;

            if (sortRadial)
            {
                metric = std::sqrt(wx * wx + wy * wy + wz * wz);
            }
            else metric = wx * viewDir[0] + wy * viewDir[1] + wz * viewDir[2] + 100.0f;

            std::memcpy(metrics + i, &metric, sizeof(float));
        }
    });
}

/* static */ int WSplatSort::sort32SplatsFast(int numSplats, const quint32 * readback,
                                              quint32 * ordering, int threads)
{
    // NOTE: Sorting the inverted keys ascending with a stable LSD radix gives one unique
    //       ordering whatever the digits are, so 11 bit digits (2048 buckets, they fit in the L1
    //       cache) give exactly the result of the two 16 bit passes of sort32.
    const int DIGIT_BITS = 11;
    const int DIGIT_BASE = 1 << DIGIT_BITS;
    const int DIGIT_MASK = DIGIT_BASE - 1;

    // NOTE: Below a million keys the upstream sort is faster, the threads do not pay off.
    if (numSplats < WSPLATSORT_PARALLEL_MIN)
    {
        return sort32Splats(numSplats, readback, numSplats, ordering);
    }

    threads = qMax(1, threads);

    static thread_local std::vector<quint64> bufferA;
    static thread_local std::vector<quint64> bufferB;
    static thread_local std::vector<quint32> histograms;

    if (bufferA.size() < size_t(numSplats)) bufferA.resize(numSplats);
    if (bufferB.size() < size_t(numSplats)) bufferB.resize(numSplats);

    histograms.assign(size_t(threads) * DIGIT_BASE, 0);

    // NOTE: The workers run on other threads, they must not name the thread_local buffers.
    quint64 * source = bufferA.data();
    quint64 * target = bufferB.data();

    quint32 * buckets = histograms.data();

    // Compaction: the active keys (key < +INF) as (inv << 32 | index), in index order, with the
    // histogram of the first digit.
    std::vector<int> valid(threads, 0);
    std::vector<int> begins(threads, 0);
    std::vector<int> ends(threads, 0);

    parallelFor(numSplats, threads, [&](int begin, int end, int thread)
    {
        int count = 0;

        for (int i = begin; i < end; i++)
        {
            if (readback[i] < DEPTH_INFINITY_F32) count++;
        }

        valid[thread] = count;
    });

    int activeSplats = 0;

    for (int thread = 0; thread < threads; thread++)
    {
        begins[thread] = activeSplats;

        activeSplats += valid[thread];

        ends[thread] = activeSplats;
    }

    parallelFor(numSplats, threads, [&](int begin, int end, int thread)
    {
        quint32 * counts = buckets + size_t(thread) * DIGIT_BASE;

        quint64 * out = source + begins[thread];

        for (int i = begin; i < end; i++)
        {
            quint32 key = readback[i];

            if (key >= DEPTH_INFINITY_F32) continue;

            quint32 inv = ~key;

            *out++ = (quint64(inv) << 32) | quint64(i);

            counts[inv & DIGIT_MASK]++;
        }
    });

    // Three passes: digits 0..10, 11..21, 22..31.
    for (int pass = 0; pass < 3; pass++)
    {
        int shift = 32 + pass * DIGIT_BITS;

        // NOTE: The first histogram comes from the compaction, over the same thread ranges.
        if (pass)
        {
            std::fill(histograms.begin(), histograms.end(), 0);

            for (int thread = 0; thread < threads; thread++)
            {
                begins[thread] = qint64(activeSplats) * thread       / threads;
                ends  [thread] = qint64(activeSplats) * (thread + 1) / threads;
            }

            parallelFor(threads, threads, [&](int thread, int, int)
            {
                quint32 * counts = buckets + size_t(thread) * DIGIT_BASE;

                for (int i = begins[thread]; i < ends[thread]; i++)
                {
                    counts[(source[i] >> shift) & DIGIT_MASK]++;
                }
            });
        }

        // Offsets: bucket major, thread minor, which keeps the pass stable.
        quint32 total = 0;

        for (int bucket = 0; bucket < DIGIT_BASE; bucket++)
        {
            for (int thread = 0; thread < threads; thread++)
            {
                quint32 & value = buckets[size_t(thread) * DIGIT_BASE + bucket];

                quint32 tmp = value;

                value = total;

                total += tmp;
            }
        }

        parallelFor(threads, threads, [&](int thread, int, int)
        {
            quint32 * offsets = buckets + size_t(thread) * DIGIT_BASE;

            for (int i = begins[thread]; i < ends[thread]; i++)
            {
                quint64 value = source[i];

                target[offsets[(value >> shift) & DIGIT_MASK]++] = value;
            }
        });

        std::swap(source, target);
    }

    parallelFor(activeSplats, threads, [&](int begin, int end, int)
    {
        for (int i = begin; i < end; i++) ordering[i] = quint32(source[i]);
    });

    return activeSplats;
}

/* static */ int WSplatSort::idealThreads()
{
    return qBound(1, int(std::thread::hardware_concurrency()), 8);
}

// SPARK: sort32_splats
/* static */ int WSplatSort::sort32Splats(int numSplats, const quint32 * readback, int maxSplats,
                                          quint32 * ordering)
{
    // NOTE: SORT32_BUFFERS is a thread local in the wasm module.
    static thread_local WSplatSort32Buffers buffers;

    buffers.ensureSize(maxSplats);

    std::memcpy(buffers.readback.data(), readback, numSplats * sizeof(quint32));

    QString error;

    int activeSplats = sort32Internal(buffers, maxSplats, numSplats, &error);

    if (activeSplats < 0)
    {
        qWarning("WSplatSort::sort32Splats: %s.", error.toUtf8().constData());

        return 0;
    }

    // Copy out ordering result
    if (activeSplats > 0)
    {
        std::memcpy(ordering, buffers.ordering.constData(), activeSplats * sizeof(quint32));
    }

    return activeSplats;
}
