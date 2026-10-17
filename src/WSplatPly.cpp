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

// SPARK v2.3.1: rust/spark-lib/src/ply.rs

#include "WSplatPly.h"

// Qt includes
#include <QByteArrayView>
#include <QHash>
#include <QList>
#include <QStringDecoder>
#include <QVector>
#include <QtEndian>

// Splat includes
#include <WSplatMath.h>
#include <WPackedSplats.h>

// C++ includes
#include <climits>
#include <cmath>
#include <cstring>

// NOTE: Spark's math is plain IEEE arithmetic without fused multiply add. We keep the compiler
//       from contracting a * b + c, otherwise the decoding is not bit exact on FMA targets.
#if defined(__clang__)
#pragma STDC FP_CONTRACT OFF
#elif defined(__GNUC__)
#pragma GCC optimize("fp-contract=off")
#elif defined(_MSC_VER)
#pragma fp_contract(off)
#endif

//-------------------------------------------------------------------------------------------------
// Static variables
//-------------------------------------------------------------------------------------------------

static const quint32 PLY_MAGIC = 0x00796c70; // "ply"

static const int PLY_MAX_SPLAT_CHUNK = 65536;

static const float PLY_SH_C0 = 0.28209479177387814f;

static const int PLY_SUPER_CHUNK_SIZE = 256;

static const float PLY_DEFAULT_POINT_SCALE = 0.001f;

// NOTE: std::f32::consts::SQRT_2.
static const float PLY_SQRT_2 = 1.41421356237309504880f;

static const char * PLY_TERMINATOR = "end_header\n";

static const int PLY_TERMINATOR_SIZE = 11;

//-------------------------------------------------------------------------------------------------
// Private
//-------------------------------------------------------------------------------------------------

namespace
{

// SPARK: PlyPropertyType
enum PlyPropertyType
{
    PlyChar,
    PlyUchar,
    PlyShort,
    PlyUshort,
    PlyInt,
    PlyUint,
    PlyFloat,
    PlyDouble
};

// SPARK: PlyProperty
struct PlyProperty
{
    PlyPropertyType type   = PlyUchar;
    qint64          offset = 0;
};

// SPARK: PlyElementDesc
struct PlyElementDesc
{
    QByteArray name;

    quint64 count      = 0;
    qint64  recordSize = 0;

    QHash<QByteArray, PlyProperty> properties;
};

// SPARK: ParsedHeader
struct PlyParsedHeader
{
    QList<PlyElementDesc> elements;

    int vertex = -1;
    int chunk  = -1;
    int sh     = -1;

    bool isPointcloud = false;
    bool isSupersplat = false;
};

//-------------------------------------------------------------------------------------------------

// SPARK: PlyElementKind
enum PlyElementKind
{
    PlyKindChunk,
    PlyKindVertex,
    PlyKindSh,
    PlyKindOther
};

// SPARK: SuperSplatChunk
struct SuperSplatChunk
{
    float minX;
    float minY;
    float minZ;
    float maxX;
    float maxY;
    float maxZ;

    float minScaleX;
    float minScaleY;
    float minScaleZ;
    float maxScaleX;
    float maxScaleY;
    float maxScaleZ;

