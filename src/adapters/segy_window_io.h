#pragma once
#include "paleo_data/seismic.h"
#include <QFile>
#include <QString>
#include <memory>

namespace paleo::data::segy_detail {
// A private read-only bounded-window file accessor. Each worker owns one instance.
// Views do not copy the source file or map the complete file into resident RAM.
class WindowAccess {
public:
    WindowAccess();
    ~WindowAccess();
    WindowAccess(const WindowAccess&)=delete;
    WindowAccess& operator=(const WindowAccess&)=delete;
    bool open(const QString& path, long long expectedBytes, const std::string& mode,
              int windowMiB, std::string& error);
    bool read(long long offset, void* destination, size_t length, long long requestEnd,
              SeismicReadStatistics& statistics, bool timings, std::string& error);
    const std::string& mode() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
