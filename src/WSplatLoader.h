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

// SPARK v2.3.1: src/SplatLoader.ts (getSplatFileType)
//               rust/spark-lib/src/decoder.rs (SplatFileType, MultiDecoder dispatch)

#ifndef WSPLATLOADER_H
#define WSPLATLOADER_H

// Qt includes
#include <QSharedPointer>
#include <QString>

// C++ includes
#include <functional>

// Splat includes
#include <Splat.h>

// Forward declarations
class WSplatReceiver;
class WPackedSplats;

class SPLAT_EXPORT WSplatLoader
{
public:
    enum FileType
    {
        Unknown,
        Ply,
        Spz
    };

public: // Static functions
    // NOTE: Detects the type from magic bytes first, then from the file extension.
    static FileType getSplatFileType(const uchar * data, qint64 size,
                                     const QString & fileName = QString());

    static FileType fileTypeFromExtension(const QString & fileName);

    // NOTE: Synchronous, call it from a worker thread. Returns false and fills 'error' on failure.
    static bool decode(const uchar * data, qint64 size, FileType type,
                       WSplatReceiver & receiver, QString * error);

    static bool loadFile(const QString & fileName, WSplatReceiver & receiver, QString * error);

    // NOTE: Convenience: loads a file into a new WPackedSplats. Returns null on failure, or when
    //       'canceled' returns true during the load (error "Canceled").
    static QSharedPointer<WPackedSplats> loadPackedSplats(const QString & fileName,
                                                          QString       * error,
                                                          const std::function<bool()> & canceled
                                                          = std::function<bool()>());
};

#endif // WSPLATLOADER_H