    float minR;
    float minG;
    float minB;
    float maxR;
    float maxG;
    float maxB;
};

//-------------------------------------------------------------------------------------------------

void setError(QString * error, const QString & message)
{
    if (error) *error = message;
}

quint32 readU32(const uchar * data)
{
    return qFromLittleEndian<quint32>(data);
}

float readF32(const uchar * data)
{
    quint32 bits = qFromLittleEndian<quint32>(data);

    float value;

    memcpy(&value, &bits, sizeof(value));

    return value;
}

double readF64(const uchar * data)
{
    quint64 bits = qFromLittleEndian<quint64>(data);

    double value;

    memcpy(&value, &bits, sizeof(value));

    return value;
}

// NOTE: Rust 'f64 as u32' saturates, and maps NaN to 0.
quint32 castU32(double value)
{
    if (value != value) return 0;

    if (value <= 0.0)          return 0;
    if (value >= 4294967295.0) return 0xffffffff;

    return (quint32) value;
}

//-------------------------------------------------------------------------------------------------

// SPARK: PlyPropertyType::size
int plyTypeSize(PlyPropertyType type)
{
    switch (type)
    {
        case PlyChar:
        case PlyUchar:  return 1;
        case PlyShort:
        case PlyUshort: return 2;
        case PlyDouble: return 8;
        default:        return 4;
    }
}

// SPARK: PlyPropertyType::get_raw_f32
float plyGetRawF32(const PlyProperty & property, const uchar * data, qint64 recordOffset)
{
    const uchar * at = data + recordOffset + property.offset;

    switch (property.type)
    {
        case PlyFloat:  return readF32(at);
        case PlyDouble: return (float) readF64(at);
        case PlyChar:   return (float) (qint8) at[0];
        case PlyUchar:  return (float) at[0];
        case PlyShort:  return (float) qFromLittleEndian<qint16>(at);
        case PlyUshort: return (float) qFromLittleEndian<quint16>(at);
        case PlyInt:    return (float) qFromLittleEndian<qint32>(at);
        default:        return (float) qFromLittleEndian<quint32>(at);
    }
}

// SPARK: PlyPropertyType::get_f32
// NOTE: Same as get_raw_f32, except that char and uchar are normalized by 255.
float plyGetF32(const PlyProperty & property, const uchar * data, qint64 recordOffset)
{
    const uchar * at = data + recordOffset + property.offset;

    switch (property.type)
    {
        case PlyChar:  return (float) (qint8) at[0] / 255.0f;
        case PlyUchar: return (float) at[0] / 255.0f;
        default:       return plyGetRawF32(property, data, recordOffset);
    }
}

// SPARK: PlyPropertyType::get_u32
quint32 plyGetU32(const PlyProperty & property, const uchar * data, qint64 recordOffset)
{
    const uchar * at = data + recordOffset + property.offset;

    switch (property.type)
    {
        case PlyUint:   return readU32(at);
        case PlyInt:    return readU32(at);
        case PlyFloat:  return readU32(at);
        case PlyUshort: return qFromLittleEndian<quint16>(at);
        case PlyShort:  return (quint32) (qint32) qFromLittleEndian<qint16>(at);
        case PlyUchar:  return at[0];
        case PlyChar:   return (quint32) (qint32) (qint8) at[0];
        default:        return castU32(readF64(at));
    }
}

//-------------------------------------------------------------------------------------------------

// SPARK: parse_property_type
bool parsePropertyType(const QByteArray & string, PlyPropertyType * type)
{
    // NOTE: Same order as PlyPropertyType.
    static const char * names[8] =
    {
        "char", "uchar", "short", "ushort", "int", "uint", "float", "double"
    };

    for (int i = 0; i < 8; i++)
    {
        if (string != names[i]) continue;

        *type = (PlyPropertyType) i;

        return true;
    }

    return false;
}

// NOTE: Rust usize::from_str: an optional '+' followed by decimal digits, no overflow.
bool parseCount(const QByteArray & string, quint64 * count)
{
    int index = 0;

    if (string.startsWith('+')) index = 1;

    if (index == string.length()) return false;

    quint64 value = 0;

    for (; index < string.length(); index++)
    {
        char character = string.at(index);

        if (character < '0' || character > '9') return false;

        quint64 digit = character - '0';

        if (value > (UINT64_MAX - digit) / 10) return false;

        value = value * 10 + digit;
    }

    *count = value;

    return true;
}

// NOTE: ASCII whitespace, like isspace in the C locale.
bool isAsciiSpace(char character)
{
    return (character == ' '  || character == '\t' || character == '\n'
            ||
            character == '\v' || character == '\f' || character == '\r');
}

// NOTE: str::split_whitespace on an already trimmed ASCII line.
QList<QByteArray> splitWhitespace(const QByteArray & line)
{
    QList<QByteArray> fields;

    int length = line.length();

    int index = 0;

    while (index < length)
    {
        while (index < length && isAsciiSpace(line.at(index))) index++;

        int start = index;

        while (index < length && isAsciiSpace(line.at(index)) == false) index++;

        if (index > start) fields.append(line.mid(start, index - start));
    }

    return fields;
}

// SPARK: parse_header
// NOTE: Rust trims Unicode whitespace, we only trim ASCII whitespace.
bool parseHeader(const QByteArray & header, PlyParsedHeader * parsed, QString * error)
{
    QList<PlyElementDesc> & elements = parsed->elements;

    bool hasCurrent = false;
    bool formatSeen = false;

    int lineIndex = 0;

    qsizetype length = header.length();
    qsizetype start  = 0;

    // NOTE: str::lines, a final line ending does not produce an empty line.
    while (start < length)
    {
        qsizetype end = header.indexOf('\n', start);

        if (end == -1) end = length;

        QByteArray line = header.mid(start, end - start).trimmed();

        start = end + 1;

        if (lineIndex++ == 0)
        {
            if (line != "ply")
            {
                setError(error, "Invalid PLY header");

                return false;
            }

            continue;
        }

        if (line.isEmpty()) continue;

        QList<QByteArray> fields = splitWhitespace(line);

        const QByteArray & keyword = fields.at(0);

        if (keyword == "format" && fields.count() == 3)
        {
            formatSeen = true;

            if (fields.at(1) != "binary_little_endian")
            {
                setError(error, "Unsupported PLY format: " + QString::fromUtf8(fields.at(1)));

                return false;
            }

            if (fields.at(2) != "1.0")
            {
                setError(error, "Unsupported PLY version: " + QString::fromUtf8(fields.at(2)));

                return false;
            }
        }
        else if (keyword == "comment" || keyword == "obj_info")
        {
            continue;
        }
        else if (keyword == "element" && fields.count() == 3)
        {
            PlyElementDesc element;

            element.name = fields.at(1);

            if (parseCount(fields.at(2), &element.count) == false)
            {
                setError(error, "Invalid PLY element count: " + QString::fromUtf8(fields.at(2)));

                return false;
            }

            elements.append(element);

            hasCurrent = true;
        }
        else if (keyword == "property")
        {
            if (fields.count() > 1 && fields.at(1) == "list")
            {
                setError(error, "PLY list properties are not supported");

                return false;
            }

            if (fields.count() != 3)
            {
                setError(error, "Invalid property line: " + QString::fromUtf8(line));

                return false;
            }

            if (hasCurrent == false)
            {
                setError(error, "Property outside of element");

                return false;
            }

            PlyProperty property;

            if (parsePropertyType(fields.at(1), &property.type) == false)
            {
                setError(error, "Unsupported PLY property type: "
                                + QString::fromUtf8(fields.at(1)));

                return false;
            }

            // SPARK: PlyElementBuilder::add_property
            PlyElementDesc & element = elements.last();

            property.offset = element.recordSize;

            element.recordSize += plyTypeSize(property.type);

            // NOTE: Like a HashMap collected in order, the last duplicate wins.
            element.properties.insert(fields.at(2), property);
        }
        else if (keyword == "end_header")
        {
            break;
        }
        else
        {
            setError(error, "Unsupported PLY header line: " + QString::fromUtf8(line));

            return false;
        }
    }

    if (formatSeen == false)
    {
        setError(error, "Missing PLY format line");

        return false;
    }

    for (int i = 0; i < elements.count(); i++)
    {
        const QByteArray & name = elements.at(i).name;

        if (name == "vertex" && parsed->vertex == -1) parsed->vertex = i;
        if (name == "chunk"  && parsed->chunk  == -1) parsed->chunk  = i;
        if (name == "sh"     && parsed->sh     == -1) parsed->sh     = i;
    }

    if (parsed->vertex == -1)
    {
        setError(error, "Missing vertex element");

        return false;
    }

    const QHash<QByteArray, PlyProperty> & properties = elements.at(parsed->vertex).properties;

    parsed->isPointcloud = (properties.contains("x")
                            &&
                            properties.contains("y")
                            &&
                            properties.contains("z")
                            &&
                            properties.contains("red")
                            &&
                            properties.contains("green")
                            &&
                            properties.contains("blue"));

    parsed->isSupersplat = (parsed->chunk != -1);

    return true;
}

//-------------------------------------------------------------------------------------------------

bool getProperty(const PlyElementDesc & element, const char * name, PlyProperty * property,
                 QString * error)
{
    QHash<QByteArray, PlyProperty>::const_iterator i = element.properties.constFind(name);

    if (i == element.properties.constEnd())
    {
        setError(error, QString("Missing %1 property").arg(name));

        return false;
    }

    *property = i.value();

    return true;
}

int countFRest(const PlyElementDesc & element)
{
    int count = 0;

    while (element.properties.contains("f_rest_" + QByteArray::number(count)))
    {
        count++;
    }

    return count;
}

// NOTE: Returns -1 for an invalid count.
int shDegreeFromFRest(int count)
{
    switch (count)
    {
        case 0:  return 0;
        case 9:  return 1;
        case 24: return 2;
        case 45: return 3;
        default: return -1;
    }
}

// SPARK: f_rest_offset
int fRestOffset(int degree)
{
    switch (degree)
    {
        case 0:  return 0;
        case 1:  return 3;
        case 2:  return 8;
        default: return 15;
    }
}

// SPARK: f_rest_name
QByteArray fRestName(int maxShDegree, int degree, int k, int d)
{
    int stride = fRestOffset(maxShDegree);
    int offset = fRestOffset(degree - 1);

    return "f_rest_" + QByteArray::number(stride * d + offset + k);
}

bool checkNumSplats(quint64 count, QString * error)
{
    if (count <= INT_MAX) return true;

    setError(error, QString("Too many splats: %1").arg(count));

    return false;
}

bool initSplats(WSplatReceiver & receiver, int numSplats, int maxShDegree, QString * error)
{
    WSplatInit init;

    init.numSplats   = numSplats;
    init.maxShDegree = maxShDegree;
    init.lodTree     = false;

    if (receiver.initSplats(init)) return true;

    setError(error, "Failed to initialize splats");

    return false;
}

//-------------------------------------------------------------------------------------------------

// SPARK: PlyDecoder::poll_data_pointcloud
bool decodePointCloud(const PlyElementDesc & vertex, const uchar * data, qint64 size,
                      WSplatReceiver & receiver, QString * error)
{
    // SPARK: PointCloudDecoderState::new
    PlyProperty xyz[3];
    PlyProperty rgb[3];

    if (getProperty(vertex, "x",     &xyz[0], error) == false) return false;
    if (getProperty(vertex, "y",     &xyz[1], error) == false) return false;
    if (getProperty(vertex, "z",     &xyz[2], error) == false) return false;
    if (getProperty(vertex, "red",   &rgb[0], error) == false) return false;
    if (getProperty(vertex, "green", &rgb[1], error) == false) return false;
    if (getProperty(vertex, "blue",  &rgb[2], error) == false) return false;

    PlyProperty alpha;

    bool hasAlpha = vertex.properties.contains("alpha");

    if (hasAlpha) alpha = vertex.properties.value("alpha");

    if (checkNumSplats(vertex.count, error) == false) return false;

    int numSplats = (int) vertex.count;

    qint64 recordSize = vertex.recordSize;

    // NOTE: Upstream fails after the last batch when the data is short, we fail before
    //       allocating anything.
    if (numSplats > size / recordSize)
    {
        setError(error, QString("Expected %1 splats, got %2").arg(numSplats)
                                                              .arg(size / recordSize));

        return false;
    }

    if (initSplats(receiver, numSplats, 0, error) == false) return false;

    int chunkSize = qMin(numSplats, PLY_MAX_SPLAT_CHUNK);

    QVector<float> outCenter (chunkSize * 3);
    QVector<float> outOpacity(chunkSize);
    QVector<float> outRgb    (chunkSize * 3);
    QVector<float> outScale  (chunkSize * 3);
    QVector<float> outQuat   (chunkSize * 4);

    int nextSplat = 0;

    qint64 offset = 0;

    while (nextSplat < numSplats)
    {
        int count = qMin(numSplats - nextSplat, PLY_MAX_SPLAT_CHUNK);

        for (int i = 0; i < count; i++)
        {
            int i3 = i * 3;
            int i4 = i * 4;

            qint64 base = offset + i * recordSize;

            for (int d = 0; d < 3; d++)
            {
                outCenter[i3 + d] = plyGetF32(xyz[d], data, base);
            }

            if (hasAlpha)
            {
                outOpacity[i] = plyGetF32(alpha, data, base);
            }
            else outOpacity[i] = 1.0f;

            for (int d = 0; d < 3; d++)
            {
                outRgb[i3 + d] = plyGetF32(rgb[d], data, base);
            }

            outScale[i3    ] = PLY_DEFAULT_POINT_SCALE;
            outScale[i3 + 1] = PLY_DEFAULT_POINT_SCALE;
            outScale[i3 + 2] = PLY_DEFAULT_POINT_SCALE;

            outQuat[i4    ] = 0.0f;
            outQuat[i4 + 1] = 0.0f;
            outQuat[i4 + 2] = 0.0f;
            outQuat[i4 + 3] = 1.0f;
        }

        WSplatProps props;

        props.center  = outCenter .constData();
        props.opacity = outOpacity.constData();
        props.rgb     = outRgb    .constData();
        props.scale   = outScale  .constData();
        props.quat    = outQuat   .constData();

        receiver.setBatch(nextSplat, count, props);

        nextSplat += count;

        offset += count * recordSize;
    }

    receiver.finish();

    return true;
}

// SPARK: PlyDecoder::poll_data_standard
bool decodeStandard(const PlyElementDesc & vertex, const uchar * data, qint64 size,
                    WSplatReceiver & receiver, QString * error)
{
    // SPARK: PlyDecoderState::new
    PlyProperty xyz[3];
    PlyProperty scale[3];
    PlyProperty rot[4];
    PlyProperty opLogi;
    PlyProperty fDc[3];

    if (getProperty(vertex, "x", &xyz[0], error) == false) return false;
    if (getProperty(vertex, "y", &xyz[1], error) == false) return false;
    if (getProperty(vertex, "z", &xyz[2], error) == false) return false;

    if (getProperty(vertex, "scale_0", &scale[0], error) == false) return false;
    if (getProperty(vertex, "scale_1", &scale[1], error) == false) return false;
    if (getProperty(vertex, "scale_2", &scale[2], error) == false) return false;

    // NOTE: The quaternion is stored w first, we read it as x y z w.
    if (getProperty(vertex, "rot_1", &rot[0], error) == false) return false;
    if (getProperty(vertex, "rot_2", &rot[1], error) == false) return false;
    if (getProperty(vertex, "rot_3", &rot[2], error) == false) return false;
    if (getProperty(vertex, "rot_0", &rot[3], error) == false) return false;

    if (getProperty(vertex, "opacity", &opLogi, error) == false) return false;

    if (getProperty(vertex, "f_dc_0", &fDc[0], error) == false) return false;
    if (getProperty(vertex, "f_dc_1", &fDc[1], error) == false) return false;
    if (getProperty(vertex, "f_dc_2", &fDc[2], error) == false) return false;

    int numFRest = countFRest(vertex);

    int maxShDegree = shDegreeFromFRest(numFRest);

    if (maxShDegree == -1)
    {
        setError(error, QString("Invalid number of f_rest properties: %1").arg(numFRest));

        return false;
    }

    // NOTE: f_rest is planar (all red coefficients, then green, then blue), the receiver wants
    //       it coefficient major with rgb interleaved.
    PlyProperty sh1[9];
    PlyProperty sh2[15];
    PlyProperty sh3[21];

    if (maxShDegree >= 1)
    {
        for (int i = 0; i < 9; i++)
        {
            sh1[i] = vertex.properties.value(fRestName(maxShDegree, 1, i / 3, i % 3));
        }
    }

    if (maxShDegree >= 2)
    {
        for (int i = 0; i < 15; i++)
        {
            sh2[i] = vertex.properties.value(fRestName(maxShDegree, 2, i / 3, i % 3));
        }
    }

    if (maxShDegree >= 3)
    {
        for (int i = 0; i < 21; i++)
        {
            sh3[i] = vertex.properties.value(fRestName(maxShDegree, 3, i / 3, i % 3));
        }
    }

    if (checkNumSplats(vertex.count, error) == false) return false;

    int numSplats = (int) vertex.count;

    qint64 recordSize = vertex.recordSize;

    // NOTE: Upstream fails after the last batch when the data is short, we fail before
    //       allocating anything.
    if (numSplats > size / recordSize)
    {
        setError(error, QString("Expected %1 splats, got %2").arg(numSplats)
                                                              .arg(size / recordSize));

        return false;
    }

    if (initSplats(receiver, numSplats, maxShDegree, error) == false) return false;

    int chunkSize = qMin(numSplats, PLY_MAX_SPLAT_CHUNK);

    QVector<float> outCenter (chunkSize * 3);
    QVector<float> outOpacity(chunkSize);
    QVector<float> outRgb    (chunkSize * 3);
    QVector<float> outScale  (chunkSize * 3);
    QVector<float> outQuat   (chunkSize * 4);

    QVector<float> outSh1;
    QVector<float> outSh2;
    QVector<float> outSh3;

    if (maxShDegree >= 1) outSh1.resize(chunkSize * 9);
    if (maxShDegree >= 2) outSh2.resize(chunkSize * 15);
    if (maxShDegree >= 3) outSh3.resize(chunkSize * 21);

    int nextSplat = 0;

    qint64 offset = 0;

    while (nextSplat < numSplats)
    {
        int count = qMin(numSplats - nextSplat, PLY_MAX_SPLAT_CHUNK);

        for (int i = 0; i < count; i++)
        {
            int i3 = i * 3;
            int i4 = i * 4;

            qint64 base = offset + i * recordSize;

            for (int d = 0; d < 3; d++)
            {
                outCenter[i3 + d] = plyGetF32(xyz[d], data, base);
            }

            float opLogistic = plyGetF32(opLogi, data, base);

            outOpacity[i] = 1.0f / (1.0f + WSplatMath::expf(-opLogistic));

            for (int d = 0; d < 3; d++)
            {
                outRgb[i3 + d] = 0.5f + plyGetF32(fDc[d], data, base) * PLY_SH_C0;
            }

            for (int d = 0; d < 3; d++)
            {
                outScale[i3 + d] = WSplatMath::expf(plyGetF32(scale[d], data, base));
            }

            float quat[4];

            for (int d = 0; d < 4; d++)
            {
                quat[d] = plyGetF32(rot[d], data, base);
            }

            // NOTE: Iterator sum, folded left to right.
            float quatMagnitude = std::sqrt(quat[0] * quat[0] + quat[1] * quat[1]
                                            + quat[2] * quat[2] + quat[3] * quat[3]);

            for (int d = 0; d < 4; d++)
            {
                outQuat[i4 + d] = quat[d] / quatMagnitude;
            }

            if (maxShDegree >= 1)
            {
                int i9 = i * 9;

                for (int d = 0; d < 9; d++)
                {
                    outSh1[i9 + d] = plyGetF32(sh1[d], data, base);
                }
            }

            if (maxShDegree >= 2)
            {
                int i15 = i * 15;

                for (int d = 0; d < 15; d++)
                {
                    outSh2[i15 + d] = plyGetF32(sh2[d], data, base);
                }
            }

            if (maxShDegree >= 3)
            {
                int i21 = i * 21;

                for (int d = 0; d < 21; d++)
                {
                    outSh3[i21 + d] = plyGetF32(sh3[d], data, base);
                }
            }
        }

        WSplatProps props;

        props.center  = outCenter .constData();
        props.opacity = outOpacity.constData();
        props.rgb     = outRgb    .constData();
        props.scale   = outScale  .constData();
        props.quat    = outQuat   .constData();

        if (maxShDegree >= 1) props.sh1 = outSh1.constData();
        if (maxShDegree >= 2) props.sh2 = outSh2.constData();
        if (maxShDegree >= 3) props.sh3 = outSh3.constData();

        receiver.setBatch(nextSplat, count, props);

        nextSplat += count;

        offset += count * recordSize;
    }

    receiver.finish();

    return true;
}

//-------------------------------------------------------------------------------------------------

// NOTE: Bytes covered by a set of properties, so a record can be checked against the data end.
qint64 propertyExtent(const PlyProperty * properties, int count)
{
    qint64 extent = 0;

    for (int i = 0; i < count; i++)
    {
        const PlyProperty & property = properties[i];

        extent = qMax(extent, property.offset + plyTypeSize(property.type));
    }

    return extent;
}

// SPARK: PlyDecoder::poll_data_supersplat
bool decodeSuperSplat(const PlyParsedHeader & parsed, const uchar * data, qint64 size,
                      WSplatReceiver & receiver, QString * error)
{
    // SPARK: SuperSplatState::new
    const PlyElementDesc & chunkDesc  = parsed.elements.at(parsed.chunk);
    const PlyElementDesc & vertexDesc = parsed.elements.at(parsed.vertex);

    quint64 expectedChunks = (vertexDesc.count + PLY_SUPER_CHUNK_SIZE - 1)
                             / PLY_SUPER_CHUNK_SIZE;

    if (chunkDesc.count < expectedChunks)
    {
        setError(error, QString("Not enough chunk records: have %1, need at least %2")
                        .arg(chunkDesc.count).arg(expectedChunks));

        return false;
    }

    // NOTE: minX, minY, minZ, maxX, maxY, maxZ, then the same for the scales.
    static const char * chunkNames[12] =
    {
        "min_x", "min_y", "min_z", "max_x", "max_y", "max_z",
        "min_scale_x", "min_scale_y", "min_scale_z", "max_scale_x", "max_scale_y", "max_scale_z"
    };

    // NOTE: minR, minG, minB, maxR, maxG, maxB, they are optional.
    static const char * colorNames[6] = { "min_r", "min_g", "min_b", "max_r", "max_g", "max_b" };

    PlyProperty chunkProps[12];

    for (int i = 0; i < 12; i++)
    {
        if (getProperty(chunkDesc, chunkNames[i], &chunkProps[i], error) == false) return false;
    }

    PlyProperty colorProps[6];

    bool hasColor[6];

    for (int i = 0; i < 6; i++)
    {
        hasColor[i] = chunkDesc.properties.contains(colorNames[i]);

        if (hasColor[i]) colorProps[i] = chunkDesc.properties.value(colorNames[i]);
    }

    // NOTE: packed_position, packed_rotation, packed_scale, packed_color.
    PlyProperty vertexProps[4];

    if (getProperty(vertexDesc, "packed_position", &vertexProps[0], error) == false
        ||
        getProperty(vertexDesc, "packed_rotation", &vertexProps[1], error) == false
        ||
        getProperty(vertexDesc, "packed_scale",    &vertexProps[2], error) == false
        ||
        getProperty(vertexDesc, "packed_color",    &vertexProps[3], error) == false)
    {
        return false;
    }

    QVector<PlyProperty> fRest;

    int numFRest = 0;

    int maxShDegree = 0;

    if (parsed.sh != -1)
    {
        const PlyElementDesc & shDesc = parsed.elements.at(parsed.sh);

        if (shDesc.count != vertexDesc.count)
        {
            setError(error, QString("SH element count (%1) must match vertex count (%2)")
                            .arg(shDesc.count).arg(vertexDesc.count));

            return false;
        }

        numFRest = countFRest(shDesc);

        maxShDegree = shDegreeFromFRest(numFRest);

        if (maxShDegree == -1)
        {
            setError(error, QString("Invalid number of f_rest properties: %1").arg(numFRest));

            return false;
        }

        fRest.resize(numFRest);

        QHash<QByteArray, PlyProperty>::const_iterator i = shDesc.properties.constBegin();

        while (i != shDesc.properties.constEnd())
        {
            const QByteArray & name = i.key();

            if (name.startsWith("f_rest_"))
            {
                quint64 index;

                if (parseCount(name.mid(7), &index) && index < (quint64) numFRest)
                {
                    fRest[index] = i.value();
                }
            }

            ++i;
        }
    }

    // NOTE: f_rest is planar, these map each interleaved coefficient to its f_rest index.
    int stride = numFRest / 3;

    int sh1Props[9];
    int sh2Props[15];
    int sh3Props[21];

    for (int k = 0; k < 3; k++)
    {
        for (int d = 0; d < 3; d++) sh1Props[k * 3 + d] = k + d * stride;
    }

    for (int k = 0; k < 5; k++)
    {
        for (int d = 0; d < 3; d++) sh2Props[k * 3 + d] = 3 + k + d * stride;
    }

    for (int k = 0; k < 7; k++)
    {
        for (int d = 0; d < 3; d++) sh3Props[k * 3 + d] = 8 + k + d * stride;
    }

    if (checkNumSplats(vertexDesc.count, error) == false) return false;

    int numSplats = (int) vertexDesc.count;

    // NOTE: Upstream fails at the end when the vertex element is short, we fail before
    //       allocating anything.
    qint64 vertexEnd = 0;

    bool fits = true;

    for (int i = 0; i <= parsed.vertex; i++)
    {
        const PlyElementDesc & element = parsed.elements.at(i);

        qint64 recordSize = element.recordSize;

        if (recordSize == 0) continue;

        if (element.count > (quint64) ((size - vertexEnd) / recordSize))
        {
            fits = false;

            break;
        }

        vertexEnd += (qint64) element.count * recordSize;
    }

    if (fits == false)
    {
        setError(error, QString("Expected %1 splats, got less").arg(numSplats));

        return false;
    }

    if (initSplats(receiver, numSplats, maxShDegree, error) == false) return false;

    qint64 chunkExtent = qMax(propertyExtent(chunkProps, 12), propertyExtent(colorProps, 6));

    qint64 vertexExtent = propertyExtent(vertexProps, 4);

    qint64 shExtent = propertyExtent(fRest.constData(), numFRest);

    QVector<SuperSplatChunk> chunks;

    QVector<float> outCenter;
    QVector<float> outOpacity;
    QVector<float> outRgb;
    QVector<float> outScale;
    QVector<float> outQuat;
    QVector<float> outSh1;
    QVector<float> outSh2;
    QVector<float> outSh3;

    QVector<float> tempRest(numFRest);

    int count = parsed.elements.count();

    QVector<quint64> reads(count, 0);

    int current = 0;

    qint64 offset = 0;

    while (current < count)
    {
        const PlyElementDesc & element = parsed.elements.at(current);

        qint64 recordSize = element.recordSize;

        // NOTE: Upstream divides by zero here.
        if (recordSize == 0)
        {
            setError(error, "Invalid PLY element: " + QString::fromUtf8(element.name));

            return false;
        }

        qint64 available = (size - offset) / recordSize;

        if (available == 0) break;

        quint64 read = reads.at(current);

        quint64 remaining = (element.count > read) ? element.count - read : 0;

        int chunk = (int) qMin(qMin(remaining, (quint64) available),
                               (quint64) PLY_MAX_SPLAT_CHUNK);

        if (chunk == 0) break;

        PlyElementKind kind = PlyKindOther;

        qint64 extent = 0;

        if (element.name == "chunk")
        {
            kind   = PlyKindChunk;
            extent = chunkExtent;
        }
        else if (element.name == "vertex")
        {
            kind   = PlyKindVertex;
            extent = vertexExtent;
        }
        else if (element.name == "sh")
        {
            kind   = PlyKindSh;
            extent = shExtent;
        }

        // NOTE: Properties come from the first element of a kind. A duplicate element with a
        //       smaller record would read past the data end, upstream panics there.
        if (offset + (chunk - 1) * recordSize + extent > size)
        {
            setError(error, "Invalid PLY element: " + QString::fromUtf8(element.name));

            return false;
        }

        if (kind == PlyKindChunk)
        {
            // SPARK: SuperSplatState::decode_chunks
            for (int i = 0; i < chunk; i++)
            {
                qint64 base = offset + i * recordSize;

                float values[12];

                for (int j = 0; j < 12; j++)
                {
                    values[j] = plyGetRawF32(chunkProps[j], data, base);
                }

                float colors[6];

                for (int j = 0; j < 6; j++)
                {
                    if (hasColor[j])
                    {
                        colors[j] = plyGetRawF32(colorProps[j], data, base);
                    }
                    else colors[j] = (j < 3) ? 0.0f : 1.0f;
                }

                SuperSplatChunk c;

                c.minX = values[0];
                c.minY = values[1];
                c.minZ = values[2];
                c.maxX = values[3];
                c.maxY = values[4];
                c.maxZ = values[5];

                c.minScaleX = values[6];
                c.minScaleY = values[7];
                c.minScaleZ = values[8];
                c.maxScaleX = values[9];
                c.maxScaleY = values[10];
                c.maxScaleZ = values[11];

                c.minR = colors[0];
                c.minG = colors[1];
                c.minB = colors[2];
                c.maxR = colors[3];
                c.maxG = colors[4];
                c.maxB = colors[5];

                chunks.append(c);
            }
        }
        else if (kind == PlyKindVertex)
        {
            // SPARK: SuperSplatState::decode_vertices
            if (outCenter.size() < chunk * 3)
            {
                outCenter .resize(chunk * 3);
                outOpacity.resize(chunk);
                outRgb    .resize(chunk * 3);
                outScale  .resize(chunk * 3);
                outQuat   .resize(chunk * 4);
            }

            for (int i = 0; i < chunk; i++)
            {
                quint64 splatIndex = read + i;

                quint64 chunkIndex = splatIndex / PLY_SUPER_CHUNK_SIZE;

                if (chunkIndex >= (quint64) chunks.size())
                {
                    setError(error, QString("Missing PLY chunk for splat %1").arg(splatIndex));

                    return false;
                }

                const SuperSplatChunk & c = chunks.at(chunkIndex);

                qint64 base = offset + i * recordSize;

                quint32 packedPosition = plyGetU32(vertexProps[0], data, base);
                quint32 packedRotation = plyGetU32(vertexProps[1], data, base);
                quint32 packedScale    = plyGetU32(vertexProps[2], data, base);
                quint32 packedColor    = plyGetU32(vertexProps[3], data, base);

                float x = (float) ((packedPosition >> 21) & 2047) / 2047.0f
                          * (c.maxX - c.minX) + c.minX;
                float y = (float) ((packedPosition >> 11) & 1023) / 1023.0f
                          * (c.maxY - c.minY) + c.minY;
                float z = (float) (packedPosition & 2047) / 2047.0f
                          * (c.maxZ - c.minZ) + c.minZ;

                float r0 = ((float) ((packedRotation >> 20) & 1023) / 1023.0f - 0.5f)
                           * PLY_SQRT_2;
                float r1 = ((float) ((packedRotation >> 10) & 1023) / 1023.0f - 0.5f)
                           * PLY_SQRT_2;
                float r2 = ((float) (packedRotation & 1023) / 1023.0f - 0.5f) * PLY_SQRT_2;

                // NOTE: f32::max(0.0) maps NaN to 0.
                float rr = 1.0f - r0 * r0 - r1 * r1 - r2 * r2;

                if ((rr > 0.0f) == false) rr = 0.0f;

                rr = std::sqrt(rr);

                quint32 rOrder = (packedRotation >> 30) & 3;

                float quatX = (rOrder == 0) ? r0 : ((rOrder == 1) ? rr : r1);
                float quatY = (rOrder <= 1) ? r1 : ((rOrder == 2) ? rr : r2);
                float quatZ = (rOrder <= 2) ? r2 : rr;
                float quatW = (rOrder == 0) ? rr : r0;

                float scaleX = (float) ((packedScale >> 21) & 2047) / 2047.0f
                               * (c.maxScaleX - c.minScaleX) + c.minScaleX;
                float scaleY = (float) ((packedScale >> 11) & 1023) / 1023.0f
                               * (c.maxScaleY - c.minScaleY) + c.minScaleY;
                float scaleZ = (float) (packedScale & 2047) / 2047.0f
                               * (c.maxScaleZ - c.minScaleZ) + c.minScaleZ;

                float r = (float) ((packedColor >> 24) & 255) / 255.0f
                          * (c.maxR - c.minR) + c.minR;
                float g = (float) ((packedColor >> 16) & 255) / 255.0f
                          * (c.maxG - c.minG) + c.minG;
                float b = (float) ((packedColor >> 8) & 255) / 255.0f
                          * (c.maxB - c.minB) + c.minB;

                float opacity = (float) (packedColor & 255) / 255.0f;

                int i3 = i * 3;
                int i4 = i * 4;

                outCenter[i3    ] = x;
                outCenter[i3 + 1] = y;
                outCenter[i3 + 2] = z;

                outScale[i3    ] = WSplatMath::expf(scaleX);
                outScale[i3 + 1] = WSplatMath::expf(scaleY);
                outScale[i3 + 2] = WSplatMath::expf(scaleZ);

                outRgb[i3    ] = r;
                outRgb[i3 + 1] = g;
                outRgb[i3 + 2] = b;

                outOpacity[i] = opacity;

                outQuat[i4    ] = quatX;
                outQuat[i4 + 1] = quatY;
                outQuat[i4 + 2] = quatZ;
                outQuat[i4 + 3] = quatW;
            }

            WSplatProps props;

            props.center  = outCenter .constData();
            props.opacity = outOpacity.constData();
            props.rgb     = outRgb    .constData();
            props.scale   = outScale  .constData();
            props.quat    = outQuat   .constData();

            receiver.setBatch((int) read, chunk, props);
        }
        else if (kind == PlyKindSh && maxShDegree > 0)
        {
            // SPARK: SuperSplatState::decode_sh
            if (outSh1.size() < chunk * 9)
            {
                outSh1.resize(chunk * 9);

                if (maxShDegree >= 2) outSh2.resize(chunk * 15);
                if (maxShDegree >= 3) outSh3.resize(chunk * 21);
            }

            for (int i = 0; i < chunk; i++)
            {
                qint64 base = offset + i * recordSize;

                for (int j = 0; j < numFRest; j++)
                {
                    tempRest[j] = plyGetRawF32(fRest.at(j), data, base);
                }

                int start = i * 9;

                for (int j = 0; j < 9; j++)
                {
                    outSh1[start + j] = tempRest.at(sh1Props[j]) * 8.0f / 255.0f - 4.0f;
                }

                if (maxShDegree >= 2)
                {
                    start = i * 15;

                    for (int j = 0; j < 15; j++)
                    {
                        outSh2[start + j] = tempRest.at(sh2Props[j]) * 8.0f / 255.0f - 4.0f;
                    }
                }

                if (maxShDegree >= 3)
                {
                    start = i * 21;

                    for (int j = 0; j < 21; j++)
                    {
                        outSh3[start + j] = tempRest.at(sh3Props[j]) * 8.0f / 255.0f - 4.0f;
                    }
                }
            }

            // SPARK: PackedSplatsData::set_sh
            receiver.setSh1((int) read, chunk, outSh1.constData());

            if (maxShDegree >= 2) receiver.setSh2((int) read, chunk, outSh2.constData());
            if (maxShDegree >= 3) receiver.setSh3((int) read, chunk, outSh3.constData());
        }

        read += chunk;

        reads[current] = read;

        if (read == element.count) current++;

        offset += chunk * recordSize;
    }

    // SPARK: PlyDecoder::finish
    for (int i = 0; i < count; i++)
    {
        const PlyElementDesc & element = parsed.elements.at(i);

        if (element.name != "vertex") continue;

        if (reads.at(i) != element.count)
        {
            setError(error, QString("Expected %1 splats, got %2").arg(numSplats)
                                                                  .arg(reads.at(i)));

            return false;
        }

        break;
    }

    for (int i = 0; i < count; i++)
    {
        const PlyElementDesc & element = parsed.elements.at(i);

        if (element.name != "sh") continue;

        if (reads.at(i) != element.count)
        {
            setError(error, QString("Expected %1 SH records, got %2").arg(element.count)
                                                                      .arg(reads.at(i)));

            return false;
        }

        break;
    }

    receiver.finish();

    return true;
}

}

