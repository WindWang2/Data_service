// 案例 02：从井名和曲线名出发，取得真正的深度与数值数组。
#include <paleo_data/service.h>
#include <QCoreApplication>
#include <iostream>
#include <vector>
using namespace paleo::data;

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    const QStringList arguments = app.arguments();
    if (arguments.size() != 4 && arguments.size() != 5) {
        std::cerr << "用法：example_02_curve_by_name <工区> <井名> <曲线名> [曲线组编号]\n";
        return 1;
    }
    const std::string wellName = arguments[2].toUtf8().toStdString();
    const std::string curveName = arguments[3].toUtf8().toStdString();
    long long requiredGroupId = 0; // 0 表示暂不按曲线组筛选。
    if (arguments.size() == 5) {
        bool numberIsValid = false;
        requiredGroupId = arguments[4].toLongLong(&numberIsValid);
        if (!numberIsValid || requiredGroupId <= 0) {
            std::cerr << "曲线组编号必须为正整数。\n";
            return 1;
        }
    }

    DataService service;
    std::string error;
    if (!service.openProject(arguments[1].toUtf8().toStdString(), error)) {
        std::cerr << error << '\n'; return 2;
    }
    std::vector<WellSummary> wells;
    if (!service.listWells(wells, error)) {
        std::cerr << error << '\n'; return 2;
    }

    // 名称严格匹配：不删除 D 等井名后缀，也不自行合并相似井号。
    long long wellId = 0;
    int matchingWells = 0;
    for (const WellSummary& well : wells) {
        if (well.name == wellName) {
            wellId = well.id;
            matchingWells = matchingWells + 1;
        }
    }
    if (matchingWells != 1) {
        std::cerr << "需要唯一井名，匹配数量：" << matchingWells << '\n'; return 3;
    }

    // 第一步拿“曲线索引”：用于找到编号，此时还没有读取曲线值。
    std::vector<DataEntry> curves;
    if (!service.listData("curves", wellId, curves, error)) {
        std::cerr << error << '\n'; return 4;
    }
    long long curveId = 0;
    int matchingCurves = 0;
    for (const DataEntry& curve : curves) {
        if (curve.name == curveName) {
            std::cout << "同名候选：曲线编号=" << curve.id
                      << "，曲线组编号=" << curve.parentId << '\n';
            if (requiredGroupId == 0 || curve.parentId == requiredGroupId) {
                curveId = curve.id;
                matchingCurves = matchingCurves + 1;
            }
        }
    }
    if (matchingCurves != 1) {
        std::cerr << "匹配曲线数量=" << matchingCurves
                  << "；请核对名称，或补充曲线组编号。组内仍重复时应改用明确的曲线编号。\n";
        return 5; // 不随便取第一条，避免混用不同解释/处理版本。
    }

    // 第二步读取真正的数据。Missing 表示该端深度不限；true 表示取处理值。
    CurveSlice result;
    if (!service.readCurveSlice(curveId, Missing, Missing, true, result, error)) {
        std::cerr << error << '\n'; return 6;
    }
    std::cout << "已返回全部 " << result.depths.size() << " 个样点，曲线单位="
              << result.unit << "，深度单位=" << result.depthUnit << '\n';

    // 只打印前 10 点，避免刷屏；result 中仍然保存着完整返回数组。
    size_t previewCount = result.depths.size();
    if (previewCount > 10) previewCount = 10;
    for (size_t i = 0; i < previewCount; i = i + 1) {
        std::cout << "深度=" << result.depths[i] << "，原始值=" << result.rawValues[i]
                  << "，本次值=" << result.values[i]
                  << "，质量=" << int(result.quality[i]) << '\n';
    }
    // 绘图：横坐标使用 result.values[i]，纵坐标使用 result.depths[i]。
    // 数组同一下标对应同一采样点；原始空值位置不要连接成有效曲线。
    return 0;
}
