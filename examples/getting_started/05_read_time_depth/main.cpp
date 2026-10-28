// 案例 05：读取一张时深表，取得配对的深度和时间。
#include <paleo_data/service.h>
#include <QCoreApplication>
#include <iostream>
#include <vector>
using namespace paleo::data;

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    const QStringList arguments = app.arguments();
    if (arguments.size() != 2 && arguments.size() != 3) {
        std::cerr << "用法：example_05_read_time_depth <工区> [时深表编号]\n";
        return 1;
    }
    DataService service;
    std::string error;
    if (!service.openProject(arguments[1].toUtf8().toStdString(), error)) {
        std::cerr << error << '\n'; return 2;
    }
    if (arguments.size() == 2) {
        std::vector<DataEntry> tables;
        if (!service.listData("time_depth", 0, tables, error)) {
            std::cerr << error << '\n'; return 3;
        }
        for (const DataEntry& table : tables) {
            std::cout << "时深表编号=" << table.id << "，井编号=" << table.wellId
                      << "，名称=" << table.name << "，点数=" << table.count;
            if (table.wellId == 0) std::cout << "（待归井，点数据已保存）";
            std::cout << '\n';
        }
        return 0;
    }
    bool idIsValid = false;
    const long long tableId = arguments[2].toLongLong(&idIsValid);
    if (!idIsValid || tableId <= 0) { std::cerr << "时深表编号不合法。\n"; return 1; }

    TimeDepthTable result;
    if (!service.readTimeDepth(tableId, result, error)) {
        std::cerr << error << '\n'; return 3;
    }
    std::cout << "关系表=" << result.relationName
              << "，深度类型=" << result.depthInfo.depthType
              << "，深度单位=" << result.depthInfo.depthUnit
              << "，时间类型=" << result.timeType << "，时间单位=" << result.timeUnit
              << "，时间参考面=" << result.timeReference
              << "，总点数=" << result.depthTimePairs.size() << '\n';

    size_t previewCount = result.depthTimePairs.size();
    if (previewCount > 10) previewCount = 10;
    for (size_t i = 0; i < previewCount; i = i + 1) {
        const TimeDepthPoint& point = result.depthTimePairs[i];
        std::cout << "深度=" << point.depthValue << "，时间=" << point.timeValue << '\n';
    }
    // result.depthTimePairs 包含全部点；前 10 点只是控制台预览。
    // 文件里的负时间保持原样；不能自动取绝对值、将 OWT 乘二或把未知单位当作 ms。
    // 如需插值求时深转换，应先确认单位/基准，再由具体业务明确选择插值方法。
    return 0;
}
