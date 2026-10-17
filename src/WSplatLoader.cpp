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

#include "WSplatLoader.h"

// Qt includes
#include <QFile>
#include <QtEndian>

// Splat includes
#include <WPackedSplats.h>
#include <WSplatPly.h>
#include <WSplatSpz.h>

//-------------------------------------------------------------------------------------------------
// Static variables
//-------------------------------------------------------------------------------------------------

static const quint32 LOADER_PLY_MAGIC = 0x00796c70; // "ply"

// NOTE: Gzip deflate.
static const quint32 LOADER_GZIP_MAGIC = 0x00088b1f;

static const quint32 LOADER_RAD_MAGIC       = 0x30444152; // "RAD0"
static const quint32 LOADER_RAD_CHUNK_MAGIC = 0x43444152; // "RADC"

//-------------------------------------------------------------------------------------------------
// Static functions
//-------------------------------------------------------------------------------------------------

static void setError(QString * error, const QString & message)
{
    if (error) *error = message;
}

//-------------------------------------------------------------------------------------------------

// SPARK: MultiDecoder::push (file type detection), getSplatFileType
// NOTE: Upstream reports an unknown type, without the extension fallback, when a gzip stream is
//       too short to inflate 4 bytes. We fall back to the extension, the decoder fails anyway.
/* static */ WSplatLoader::FileType WSplatLoader::getSplatFileType(const uchar * data,
                                                                   qint64 size,
                                                                   const QString & fileName)
{
    if (size < 4) return Unknown;

    quint32 magic = qFromLittleEndian<quint32>(data);

    if ((magic & 0x00ffffff) == LOADER_PLY_MAGIC) return Ply;

    if ((magic & 0x00ffffff) == LOADER_GZIP_MAGIC)
    {
        if (WSplatSpz::isSpz(data, size)) return Spz;
    }
    // NOTE: RAD is detected by its magic, but we do not support it.
    else if (magic == LOADER_RAD_MAGIC || magic == LOADER_RAD_CHUNK_MAGIC)
    {
        return Unknown;
    }

    return fileTypeFromExtension(fileName);
}

// SPARK: SplatFileType::from_pathname
// NOTE: Like upstream, the query ('?') and the fragment ('#') are stripped first, and the
//       extension is whatever follows the last '.' of the whole path.
/* static */ WSplatLoader::FileType WSplatLoader::fileTypeFromExtension(const QString & fileName)
{
    QString path = fileName;

    int index = path.indexOf('?');

    if (index != -1) path.truncate(index);

    index = path.indexOf('#');

    if (index != -1) path.truncate(index);

    QString extension = path.mid(path.lastIndexOf('.') + 1).toLower();

    if (extension == "ply") return Ply;
    if (extension == "spz") return Spz;

    return Unknown;
}

// SPARK: new_decoder
/* static */ bool WSplatLoader::decode(const uchar * data, qint64 size, FileType type,
                                       WSplatReceiver & receiver, QString * error)
{
    if (type == Ply)
    {
        return WSplatPly::decode(data, size, receiver, error);
    }
    else if (type == Spz)
    {
        return WSplatSpz::decode(data, size, receiver, error);
    }

    setError(error, "Unknown file type");

    return false;
}

/* static */ bool WSplatLoader::loadFile(const QString & fileName, WSplatReceiver & receiver,
                                         QString * error)
{
    QFile file(fileName);

    if (file.open(QIODevice::ReadOnly) == false)
    {
        setError(error, "Failed to open " + fileName + ": " + file.errorString());

        return false;
    }

    qint64 size = file.size();

    const uchar * data = NULL;

    if (size > 0) data = file.map(0, size);

    QByteArray content;

    // NOTE: An empty or unmappable file is read instead.
    if (data == NULL)
    {
        content = file.readAll();

        data = (const uchar *) content.constData();

        size = content.size();
    }

    FileType type = getSplatFileType(data, size, fileName);

    // NOTE: The mapping is released when the file is closed.
    return decode(data, size, type, receiver, error);
}

/* static */ QSharedPointer<WPackedSplats> WSplatLoader::loadPackedSplats(const QString & fileName,
                                                                         QString       * error)
{
    QSharedPointer<WPackedSplats> splats(new WPackedSplats);

    if (loadFile(fileName, *splats, error) == false) return QSharedPointer<WPackedSplats>();

    return splats;
}
