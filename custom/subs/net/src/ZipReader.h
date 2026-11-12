// Small, strict zip reader for the model package. Part of CrispPlay, GPL-3.0 (see LICENSE).
// Reads the central directory only (no streaming guesswork), accepts store and deflate,
// rejects encryption, zip64, multi-disk archives, duplicate names and any name that could
// leave the target folder (.., absolute paths, drive letters, backslashes, device names).
#pragma once

#include <QByteArray>
#include <QCryptographicHash>
#include <QFile>
#include <QList>
#include <QString>

#include <atomic>

namespace crispplay::subs::detail {

struct ZipEntry {
    QString name;          // '/' separated, validated
    bool directory = false;
    quint16 method = 0;    // 0 store, 8 deflate
    quint32 crc32 = 0;
    qint64 compressedSize = 0;
    qint64 size = 0;
    qint64 localHeaderOffset = 0;
};

// True when `name` is a safe relative path ("a/b.txt", "dir/"): no empty, "." or ".."
// segment, no drive/colon, no backslash, no control character, no Windows device name,
// no segment ending in '.' or ' '.
bool isSafeZipName(const QString& name);

class ZipReader {
public:
    bool open(const QString& path, QString* error);
    const QList<ZipEntry>& entries() const { return entries_; }

    // Writes the entry's data to `out` (and `hash` when given), checking the size, the
    // CRC-32 and that the deflate stream ends exactly. `cancel` is polled between chunks.
    bool extract(const ZipEntry& entry, QIODevice& out, QCryptographicHash* hash,
                 const std::atomic<bool>* cancel, QString* error);

private:
    QFile file_;
    qint64 centralDirOffset_ = 0;
    QList<ZipEntry> entries_;
};

} // namespace crispplay::subs::detail
