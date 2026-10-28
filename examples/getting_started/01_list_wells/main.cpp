// 案例 01：打开工区，查看井有哪些资料。这里只查询，不修改数据库。
#include <paleo_data/service.h> // 服务接口以及 WellSummary 等数据类型
#include <QCoreApplication>    // 当前 SDK 内部使用 Qt，需要初始化一次
#include <iostream>
#include <vector>
using namespace paleo::data;

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    const QStringList arguments = app.arguments();
    if (arguments.size() != 2) {
        std::cerr << "用法：example_01_list_wells <工区目录>\n";
        return 1;
    }

    // 工区目录里应有 data.sqlite；这里不能传测试资料目录或单个 LAS 文件。
    // 转成 UTF-8，才能正确传递中文路径。
    const std::string projectDirectory = arguments[1].toUtf8().toStdString();
    DataService service;
    std::string error; // 方法失败时，这个变量接收具体原因。
    if (!service.openProject(projectDirectory, error)) {
        std::cerr << "打开工区失败：" << error << '\n';
        return 2;
    }

    // bool 是成功/失败标志；真正的结果写入 wells，而不是写入 bool。
    std::vector<WellSummary> wells;
    if (!service.listWells(wells, error)) {
        std::cerr << "查询井失败：" << error << '\n';
        return 3;
    }

    std::cout << "工区：" << service.projectName() << "，井数：" << wells.size() << '\n';
    for (const WellSummary& well : wells) {
        std::cout << "编号=" << well.id << "  井名=" << well.name
                  << "  曲线=" << well.curves << "  分层道=" << well.tracks
                  << "  样品=" << well.samples << "  时深表=" << well.timeDepthTables
                  << "  测斜表=" << well.trajectorySurveys
                  << "  有井坐标=" << well.hasCoordinates << '\n';
    }
    // curves==0 只表示当前工区没有该井的已入库曲线，不表示其他资料丢失。
    // well.id 是数据库稳定编号，不是 wells 数组中的位置。
    return 0;
}
