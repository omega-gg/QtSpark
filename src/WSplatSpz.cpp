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

#include "WSplatSpz.h"

// Qt includes
#include <QVector>
#include <QtEndian>

// Splat includes
#include <WSplatMath.h>
#include <WPackedSplats.h>

// C++ includes
#include <climits>
#include <cmath>
#include <cstring>

// 3rdparty includes
#include <zlib.h>

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

static const quint32 SPZ_MAGIC = 0x5053474e; // "NGSP"

static const float SPZ_SH_C0 = 0.28209479177387814f;

static const int SPZ_MAX_SPLAT_CHUNK = 65536;

// NOTE: std::f32::consts::FRAC_1_SQRT_2.
static const float SPZ_FRAC_1_SQRT_2 = 0.707106781186547524400844362104849039f;

static const int SPZ_BUFFER_SIZE = 128 * 1024;

//-------------------------------------------------------------------------------------------------
// Private
//-------------------------------------------------------------------------------------------------

namespace
{

void setError(QString * error, const QString & message)
{
    if (error) *error = message;
}

// SPARK: parse_gzip_header
// NOTE: Returns the offset of the deflate stream, 0 when the header is incomplete and -1 when it
//       is invalid.
qint64 parseGzipHeader(const uchar * buffer, qint64 size)
{
    if (size < 10) return 0;

    if (buffer[0] != 0x1f || buffer[1] != 0x8b || buffer[2] != 8) return -1;

    uchar flags = buffer[3];

    qint64 end = 10;

    // NOTE: FEXTRA.
    if (flags & 0x04)
    {
        if (size < end + 2) return 0;

        qint64 extraLength = buffer[end] | (buffer[end + 1] << 8);

        end += 2;

        if (size < end + extraLength) return 0;

        end += extraLength;
    }

    // NOTE: FNAME and FCOMMENT, both zero terminated.
    for (int flag = 0x08; flag <= 0x10; flag <<= 1)
    {
        if ((flags & flag) == 0) continue;

        qint64 index = end;

        bool found = false;

        while (index < size)
        {
            if (buffer[index++] == 0)
            {
                found = true;

                break;
            }
        }

        if (found == false) return 0;

        end = index;
    }

    // NOTE: FHCRC.
    if (flags & 0x02)
    {
        if (size < end + 2) return 0;

        end += 2;
    }

    return end;
}

//-------------------------------------------------------------------------------------------------
// SpzStream
//-------------------------------------------------------------------------------------------------

// NOTE: Streams the raw deflate payload, like miniz_oxide's decompress in upstream. The gzip
//       trailer is ignored, so neither the CRC nor the size is checked.
class SpzStream
{
public:
    enum State
    {
        Ok,
        Done,
        Truncated,
        Failed
    };

public:
    SpzStream(const uchar * data, qint64 size);

    ~SpzStream();

public: // Interface
    // NOTE: Reads exactly 'size' bytes, returns false when the stream ends before that.
    bool read(uchar * out, qint64 size);

    // NOTE: Decompresses up to the end of the stream, returns true when it ends properly.
    bool drain();

    QString errorString() const;

private: // Functions
    bool fill();

public: // Variables
    State state;

private:
    z_stream stream;

    bool initialized;

    const uchar * input;
    qint64        inputSize;
    qint64        inputPos;

    QVector<uchar> buffer;

