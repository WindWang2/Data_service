// 案例 03：取一段曲线，使用完整返回数组计算均值。
#include <paleo_data/service.h>
#include <QCoreApplication>
#include <cmath>
#include <iostream>
using namespace paleo::data;

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    const QStringList arguments = app.arguments();
    if (arguments.size() != 5) {
        std::cerr << "用法：example_03_curve_statistics <工区> <曲线编号> <顶深> <底深>\n";
        return 1;
    }
    bool idIsValid = false, topIsValid = false, bottomIsValid = false;
    const long long curveId = arguments[2].toLongLong(&idIsValid);
    const double topDepth = arguments[3].toDouble(&topIsValid);
    const double bottomDepth = arguments[4].toDouble(&bottomIsValid);
    if (!idIsValid || curveId <= 0 || !topIsValid || !bottomIsValid
        || !std::isfinite(topDepth) || !std::isfinite(bottomDepth) || topDepth > bottomDepth) {
        std::cerr << "编号或深度范围不合法。\n"; return 1;
    }

    DataService service;
    std::string error;
    if (!service.openProject(arguments[1].toUtf8().toStdString(), error)) {
        std::cerr << error << '\n'; return 2;
    }
    CurveSlice result;
    if (!service.readCurveSlice(curveId, topDepth, bottomDepth, true, result, error)) {
        std::cerr << error << '\n'; return 3;
    }

    // 范围采用资料原生的深度数值；单位为空时不能自称“米”。
    std::cout << "曲线=" << result.name << "，深度单位=" << result.depthUnit
              << "，曲线单位=" << result.unit << "，范围内样点=" << result.values.size() << '\n';
    long long acceptedCount = 0, skippedCount = 0;
    long double sum = 0; // 用更宽的中间数值类型累加，再计算平均值。
    for (size_t i = 0; i < result.values.size(); i = i + 1) {
        const double value = result.values[i];
        const int quality = int(result.quality[i]);
        // 本案例的业务规则：排除空值(1)、阈值异常(2)以及非有限数值。
        // 接受正常值(0)与人工修改值(4)。其他业务可以明确采用不同规则。
        if (quality == 1 || quality == 2 || !std::isfinite(value)) {
            skippedCount = skippedCount + 1;
            continue;
        }
        // 不用“value < 0”判断无效：例如 SP 的负值可能完全合法。
        sum = sum + value;
        acceptedCount = acceptedCount + 1;
    }
    std::cout << "参与统计=" << acceptedCount << "，跳过=" << skippedCount << '\n';
    if (acceptedCount == 0) {
        std::cout << "没有符合本案例质量规则的数值，不能计算均值。\n";
        return 0;
    }
    const double mean = double(sum / acceptedCount);
    std::cout << "均值=" << mean << '\n';
    return 0;
}
