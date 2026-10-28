#include "file_digest.h"

#include <QByteArray>
#include <QCryptographicHash>
#include <QFile>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>
#endif

namespace paleo::data::detail {
namespace {

#ifdef _WIN32
// Each operation owns its handles. There is no shared mutable hash state across
// DataService instances or worker threads. Cleanup also runs on early returns.
class NativeSha256 {
public:
    NativeSha256() {
        if (BCryptOpenAlgorithmProvider(&algorithm_, BCRYPT_SHA256_ALGORITHM,
                                        nullptr, 0) < 0) return;
        DWORD objectBytes = 0;
        DWORD returnedBytes = 0;
        if (BCryptGetProperty(algorithm_, BCRYPT_OBJECT_LENGTH,
                              reinterpret_cast<PUCHAR>(&objectBytes), sizeof(objectBytes),
                              &returnedBytes, 0) < 0 || returnedBytes != sizeof(objectBytes)) return;
        // A provider returning an unreasonable state size is treated as
        // unavailable; it must not defeat the reader's bounded memory use.
        if (objectBytes == 0 || objectBytes > 1024 * 1024) return;
        state_.resize(objectBytes);
        if (BCryptCreateHash(algorithm_, &hash_, state_.data(), objectBytes,
                             nullptr, 0, 0) < 0) return;
        ready_ = true;
    }

    ~NativeSha256() {
        if (hash_) BCryptDestroyHash(hash_);
        if (algorithm_) BCryptCloseAlgorithmProvider(algorithm_, 0);
    }
    NativeSha256(const NativeSha256&) = delete;
    NativeSha256& operator=(const NativeSha256&) = delete;

    bool ready() const { return ready_; }
    bool append(const char* bytes, ULONG count, std::string& error) {
        const NTSTATUS status = BCryptHashData(hash_,
            reinterpret_cast<PUCHAR>(const_cast<char*>(bytes)), count, 0);
        return checked(status, error);
    }
    bool finish(QByteArray& digest, std::string& error) {
        digest.resize(32);
        const NTSTATUS status = BCryptFinishHash(hash_,
            reinterpret_cast<PUCHAR>(digest.data()), ULONG(digest.size()), 0);
        return checked(status, error);
    }

private:
    static bool checked(NTSTATUS status, std::string& error) {
        if (status >= 0) return true;
        error = QStringLiteral("SHA-256 系统计算失败，错误码 0x%1")
            .arg(quint32(status), 8, 16, QLatin1Char('0')).toUtf8().toStdString();
        return false;
    }
    BCRYPT_ALG_HANDLE algorithm_ = nullptr;
    BCRYPT_HASH_HANDLE hash_ = nullptr;
    std::vector<unsigned char> state_;
    bool ready_ = false;
};
#endif

}

bool fullFileSha256(const QString& path, QString& digest, std::string& error,
                    const CancelCheck& cancelled, long long* bytesRead) {
    digest.clear();
    error.clear();
    if (bytesRead) *bytesRead = 0;
    const auto stopped = [&]() {
        if (!cancelled || !cancelled()) return false;
        error = "已取消";
        return true;
    };
    if (stopped()) return false;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        error = file.errorString().toUtf8().toStdString();
        return false;
    }
    QCryptographicHash portable(QCryptographicHash::Sha256);
#ifdef _WIN32
    NativeSha256 native;
#endif
    QByteArray buffer(1024 * 1024, Qt::Uninitialized);
    for (;;) {
        if (stopped()) return false;
        const qint64 count = file.read(buffer.data(), buffer.size());
        // Never reinterpret a read error as EOF or hide it by retrying another
        // hash backend: an incomplete digest must not become source identity.
        if (count < 0 || file.error() != QFileDevice::NoError) {
            error = file.errorString().toUtf8().toStdString();
            return false;
        }
        if (count == 0) break;
        if (bytesRead) *bytesRead += count;
#ifdef _WIN32
        if (native.ready()) {
            if (!native.append(buffer.constData(), ULONG(count), error)) return false;
        } else
#endif
        {
            portable.addData(QByteArrayView(buffer.constData(), count));
        }
    }
    if (stopped()) return false;
    QByteArray result;
#ifdef _WIN32
    if (native.ready()) {
        if (!native.finish(result, error)) return false;
    } else
#endif
    {
        result = portable.result();
    }
    digest = QString::fromLatin1(result.toHex());
    return true;
}

}
