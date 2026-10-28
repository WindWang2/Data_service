// 案例 08：创建临时工区，演示导入、清理、修改和重开；不修改用户真实工区。
#include <paleo_data/service.h>
#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>
#include <cmath>
#include <iostream>
#include <vector>
using namespace paleo::data;

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    if (app.arguments().size() != 1) {
        std::cerr << "此案例不接收工区路径，全部操作在自动创建的临时工区中进行。\n";
        return 1;
    }
    // 函数退出时，临时目录自动清理。service 在 temporary 后创建，会先关闭数据库。
    QTemporaryDir temporary;
    if (!temporary.isValid()) { std::cerr << "不能创建临时目录。\n"; return 1; }
    const QString projectPath = temporary.path() + "/project";
    const QString lasPath = temporary.path() + "/DEMO-EDIT-1.las";

    // 只为演示生成4点数据：GR含负异常和声明空值；SP的负值应当保留。
    const QByteArray lasText =
        "~Version\nVERS. 2.0 : version\nWRAP. NO : wrap\n"
        "~Well\nWELL. DEMO-EDIT-1 : synthetic well\nNULL. -999.25 : null\n"
        "~Curve\nDEPT.M : depth\nGR.API : gamma\nSP.MV : potential\n"
        "~Ascii\n100 40 -20\n101 -3 -30\n102 -999.25 -40\n103 80 -50\n";
    QFile lasFile(lasPath);
    if (!lasFile.open(QIODevice::WriteOnly | QIODevice::NewOnly)
        || lasFile.write(lasText) != lasText.size()) {
        std::cerr << "不能写入合成LAS。\n"; return 1;
    }
    lasFile.close();

    DataService service;
    std::string error;
    const std::string directory = projectPath.toUtf8().toStdString();
    if (!service.createProject(directory, "临时修改教程（合成数据）", error)) {
        std::cerr << error << '\n'; return 2;
    }
    ImportOptions importOptions;
    ImportReport importReport;
    if (!service.importFile(lasPath.toUtf8().toStdString(), importOptions, importReport, error)) {
        std::cerr << error << '\n'; return 3;
    }
    std::cout << "导入文件=" << importReport.seen << "，待核对=" << importReport.review << '\n';

    std::vector<DataEntry> curves;
    if (!service.listData("curves", 0, curves, error)) {
        std::cerr << error << '\n'; return 4;
    }
    long long grId = 0, spId = 0;
    for (const DataEntry& curve : curves) {
        if (curve.name == "GR") grId = curve.id;
        if (curve.name == "SP") spId = curve.id;
    }
    if (grId == 0 || spId == 0) { std::cerr << "合成曲线没有成功入库。\n"; return 4; }

    // 清理规则只针对已确认的 GR/API，不用于所有曲线，不删除采样位置。
    CurveCleaningRule rule;
    rule.curveType = "GR";
    rule.unit = "API";
    rule.checkMinimum = true;
    rule.minimum = 0;
    rule.replaceInvalidWithMissing = true;
    if (!service.cleanCurve(grId, rule, error)) {
        std::cerr << error << '\n'; return 5;
    }
    CurveSlice cleaned;
    if (!service.readCurveSlice(grId, Missing, Missing, true, cleaned, error)) {
        std::cerr << error << '\n'; return 6;
    }
    if (cleaned.values.size() != 4 || !std::isnan(cleaned.values[1])
        || cleaned.rawValues[1] != -3 || cleaned.quality[1] != 2) {
        std::cerr << "清理结果与演示预期不符。\n"; return 6;
    }
    std::cout << "清理后第1号样点：原值=" << cleaned.rawValues[1]
              << "，处理值=" << cleaned.values[1] << "，质量=" << int(cleaned.quality[1]) << '\n';

    // sampleIndex=1 是整条原始曲线的第2个位置，不是深度，也不是筛选后行号。
    const long long sampleIndex = 1;
    if (!service.updateCurveValue(grId, sampleIndex, 55.0, error)) {
        std::cerr << error << '\n'; return 7;
    }
    service.closeProject(); // 关闭并重开，验证修改已经持久化。
    if (!service.openProject(directory, error)) {
        std::cerr << error << '\n'; return 8;
    }
    CurveSlice reopened;
    if (!service.readCurveSlice(grId, 101, 101, true, reopened, error)) {
        std::cerr << error << '\n'; return 8;
    }
    if (reopened.values.size() != 1 || reopened.values[0] != 55
        || reopened.rawValues[0] != -3 || reopened.quality[0] != 4) {
        std::cerr << "修改未正确持久化。\n"; return 8;
    }
    std::cout << "重开后：深度=" << reopened.depths[0] << "，原始值=" << reopened.rawValues[0]
              << "，处理值=" << reopened.values[0] << "，质量=" << int(reopened.quality[0]) << '\n';
    CurveSlice sp;
    if (!service.readCurveSlice(spId, 100, 100, true, sp, error)) {
        std::cerr << error << '\n'; return 9;
    }
    if (sp.values.size() != 1 || sp.values[0] != -20) {
        std::cerr << "合法SP负值被错误改变。\n"; return 9;
    }
    std::cout << "SP合法负值仍是 " << sp.values[0] << "；真实工区未修改。\n";
    return 0;
}
