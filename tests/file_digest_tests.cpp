// Repository fixtures are generated synthetic examples; no private source data is bundled.
#include "flow/file_digest.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <iostream>

using namespace paleo::data;
using namespace paleo::data::detail;

namespace {
int failures = 0;
void expect(bool passed, const char* message) {
    if (!passed) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}
bool writeFile(const QString& path, const QByteArray& data) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
}
void checkDigest(const QString& path, const QByteArray& data, const char* known = nullptr) {
    expect(writeFile(path, data), "create bounded synthetic file");
    QString digest = "stale";
    std::string error = "stale";
    long long read = -1;
    const bool ok = fullFileSha256(path, digest, error, {}, &read);
    expect(ok, "full SHA succeeds");
    expect(error.empty(), "successful call clears stale error");
    expect(read == data.size(), "reports exact number of bytes read");
    expect(digest == QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex()),
           "system hash matches independent Qt hash");
    if (known) expect(digest == QString::fromLatin1(known), "matches published SHA-256 vector");
    QFile original(path);
    expect(original.open(QIODevice::ReadOnly) && original.readAll() == data, "original remains unchanged");
}
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir temp(QDir::tempPath() + QStringLiteral("/pds-digest-中文 空格-XXXXXX"));
    if (!temp.isValid()) { std::cerr << "Cannot create test directory\n"; return 1; }
    checkDigest(temp.filePath("empty.bin"), {},
        "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    checkDigest(temp.filePath(QStringLiteral("短文件.bin")), QByteArray("abc"),
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    QByteArray data(2 * 1024 * 1024 + 173, Qt::Uninitialized);
    for (qsizetype i = 0; i < data.size(); ++i) data[i] = char((i * 31 + i / 256) & 255);
    const auto multiple = temp.filePath("multiple-chunks.bin");
    checkDigest(multiple, data);

    QString digest = "stale";
    std::string error;
    long long read = -1;
    expect(!fullFileSha256(multiple, digest, error, [] { return true; }, &read), "pre-cancel fails");
    expect(digest.isEmpty() && !error.empty() && read == 0, "pre-cancel publishes no digest and reads nothing");
    int calls = 0;
    digest = "stale";
    expect(!fullFileSha256(multiple, digest, error, [&] { return ++calls >= 3; }, &read),
           "mid-stream cancellation fails");
    expect(digest.isEmpty() && !error.empty() && read == 1024 * 1024,
           "cancellation stops at first chunk and publishes no partial identity");
    digest = "stale";
    expect(!fullFileSha256(temp.filePath("does-not-exist.bin"), digest, error, {}, &read),
           "nonexistent source fails");
    expect(digest.isEmpty() && !error.empty() && read == 0, "open error never becomes empty-file digest");
    digest = "stale";
    expect(!fullFileSha256(temp.path(), digest, error, {}, &read), "directory is not a data file");
    expect(digest.isEmpty() && !error.empty(), "directory error leaves no digest");
    std::cout << "file_digest_tests failures=" << failures << '\n';
    return failures ? 1 : 0;
}
