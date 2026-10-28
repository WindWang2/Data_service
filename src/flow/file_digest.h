#pragma once

#include "paleo_data/parser.h"
#include <QString>
#include <string>

namespace paleo::data::detail {

// Full-content SHA-256, compatible with existing source identities. Reads the
// original file without changing it; memory remains bounded to one 1 MiB block.
// Windows uses the system SHA-256 implementation when available. Other systems,
// or a Windows host without an available provider, use Qt's implementation.
// bytesRead (optional) also reports progress on cancellation/read failure.
bool fullFileSha256(const QString& path, QString& digest, std::string& error,
                    const CancelCheck& cancelled = {}, long long* bytesRead = nullptr);

}
