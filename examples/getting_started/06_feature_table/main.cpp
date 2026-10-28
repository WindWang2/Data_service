// 案例 06：把同井、同一曲线组中的多条曲线组合成算法输入表。
#include <paleo_data/service.h>
#include <QCoreApplication>
#include <iostream>
#include <vector>
using namespace paleo::data;

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    const QStringList arguments = app.arguments();
    if (arguments.size() < 5) {
        std::cerr << "用法：example_06_feature_table <工区> <井编号> <曲线编号1> <曲线编号2> [...]\n";
        return 1;
    }
    bool wellIdIsValid = false;
    const long long wellId = arguments[2].toLongLong(&wellIdIsValid);
    if (!wellIdIsValid || wellId <= 0) { std::cerr << "井编号不合法。\n"; return 1; }
    std::vector<long long> curveIds;
    for (qsizetype i = 3; i < arguments.size(); i = i + 1) {
        bool curveIdIsValid = false;
        const long long curveId = arguments[i].toLongLong(&curveIdIsValid);
        if (!curveIdIsValid || curveId <= 0) { std::cerr << "曲线编号不合法。\n"; return 1; }
        curveIds.push_back(curveId);
    }

    DataService service;
    std::string error;
    if (!service.openProject(arguments[1].toUtf8().toStdString(), error)) {
        std::cerr << error << '\n'; return 2;
    }

    // 该接口负责组合业务数据，不是在这里打开 LAS 或执行数据库语句。
    // 显式传编号，不凭同名曲线随意选择版本；编号顺序决定返回列的顺序。
    WellFeatureTable result;
    if (!service.prepareWellFeatures(wellId, curveIds, Missing, Missing, true, result, error)) {
        std::cerr << "组合失败：" << error << '\n';
        return 3;
    }
    std::cout << "井编号=" << result.wellId << "，深度行数=" << result.depths.size()
              << "，特征列数=" << result.columns.size()
              << "，深度单位=" << result.depthUnit << '\n';

    // 表格结构：depths[row] 是一行的深度；columns[col].values[row] 是该行某条曲线值。
    std::cout << "depth";
    for (const FeatureColumn& column : result.columns) {
        std::cout << ',' << column.name << '(' << column.unit << "),quality";
    }
    std::cout << '\n';
    size_t previewRows = result.depths.size();
    if (previewRows > 10) previewRows = 10;
    for (size_t row = 0; row < previewRows; row = row + 1) {
        std::cout << result.depths[row];
        for (size_t col = 0; col < result.columns.size(); col = col + 1) {
            const FeatureColumn& column = result.columns[col];
            std::cout << ',' << column.values[row] << ',' << int(column.quality[row]);
        }
        std::cout << '\n';
    }
    // 完整矩阵仍在 result；可作为相推理模块的输入，但这里没有执行相分类。
    // 服务要求同一 LogSet，防止不同深度轴、基准的曲线被悄悄拼接。
    // 缺失值不会自动补齐；业务应先决定剔除、掩码或其他处理方法。
    return 0;
}