    qint64 bufferPos;
    qint64 bufferEnd;
};

SpzStream::SpzStream(const uchar * data, qint64 size)
{
    state = Ok;

    input     = data;
    inputSize = size;
    inputPos  = 0;

    bufferPos = 0;
    bufferEnd = 0;

    memset(&stream, 0, sizeof(stream));

    initialized = (inflateInit2(&stream, -MAX_WBITS) == Z_OK);

    if (initialized == false) state = Failed;
}

SpzStream::~SpzStream()
{
    if (initialized) inflateEnd(&stream);
}

bool SpzStream::read(uchar * out, qint64 size)
{
    while (size > 0)
    {
        if (bufferPos == bufferEnd)
        {
            if (fill() == false) return false;

            continue;
        }

        qint64 count = qMin(size, bufferEnd - bufferPos);

        memcpy(out, buffer.constData() + bufferPos, count);

        out  += count;
        size -= count;

        bufferPos += count;
    }

    return true;
}

bool SpzStream::drain()
{
    bufferPos = bufferEnd;

    while (fill())
    {
        bufferPos = bufferEnd;
    }

    return (state == Done);
}

QString SpzStream::errorString() const
{
    if (state == Truncated) return "Truncated gzip stream";
    if (state == Failed)    return "Decompression failed";

    return QString();
}

bool SpzStream::fill()
{
    if (state != Ok) return false;

    if (buffer.isEmpty()) buffer.resize(SPZ_BUFFER_SIZE);

    if (stream.avail_in == 0 && inputPos < inputSize)
    {
        qint64 count = qMin(inputSize - inputPos, (qint64) (1 << 30));

        stream.next_in  = (Bytef *) (input + inputPos);
        stream.avail_in = (uInt) count;

        inputPos += count;
    }

    stream.next_out  = buffer.data();
    stream.avail_out = SPZ_BUFFER_SIZE;

    int result = inflate(&stream, Z_NO_FLUSH);

    bufferPos = 0;
    bufferEnd = SPZ_BUFFER_SIZE - stream.avail_out;

    if (result == Z_STREAM_END)
    {
        state = Done;
    }
    else if (result == Z_BUF_ERROR)
    {
        // NOTE: No progress, the input is exhausted before the end of the stream.
        if (stream.avail_in == 0 && inputPos == inputSize) state = Truncated;
    }
    else if (result != Z_OK)
    {
        state = Failed;
    }

    return (bufferEnd > 0 || state == Ok);
}

//-------------------------------------------------------------------------------------------------

// SPARK: read_i24_le
qint32 readI24(const uchar * three)
{
    quint32 value = ((quint32) three[2] << 16) | ((quint32) three[1] << 8) | three[0];

    if (value & 0x00800000) return (qint32) (value | 0xff000000);

    return (qint32) value;
}

// NOTE: Reads the next 'count' items of a section into 'bytes'.
bool readItems(SpzStream & stream, QVector<uchar> & bytes, int count, int bytesPerItem,
               QString * error)
{
    qint64 size = (qint64) count * bytesPerItem;

    if (bytes.size() < size) bytes.resize(size);

    if (stream.read(bytes.data(), size)) return true;

    if (stream.state == SpzStream::Done)
    {
        setError(error, "Incomplete SPZ stream");
    }
    else setError(error, stream.errorString());

    return false;
}

}

//-------------------------------------------------------------------------------------------------
// Static functions
//-------------------------------------------------------------------------------------------------

