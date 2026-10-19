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

#ifndef SPLAT_H
#define SPLAT_H

// Qt includes
#include <QtGlobal>

//-------------------------------------------------------------------------------------------------
// Defines

#if defined(SPLAT_LIBRARY)
#   define SPLAT_EXPORT Q_DECL_EXPORT
#else
#   define SPLAT_EXPORT Q_DECL_IMPORT
#endif

// NOTE: The Spark version this code is synced with. See upstream/spark.pin and resync.md.
#define SPLAT_SPARK_VERSION "2.3.1"

//-------------------------------------------------------------------------------------------------
// SPARK v2.3.1: src/defines.ts

#define SPLAT_TEX_WIDTH_BITS  11
#define SPLAT_TEX_HEIGHT_BITS 11
#define SPLAT_TEX_LAYER_BITS  (SPLAT_TEX_WIDTH_BITS + SPLAT_TEX_HEIGHT_BITS)

#define SPLAT_TEX_WIDTH  (1 << SPLAT_TEX_WIDTH_BITS)
#define SPLAT_TEX_HEIGHT (1 << SPLAT_TEX_HEIGHT_BITS)

#define SPLAT_TEX_MIN_HEIGHT 1

#define SPLAT_LN_SCALE_MIN -12.0f
#define SPLAT_LN_SCALE_MAX   9.0f

// NOTE: The ordering texture stores 4 indices per RGBA32UI texel, 4096 texels per row.
#define SPLAT_ORDERING_WIDTH    4096
#define SPLAT_ORDERING_PER_ROW 16384

#endif // SPLAT_H
