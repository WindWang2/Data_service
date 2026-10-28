// 案例 04：先查看分层道编号，再读取层段对象。
#include <paleo_data/service.h>
#include <QCoreApplication>
#include <cmath>
#include <iostream>
#include <vector>
using namespace paleo::data;

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    const QStringList arguments = app.arguments();
    if (arguments.size() != 2 && arguments.size() != 3 && arguments.size() != 5) {
        std::cerr << "用法：example_04_read_intervals <工区> [分层道编号 [顶深 底深]]\n";
        return 1;
    }
    DataService service;
    std::string error;
    if (!service.openProject(arguments[1].toUtf8().toStdString(), error)) {
        std::cerr << error << '\n'; return 2;
    }
    if (arguments.size() == 2) {
        // wellId=0 表示列出全部井的分层道，便于用户明确选择解释方案。
        std::vector<DataEntry> tracks;
        if (!service.listData("intervals", 0, tracks, error)) {
            std::cerr << error << '\n'; return 3;
        }
        for (const DataEntry& track : tracks) {
            std::cout << "分层道编号=" << track.id << "，井编号=" << track.wellId
                      << "，名称=" << track.name << "，层段数量=" << track.count << '\n';
        }
        return 0; // 本次只列目录；补充编号再次运行，才会读取层段数组。
    }

    bool idIsValid = false;
    const long long trackId = arguments[2].toLongLong(&idIsValid);
    double topDepth = Missing, bottomDepth = Missing;
    if (arguments.size() == 5) {
        bool topIsValid = false, bottomIsValid = false;
        topDepth = arguments[3].toDouble(&topIsValid);
        bottomDepth = arguments[4].toDouble(&bottomIsValid);
        if (!topIsValid || !bottomIsValid || !std::isfinite(topDepth)
            || !std::isfinite(bottomDepth) || topDepth > bottomDepth) {
            std::cerr << "深度范围不合法。\n"; return 1;
        }
    }
    if (!idIsValid || trackId <= 0) { std::cerr << "分层道编号不合法。\n"; return 1; }

    // 元信息说明本道的深度类型、单位和基准；不能把 MD 当成 TVD 使用。
    IntervalTrack trackInfo;
    if (!service.readIntervalTrackInfo(trackId, trackInfo, error)) {
        std::cerr << error << '\n'; return 3;
    }
    std::vector<IntervalRecord> result;
    if (!service.readIntervals(trackId, topDepth, bottomDepth, result, error)) {
        std::cerr << error << '\n'; return 4;
    }
    std::cout << "分层道=" << trackInfo.trackName << "，类型=" << trackInfo.trackType
              << "，深度=" << trackInfo.depthInfo.depthType << ' '
              << trackInfo.depthInfo.depthUnit << "，返回层段=" << result.size() << '\n';
    for (const IntervalRecord& record : result) {
        const GeologicalInterval& interval = record.value;
        std::cout << "编号=" << record.id << "，名称=" << interval.intervalName
                  << "，顶深=" << interval.topDepth << "，记录底深=" << interval.bottomDepth
                  << "，相类型=" << interval.faciesType
                  << "，底部未钻穿=" << interval.bottomNotPenetrated << '\n';
    }
    // 筛选返回与指定范围相交的原层段，不把层段边界裁切成查询范围。
    // bottomNotPenetrated=1 时，记录底深不能当成已确认的地层真实底界。
    return 0;
}
