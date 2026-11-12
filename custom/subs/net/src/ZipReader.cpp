// Part of CrispPlay, GPL-3.0 (see LICENSE). Written for CrispPlay (not a port).
#include "ZipReader.h"

#include <QSet>
#include <QStringList>

#include <zlib.h> // Qt's bundled zlib (Qt6::ZlibPrivate, exported by Qt6Core with a z_ prefix)

#include <cstring>
#include <memory>

namespace crispplay::subs::detail {
namespace {

constexpr quint32 kLocalSig = 0x04034b50;
constexpr quint32 kCentralSig = 0x02014b50;
constexpr quint32 kEndSig = 0x06054b50;
constexpr quint32 kZip64LocatorSig = 0x07064b50;
constexpr int kChunk = 256 * 1024;
constexpr int kMaxEntries = 4096;

quint16 u16(const char* p) { return quint16(quint8(p[0]) | (quint8(p[1]) << 8)); }
quint32 u32(const char* p) { return quint32(u16(p)) | (quint32(u16(p + 2)) << 16); }

bool fail(QString* error, const QString& text) {
    if (error) *error = text;
    return false;
}

bool readAt(QFile& f, qint64 offset, char* buffer, qint64 size) {
    return f.seek(offset) && f.read(buffer, size) == size;
}

} // namespace

bool isSafeZipName(const QString& name) {
    if (name.isEmpty() || name.size() > 240) return false;
    if (name.startsWith(QLatin1Char('/'))) return false;
    for (const QChar c : name) {
        if (c.unicode() < 0x20 || c == QLatin1Char('\\') || c == QLatin1Char(':') || c == QLatin1Char('*')
            || c == QLatin1Char('?') || c == QLatin1Char('"') || c == QLatin1Char('<') || c == QLatin1Char('>')
            || c == QLatin1Char('|') || c.unicode() == 0x7f)
            return false;
    }
    QString body = name;
    if (body.endsWith(QLatin1Char('/'))) body.chop(1);
    static const QSet<QString> devices = {
        QStringLiteral("CON"), QStringLiteral("PRN"), QStringLiteral("AUX"), QStringLiteral("NUL"),
        QStringLiteral("COM1"), QStringLiteral("COM2"), QStringLiteral("COM3"), QStringLiteral("COM4"),
        QStringLiteral("COM5"), QStringLiteral("COM6"), QStringLiteral("COM7"), QStringLiteral("COM8"),
        QStringLiteral("COM9"), QStringLiteral("LPT1"), QStringLiteral("LPT2"), QStringLiteral("LPT3"),
        QStringLiteral("LPT4"), QStringLiteral("LPT5"), QStringLiteral("LPT6"), QStringLiteral("LPT7"),
        QStringLiteral("LPT8"), QStringLiteral("LPT9")};
    for (const QString& part : body.split(QLatin1Char('/'))) {
        if (part.isEmpty() || part == QLatin1String(".") || part == QLatin1String("..")) return false;
        if (part.endsWith(QLatin1Char('.')) || part.endsWith(QLatin1Char(' '))) return false;
        const QString stem = part.section(QLatin1Char('.'), 0, 0).toUpper();
        if (devices.contains(stem)) return false;
    }
    return true;
}

bool ZipReader::open(const QString& path, QString* error) {
    entries_.clear();
    file_.close();
    file_.setFileName(path);
    if (!file_.open(QIODevice::ReadOnly)) return fail(error, QStringLiteral("cannot open the archive"));
    const qint64 fileSize = file_.size();
    if (fileSize < 22) return fail(error, QStringLiteral("not a zip file"));

    // End of central directory: last 22 bytes + up to 64 KiB comment.
    const qint64 tailSize = qMin<qint64>(fileSize, 22 + 0xffff);
    QByteArray tail(int(tailSize), Qt::Uninitialized);
    if (!readAt(file_, fileSize - tailSize, tail.data(), tailSize)) return fail(error, QStringLiteral("read error"));
    qint64 eocd = -1;
    for (qint64 i = tailSize - 22; i >= 0; --i) {
        const char* p = tail.constData() + i;
        if (u32(p) == kEndSig && i + 22 + u16(p + 20) == tailSize) { eocd = i; break; }
    }
    if (eocd < 0) return fail(error, QStringLiteral("no end of central directory"));
    const char* e = tail.constData() + eocd;
    const qint64 eocdOffset = fileSize - tailSize + eocd;
    if (eocd >= 20 && u32(e - 20) == kZip64LocatorSig) return fail(error, QStringLiteral("zip64 is not supported"));
    const quint16 disk = u16(e + 4), cdDisk = u16(e + 6), countHere = u16(e + 8), count = u16(e + 10);
    const quint32 cdSize = u32(e + 12), cdOffset = u32(e + 16);
    if (disk != 0 || cdDisk != 0 || countHere != count) return fail(error, QStringLiteral("multi-disk archive"));
    if (count == 0xffff || cdSize == 0xffffffffu || cdOffset == 0xffffffffu)
        return fail(error, QStringLiteral("zip64 is not supported"));
    if (count > kMaxEntries) return fail(error, QStringLiteral("too many entries"));
    if (qint64(cdOffset) + cdSize != eocdOffset) return fail(error, QStringLiteral("bad central directory bounds"));
    centralDirOffset_ = cdOffset;

    QByteArray cd(int(cdSize), Qt::Uninitialized);
    if (cdSize && !readAt(file_, cdOffset, cd.data(), cdSize)) return fail(error, QStringLiteral("read error"));
    QSet<QString> seen;
    qint64 pos = 0;
    for (int i = 0; i < count; ++i) {
        if (pos + 46 > cd.size()) return fail(error, QStringLiteral("truncated central directory"));
        const char* c = cd.constData() + pos;
        if (u32(c) != kCentralSig) return fail(error, QStringLiteral("bad central directory entry"));
        const quint16 flags = u16(c + 8), method = u16(c + 10);
        const quint32 crc = u32(c + 16), csize = u32(c + 20), usize = u32(c + 24);
        const quint16 nameLen = u16(c + 28), extraLen = u16(c + 30), commentLen = u16(c + 32), startDisk = u16(c + 34);
        const quint32 localOffset = u32(c + 42);
        if (pos + 46 + nameLen + extraLen + commentLen > cd.size())
            return fail(error, QStringLiteral("truncated central directory"));
        const QByteArray rawName(c + 46, nameLen);
        pos += 46 + nameLen + extraLen + commentLen;

        if (flags & 0x0001) return fail(error, QStringLiteral("encrypted entry"));
        if (flags & 0x0040) return fail(error, QStringLiteral("strong encryption"));
        if (method != 0 && method != 8) return fail(error, QStringLiteral("unsupported compression method %1").arg(method));
        if (csize == 0xffffffffu || usize == 0xffffffffu || localOffset == 0xffffffffu)
            return fail(error, QStringLiteral("zip64 is not supported"));
        if (startDisk != 0) return fail(error, QStringLiteral("multi-disk archive"));
        // Names: UTF-8 when flagged, otherwise only plain ASCII (no CP437 guessing).
        QString name;
        if (flags & 0x0800) {
            name = QString::fromUtf8(rawName);
            if (name.toUtf8() != rawName) return fail(error, QStringLiteral("invalid UTF-8 entry name"));
        } else {
            for (const char ch : rawName)
                if (quint8(ch) >= 0x80) return fail(error, QStringLiteral("non-ASCII entry name"));
            name = QString::fromLatin1(rawName);
        }
        if (!isSafeZipName(name)) return fail(error, QStringLiteral("unsafe entry name: %1").arg(name));
        const QString key = name.toLower();
        if (seen.contains(key)) return fail(error, QStringLiteral("duplicate entry: %1").arg(name));
        seen.insert(key);
        if (method == 0 && csize != usize) return fail(error, QStringLiteral("bad stored entry size: %1").arg(name));
        if (qint64(localOffset) + 30 + csize > centralDirOffset_)
            return fail(error, QStringLiteral("entry outside the archive: %1").arg(name));

        ZipEntry entry;
        entry.name = name;
        entry.directory = name.endsWith(QLatin1Char('/'));
        entry.method = method;
        entry.crc32 = crc;
        entry.compressedSize = csize;
        entry.size = usize;
        entry.localHeaderOffset = localOffset;
        if (entry.directory && usize != 0) return fail(error, QStringLiteral("directory with data: %1").arg(name));
        entries_.append(entry);
    }
    if (pos != cd.size()) return fail(error, QStringLiteral("garbage in central directory"));
    return true;
}

bool ZipReader::extract(const ZipEntry& entry, QIODevice& out, QCryptographicHash* hash,
                        const std::atomic<bool>* cancel, QString* error) {
    char local[30];
    if (!readAt(file_, entry.localHeaderOffset, local, 30) || u32(local) != kLocalSig)
        return fail(error, QStringLiteral("bad local header: %1").arg(entry.name));
    if (u16(local + 8) != entry.method) return fail(error, QStringLiteral("header mismatch: %1").arg(entry.name));
    const qint64 dataOffset = entry.localHeaderOffset + 30 + u16(local + 26) + u16(local + 28);
    if (dataOffset + entry.compressedSize > centralDirOffset_)
        return fail(error, QStringLiteral("entry outside the archive: %1").arg(entry.name));
    if (!file_.seek(dataOffset)) return fail(error, QStringLiteral("read error"));

    QByteArray in(kChunk, Qt::Uninitialized), outBuf(kChunk, Qt::Uninitialized);
    qint64 remaining = entry.compressedSize, written = 0;
    uLong crc = z_crc32(0L, Z_NULL, 0);
    auto sink = [&](const char* data, qint64 n) -> bool {
        written += n;
        if (written > entry.size) return false;
        crc = z_crc32(crc, reinterpret_cast<const Bytef*>(data), uInt(n));
        if (hash) hash->addData(QByteArrayView(data, n));
        return out.write(data, n) == n;
    };

    if (entry.method == 0) {
        while (remaining > 0) {
            if (cancel && cancel->load()) return fail(error, QStringLiteral("cancelled"));
            const qint64 n = file_.read(in.data(), qMin<qint64>(remaining, kChunk));
            if (n <= 0) return fail(error, QStringLiteral("read error"));
            remaining -= n;
            if (!sink(in.constData(), n)) return fail(error, QStringLiteral("write error or size overflow"));
        }
    } else {
        z_stream z;
        std::memset(&z, 0, sizeof z);
        if (inflateInit2(&z, -MAX_WBITS) != Z_OK) return fail(error, QStringLiteral("zlib init failed"));
        std::unique_ptr<z_stream, int (*)(z_stream*)> guard(&z, inflateEnd);
        int rc = Z_OK;
        while (rc != Z_STREAM_END) {
            if (cancel && cancel->load()) return fail(error, QStringLiteral("cancelled"));
            if (z.avail_in == 0) {
                if (remaining == 0) return fail(error, QStringLiteral("truncated deflate stream: %1").arg(entry.name));
                const qint64 n = file_.read(in.data(), qMin<qint64>(remaining, kChunk));
                if (n <= 0) return fail(error, QStringLiteral("read error"));
                remaining -= n;
                z.next_in = reinterpret_cast<Bytef*>(in.data());
                z.avail_in = uInt(n);
            }
            z.next_out = reinterpret_cast<Bytef*>(outBuf.data());
            z.avail_out = uInt(outBuf.size());
            rc = inflate(&z, Z_NO_FLUSH);
            if (rc != Z_OK && rc != Z_STREAM_END) return fail(error, QStringLiteral("corrupt deflate data: %1").arg(entry.name));
            const qint64 produced = outBuf.size() - z.avail_out;
            if (produced && !sink(outBuf.constData(), produced))
                return fail(error, QStringLiteral("write error or size overflow: %1").arg(entry.name));
        }
        // The stream must use exactly the bytes the directory promised.
        if (z.avail_in != 0 || remaining != 0) return fail(error, QStringLiteral("trailing data: %1").arg(entry.name));
    }
    if (written != entry.size) return fail(error, QStringLiteral("size mismatch: %1").arg(entry.name));
    if (quint32(crc) != entry.crc32) return fail(error, QStringLiteral("CRC mismatch: %1").arg(entry.name));
    return true;
}

} // namespace crispplay::subs::detail
