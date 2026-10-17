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

// SPARK v2.3.1: rust/spark-lib/src/spz.rs

#ifndef WSPLATSPZ_H
#define WSPLATSPZ_H

// Qt includes
#include <QString>

// Splat includes
#include <Splat.h>

// Forward declarations
class WSplatReceiver;

class SPLAT_EXPORT WSplatSpz
{
public: // Static functions
    // NOTE: Decodes a whole gzip compressed SPZ buffer (versions 1 to 3). Returns false and fills
    //       'error' on failure.
    static bool decode(const uchar * data, qint64 size, WSplatReceiver & receiver,
                       QString * error);

    // NOTE: Returns true when the gzip stream starts with the SPZ magic ("NGSP").
    static bool isSpz(const uchar * data, qint64 size);
};

#endif // WSPLATSPZ_H