//-------------------------------------------------------------------------------------------------
// Static functions
//-------------------------------------------------------------------------------------------------

// SPARK: PlyDecoder::poll_header
/* static */ bool WSplatPly::decode(const uchar * data, qint64 size, WSplatReceiver & receiver,
                                    QString * error)
{
    if (size < 4
        ||
        (readU32(data) & 0x00ffffff) != PLY_MAGIC)
    {
        setError(error, "Invalid PLY file");

        return false;
    }

    qsizetype headerEnd = QByteArrayView(data, size).indexOf(QByteArrayView(PLY_TERMINATOR,
                                                                            PLY_TERMINATOR_SIZE));

    if (headerEnd == -1)
    {
        if (size >= 65536)
        {
            setError(error, "PLY header too large");
        }
        else setError(error, "Invalid PLY file");

        return false;
    }

    QByteArray header((const char *) data, headerEnd);

    // NOTE: Upstream parses the header as a UTF-8 str.
    QStringDecoder decoder(QStringDecoder::Utf8, QStringDecoder::Flag::Stateless);

    QString text = decoder.decode(header);

    Q_UNUSED(text);

    if (decoder.hasError())
    {
        setError(error, "Invalid PLY header encoding");

        return false;
    }

    PlyParsedHeader parsed;

    if (parseHeader(header, &parsed, error) == false) return false;

    const uchar * body = data + headerEnd + PLY_TERMINATOR_SIZE;

    qint64 bodySize = size - headerEnd - PLY_TERMINATOR_SIZE;

    if (parsed.isSupersplat)
    {
        return decodeSuperSplat(parsed, body, bodySize, receiver, error);
    }

    const PlyElementDesc & vertex = parsed.elements.at(parsed.vertex);

    if (parsed.isPointcloud)
    {
        return decodePointCloud(vertex, body, bodySize, receiver, error);
    }
    else return decodeStandard(vertex, body, bodySize, receiver, error);
}
