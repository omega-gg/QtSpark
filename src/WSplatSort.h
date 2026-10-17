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

// SPARK v2.3.1: rust/spark-rs/src/sort.rs (Sort32Buffers, sort32_internal)
//               rust/spark-rs/src/lib.rs  (sort32_splats)

#ifndef WSPLATSORT_H
#define WSPLATSORT_H

// Qt includes
#include <QVector>
#include <QString>

// Splat includes
#include <Splat.h>

//-------------------------------------------------------------------------------------------------
// WSplatSort32Buffers
//-------------------------------------------------------------------------------------------------

struct SPLAT_EXPORT WSplatSort32Buffers
{
    void ensureSize(int maxSplats);

    QVector<quint32> readback;    // raw f32 bit patterns (one per splat)
    QVector<quint32> ordering;    // output indices
    QVector<quint32> buckets16lo; // bucket counts / offsets (length == RADIX_BASE)
    QVector<quint32> buckets16hi; // bucket counts / offsets (length == RADIX_BASE)
    QVector<quint64> scratch;     // scratch space for (key, index)
};

//-------------------------------------------------------------------------------------------------
// WSplatSort
//-------------------------------------------------------------------------------------------------

class SPLAT_EXPORT WSplatSort
{
public: // Static functions
    // NOTE: Two pass radix sort (base 2^16) of 32 bit float bit patterns, descending order
    //       (largest keys first). Keys >= +INF are dropped. Returns the active splat count, or -1
    //       and fills 'error' on failure.
    static int sort32Internal(WSplatSort32Buffers & buffers, int maxSplats, int numSplats,
                              QString * error = NULL);

    // NOTE: Sorts 'numSplats' keys of 'readback' (maxSplats long) into 'ordering' using thread
    //       local buffers, like the wasm entry point. Returns the active splat count.
    static int sort32Splats(int numSplats, const quint32 * readback, int maxSplats,
                            quint32 * ordering);

    // NOTE: Splat only: the sort metric of the accumulator pass computed on the CPU, written as
    //       float bits like the readback. 'packed' gives the active flags (zero scales are
    //       inactive, +INF), 'centers' the object space centers. 'rotate' is (x, y, z, w).
    static void computeMetrics(int numSplats, const quint32 * packed, const float * centers,
                               float scale, const float * rotate, const float * translate,
                               const float * viewCenter, const float * viewDir, bool sortRadial,
                               quint32 * metrics, int threads = 1);

    // NOTE: Splat only: a faster sort32Splats for a million keys and more. Three stable LSD
    //       passes of 11 bit digits on several threads (per thread histograms, bucket major
    //       offsets), the buckets fit in the L1 cache. Below a million keys it calls
    //       sort32Splats, which is faster there. The ordering is identical to sort32Splats.
    static int sort32SplatsFast(int numSplats, const quint32 * readback, quint32 * ordering,
                                int threads);

    // NOTE: The thread count for the parallel functions: the CPU cores, at most 8.
    static int idealThreads();
};

#endif // WSPLATSORT_H
