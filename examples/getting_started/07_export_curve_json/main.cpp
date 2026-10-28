// 案例 07：读取 C++ 对象，再在程序边界把完整对象序列化成 JSON。
#include <paleo_data/service.h>
#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <cmath>
#include <iostream>
using namespace paleo::data;

// JSON 不支持 NaN/Infinity。缺失值写 null，不写 0，避免把缺失误当成测量值。
QJsonValue jsonNumber(double number) {
    if (std::isfinite(number)) return QJsonValue(number);
    return QJsonValue(QJsonValue::Null);
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    const QStringList arguments = app.arguments();
    if (arguments.size() != 4 && arguments.size() != 6) {
        std::cerr << "用法：example_07_export_curve_json <工区> <曲线编号> <新JSON文件> [顶深 底深]\n";
        return 1;
    }
    bool idIsValid = false;
    const long long curveId = arguments[2].toLongLong(&idIsValid);
    if (!idIsValid || curveId <= 0) { std::cerr << "曲线编号不合法。\n"; return 1; }
    double topDepth = Missing, bottomDepth = Missing;
    if (arguments.size() == 6) {
        bool topIsValid = false, bottomIsValid = false;
        topDepth = arguments[4].toDouble(&topIsValid);
        bottomDepth = arguments[5].toDouble(&bottomIsValid);
        if (!topIsValid || !bottomIsValid || !std::isfinite(topDepth)
            || !std::isfinite(bottomDepth) || topDepth > bottomDepth) {
            std::cerr << "深度范围不合法。\n"; return 1;
        }
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

    // 到这里 result 已是能直接绘图/计算的数据对象。序列化只是额外的输出步骤。
    QJsonArray depths, rawValues, values, quality;
    for (size_t i = 0; i < result.depths.size(); i = i + 1) {
        depths.append(jsonNumber(result.depths[i]));
        rawValues.append(jsonNumber(result.rawValues[i]));
        values.append(jsonNumber(result.values[i]));
        quality.append(int(result.quality[i]));
    }
    QJsonObject object;
    // 将 long long 编号转成字符串，避免其他语言的 JSON 数值精度限制。
    object["curveId"] = QString::number(result.curveId);
    object["name"] = QString::fromUtf8(result.name);
    object["unit"] = QString::fromUtf8(result.unit);
    object["depthUnit"] = QString::fromUtf8(result.depthUnit);
    object["sampleCount"] = qint64(result.depths.size());
    object["depths"] = depths;
    object["rawValues"] = rawValues;
    object["values"] = values;
    object["quality"] = quality;
    object["nonFiniteNumbers"] = "null";
    const QByteArray jsonBytes = QJsonDocument(object).toJson(QJsonDocument::Indented);

    // NewOnly 是“只允许新文件”：即使同名文件刚被别人创建，也不会覆盖。
    QFile output(arguments[3]);
    if (!output.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
        std::cerr << "不能创建新文件：" << output.errorString().toUtf8().constData() << '\n';
        return 4;
    }
    if (output.write(jsonBytes) != jsonBytes.size() || !output.flush()) {
        std::cerr << "JSON 写入失败，移除本次创建的不完整文件。\n";
        output.close();
        output.remove();
        return 5;
    }
    output.close();
    std::cout << "已导出完整 " << result.depths.size() << " 个点，字节数=" << jsonBytes.size() << '\n';
    // 和界面只显示前10项的摘要不同，本文件包括本次查询范围内的全部数组。
    // JSON 比二进制更占空间；大地震体不应整体转成 JSON。
    return 0;
}