// SPARK: SpzDecoder::poll_header, SpzDecoder::poll_sections
/* static */ bool WSplatSpz::decode(const uchar * data, qint64 size, WSplatReceiver & receiver,
                                    QString * error)
{
    qint64 offset = parseGzipHeader(data, size);

    if (offset == -1)
    {
        setError(error, "Invalid gzip header");

        return false;
    }

    if (offset == 0)
    {
        setError(error, "Truncated gzip stream");

        return false;
    }

    SpzStream stream(data + offset, size - offset);

    uchar header[16];

    if (stream.read(header, 16) == false)
    {
        if (stream.state == SpzStream::Done)
        {
            setError(error, "Invalid SPZ stream");
        }
        else setError(error, stream.errorString());

        return false;
    }

    quint32 magic = qFromLittleEndian<quint32>(header);

    if (magic != SPZ_MAGIC)
    {
        setError(error, QString("Invalid SPZ magic: 0x%1").arg(magic, 8, 16, QChar('0')));

        return false;
    }

    quint32 version = qFromLittleEndian<quint32>(header + 4);

    if (version < 1 || version > 3)
    {
        setError(error, QString("Unsupported SPZ version: %1").arg(version));

        return false;
    }

    quint32 count = qFromLittleEndian<quint32>(header + 8);

    int shDegree = header[12];

    int fractionalBits = header[13];

    int flags = header[14];

    // SPARK: SpzDecoderState::new
    if (shDegree > 3)
    {
        setError(error, QString("Invalid SH degree: %1").arg(shDegree));

        return false;
    }

    if (count > INT_MAX)
    {
        setError(error, QString("Too many splats: %1").arg(count));

        return false;
    }

    int numSplats = (int) count;

    bool lodTree = (flags & 0x80);

    WSplatInit init;

    init.numSplats   = numSplats;
    init.maxShDegree = shDegree;
    init.lodTree     = lodTree;

    if (receiver.initSplats(init) == false)
    {
        setError(error, "Failed to initialize splats");

        return false;
    }

    // NOTE: Upstream also calls set_encoding with lod_opacity alone when lodTree is set. Our
    //       WSplatEncoding has no optional fields, sending it would reset the receiver's other
    //       fields. init_splats already sets lodOpacity from lodTree.

    int chunkSize = qMin(numSplats, SPZ_MAX_SPLAT_CHUNK);

    QVector<uchar> bytes;

    QVector<float> output(chunkSize * 4);

    float * out = output.data();

    int next;
    int chunk;

    // SPARK: SpzDecoderStage::Centers
    int bytesPerItem = (version == 1) ? 6 : 9;

    // NOTE: Rust shifts wrap their amount in release builds.
    float frac = (float) (1u << (fractionalBits & 31));

    for (next = 0; next < numSplats; next += chunk)
    {
        chunk = qMin(numSplats - next, SPZ_MAX_SPLAT_CHUNK);

        if (readItems(stream, bytes, chunk, bytesPerItem, error) == false) return false;

        const uchar * buffer = bytes.constData();

        if (version == 1)
        {
            for (int i = 0; i < chunk * 3; i++)
            {
                quint16 bits = qFromLittleEndian<quint16>(buffer + i * 2);

                out[i] = WPackedSplats::halfToFloat(bits);
            }
        }
        else
        {
            for (int i = 0; i < chunk * 3; i++)
            {
                out[i] = (float) readI24(buffer + i * 3) / frac;
            }
        }

        receiver.setCenter(next, chunk, out);
    }

    // SPARK: SpzDecoderStage::Alphas
    float opacityScale = lodTree ? 2.0f : 1.0f;

    for (next = 0; next < numSplats; next += chunk)
    {
        chunk = qMin(numSplats - next, SPZ_MAX_SPLAT_CHUNK);

        if (readItems(stream, bytes, chunk, 1, error) == false) return false;

        const uchar * buffer = bytes.constData();

        for (int i = 0; i < chunk; i++)
        {
            out[i] = (float) buffer[i] / 255.0f * opacityScale;
        }

        receiver.setOpacity(next, chunk, out);
    }

    // SPARK: SpzDecoderStage::Rgb
    const float scale = SPZ_SH_C0 / 0.15f;

    for (next = 0; next < numSplats; next += chunk)
    {
        chunk = qMin(numSplats - next, SPZ_MAX_SPLAT_CHUNK);

        if (readItems(stream, bytes, chunk, 3, error) == false) return false;

        const uchar * buffer = bytes.constData();

        for (int i = 0; i < chunk * 3; i++)
        {
            out[i] = ((float) buffer[i] / 255.0f - 0.5f) * scale + 0.5f;
        }

        receiver.setRgb(next, chunk, out);
    }

    // SPARK: SpzDecoderStage::Scales
    for (next = 0; next < numSplats; next += chunk)
    {
        chunk = qMin(numSplats - next, SPZ_MAX_SPLAT_CHUNK);

        if (readItems(stream, bytes, chunk, 3, error) == false) return false;

        const uchar * buffer = bytes.constData();

        for (int i = 0; i < chunk * 3; i++)
        {
            out[i] = WSplatMath::expf((float) buffer[i] / 16.0f - 10.0f);
        }

        receiver.setScale(next, chunk, out);
    }

    // SPARK: SpzDecoderStage::Quats
    bytesPerItem = (version == 3) ? 4 : 3;

    for (next = 0; next < numSplats; next += chunk)
    {
        chunk = qMin(numSplats - next, SPZ_MAX_SPLAT_CHUNK);

        if (readItems(stream, bytes, chunk, bytesPerItem, error) == false) return false;

        const uchar * buffer = bytes.constData();

        if (version == 3)
        {
            // NOTE: Version 3 uses the "smallest three" encoding, 4 bytes per splat.
            const quint32 valueMask = (1u << 9) - 1;

            for (int i = 0; i < chunk; i++)
            {
                quint32 comp = qFromLittleEndian<quint32>(buffer + i * 4);

                int largestIndex = comp >> 30;

                quint32 remainingValues = comp;

                float q[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

                float sumSquares = 0.0f;

                for (int j = 3; j >= 0; j--)
                {
                    if (j == largestIndex) continue;

                    float value = (float) (remainingValues & valueMask);

                    bool sign = ((remainingValues >> 9) & 0x1);

                    remainingValues >>= 10;

                    float v = SPZ_FRAC_1_SQRT_2 * (value / (float) valueMask);

                    if (sign) v = -v;

                    q[j] = v;

                    sumSquares += v * v;
                }

                float sq = 1.0f - sumSquares;

                if (sq > 0.0f)
                {
                    q[largestIndex] = std::sqrt(sq);
                }
                else q[largestIndex] = 0.0f;

                float * o = out + i * 4;

                o[0] = q[0];
                o[1] = q[1];
                o[2] = q[2];
                o[3] = q[3];
            }
        }
        else
        {
            // NOTE: Versions 1 and 2 store x y z on 3 bytes, w is rebuilt.
            for (int i = 0; i < chunk; i++)
            {
                const uchar * at = buffer + i * 3;

                float qx = (float) at[0] / 127.5f - 1.0f;
                float qy = (float) at[1] / 127.5f - 1.0f;
                float qz = (float) at[2] / 127.5f - 1.0f;

                // NOTE: f32::max(0.0) maps NaN to 0.
                float qw = 1.0f - (qx * qx + qy * qy + qz * qz);

                if ((qw > 0.0f) == false) qw = 0.0f;

                float * o = out + i * 4;

                o[0] = qx;
                o[1] = qy;
                o[2] = qz;
                o[3] = std::sqrt(qw);
            }
        }

        receiver.setQuat(next, chunk, out);
    }

    // SPARK: SpzDecoderStage::Sh
    if (shDegree > 0)
    {
        // NOTE: 3 * (3, 8 or 15) coefficients.
        int shComponents = (shDegree == 1) ? 9 : ((shDegree == 2) ? 24 : 45);

        QVector<float> outSh(chunkSize * shComponents);

        float * sh = outSh.data();

        for (next = 0; next < numSplats; next += chunk)
        {
            chunk = qMin(numSplats - next, SPZ_MAX_SPLAT_CHUNK);

            if (readItems(stream, bytes, chunk, shComponents, error) == false) return false;

            const uchar * buffer = bytes.constData();

            // NOTE: Each splat stores its coefficients rgb interleaved, the output has the sh1
            //       block of the whole chunk first, then sh2, then sh3.
            float * sh1 = sh;
            float * sh2 = sh + 9  * chunk;
            float * sh3 = sh + 24 * chunk;

            for (int i = 0; i < chunk; i++)
            {
                const uchar * at = buffer + i * shComponents;

                for (int j = 0; j < 9; j++)
                {
                    sh1[9 * i + j] = ((float) at[j] - 128.0f) / 128.0f;
                }

                if (shDegree >= 2)
                {
                    for (int j = 0; j < 15; j++)
                    {
                        sh2[15 * i + j] = ((float) at[9 + j] - 128.0f) / 128.0f;
                    }
                }

                if (shDegree >= 3)
                {
                    for (int j = 0; j < 21; j++)
                    {
                        sh3[21 * i + j] = ((float) at[24 + j] - 128.0f) / 128.0f;
                    }
                }
            }

            // SPARK: PackedSplatsData::set_sh
            receiver.setSh1(next, chunk, sh1);

            if (shDegree >= 2) receiver.setSh2(next, chunk, sh2);
            if (shDegree >= 3) receiver.setSh3(next, chunk, sh3);
        }
    }

    // SPARK: SpzDecoderStage::ChildCounts, SpzDecoderStage::ChildStarts
    // NOTE: The LoD tree is not used, we read past it so a short stream still fails like
    //       upstream.
    if (lodTree)
    {
        for (next = 0; next < numSplats; next += chunk)
        {
            chunk = qMin(numSplats - next, SPZ_MAX_SPLAT_CHUNK);

            if (readItems(stream, bytes, chunk, 2, error) == false) return false;
        }

        for (next = 0; next < numSplats; next += chunk)
        {
            chunk = qMin(numSplats - next, SPZ_MAX_SPLAT_CHUNK);

            if (readItems(stream, bytes, chunk, 4, error) == false) return false;
        }
    }

    // NOTE: Upstream ignores trailing bytes, but requires the deflate stream to end.
    if (stream.drain() == false)
    {
        setError(error, stream.errorString());

        return false;
    }

    receiver.finish();

    return true;
}

// SPARK: try_gunzip (decoder.rs)
/* static */ bool WSplatSpz::isSpz(const uchar * data, qint64 size)
{
    qint64 offset = parseGzipHeader(data, size);

    if (offset <= 0 || size <= offset) return false;

    SpzStream stream(data + offset, size - offset);

    uchar header[4];

    if (stream.read(header, 4) == false) return false;

    return (qFromLittleEndian<quint32>(header) == SPZ_MAGIC);
}
