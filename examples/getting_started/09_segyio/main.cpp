// 案例09：通过统一C++服务按需读取SEG-Y，并处理本次返回的小剖面。
// 工区须先在界面中登记SEG-Y；本程序不复制地震体，不把振幅写进SQLite。
#include <paleo_data/service.h>
#include <QCoreApplication>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

using namespace paleo::data;

// 普通函数：打印一组已经算好的统计值，不重新读地震文件。
void printStatistics(const std::string& title, const SeismicStatistics& statistics) {
    std::cout << title << ": finite=" << statistics.finiteCount
              << " missing=" << statistics.missingCount
              << " min=" << statistics.minimum
              << " max=" << statistics.maximum
              << " mean=" << statistics.mean
              << " rms=" << statistics.rms << '\n';
}

int main(int argc, char** argv) {
    // 服务内部使用Qt；已有QApplication的界面程序不用再创建此对象。
    // 用Qt读取命令行是为了在Windows下正确保留中文工区路径。
    QCoreApplication app(argc, argv);
    const QStringList arguments = app.arguments();
    if (arguments.size() != 2 && arguments.size() != 3) {
        std::cerr << "用法：example_09_segyio <工区目录> [地震体编号]\n";
        return 1;
    }

    DataService service;
    std::string error;
    const std::string projectPath = arguments[1].toUtf8().toStdString();
    if (!service.openProject(projectPath, error)) {
        std::cerr << "打开工区失败：" << error << '\n';
        return 2;
    }

    // 第1步：查询工区里已登记的地震体，拿到稳定业务编号。
    // 这一步只有资料目录，不读振幅；后续read方法才返回真正的数值数组。
    std::vector<DataEntry> volumes;
    if (!service.listData("volumes", 0, volumes, error)) {
        std::cerr << error << '\n'; return 3;
    }
    for (const DataEntry& volume : volumes)
        std::cout << "volumeId=" << volume.id << " name=" << volume.name << '\n';

    long long volumeId = 0;
    if (arguments.size() == 3) {
        bool valid = false;
        volumeId = arguments[2].toLongLong(&valid);
        if (!valid || volumeId <= 0) {
            std::cerr << "地震体编号必须是正整数。\n"; return 3;
        }
    } else if (volumes.size() == 1) {
        volumeId = volumes[0].id;
    } else {
        std::cerr << "工区内有 " << volumes.size()
                  << " 个地震体，请先导入文件或在命令行明确选择编号。\n";
        return 3;
    }
    // 不把vector下标当成数据库编号，也不默认不同工区有相同编号。
    bool found = false;
    for (const DataEntry& volume : volumes) if (volume.id == volumeId) found = true;
    if (!found) { std::cerr << "当前工区没有此地震体编号。\n"; return 3; }

    // 第2步：检查文件头与网格候选。无需预先填写CRS、TWT或时间基准。
    // 快速探测不会扫描整个地震体；不自动把抽样规则网格称为全文件已验证。
    SeismicFileInfo info;
    if (!service.inspectSeismic(volumeId, info, error)) {
        std::cerr << "检查SEG-Y失败：" << error << '\n'; return 4;
    }
    std::cout << "engine=" << info.engine << '\n'
              << "source=" << info.sourceFile << '\n'
              << "bytes=" << info.fileBytes << " traces=" << info.traceCount
              << " samplesPerTrace=" << info.sampleCount << '\n'
              << "format=" << info.sampleFormat << " order=" << info.byteOrder
              << " firstTraceOffset=" << info.firstTraceOffset << '\n'
              << "geometry=" << info.geometryStatus
              << " checkedHeaders=" << info.checkedHeaders << '\n';
    for (const std::string& warning : info.warnings) std::cout << "说明：" << warning << '\n';

    // 第3步：读取文件中间的一道。traceIndex从0开始，是物理位置，不是原始道号。
    // sampleCount=0表示读到本道末尾；本例每次只持有一道，不持有全体振幅。
    const long long traceIndex = info.traceCount / 2;
    SeismicTraceReadOptions traceOptions;
    traceOptions.firstSample = 0;
    traceOptions.sampleCount = 0;
    traceOptions.maxSamples = 1000000;
    SeismicTraceResult trace;
    if (!service.readSeismicTrace(volumeId, traceIndex, traceOptions, trace, error)) {
        std::cerr << "读取单道失败：" << error << '\n'; return 5;
    }
    std::cout << "traceIndex=" << trace.traceIndex
              << " sourceTraceNumber=" << trace.traceNumber
              << " IL=" << trace.inlineNumber << " XL=" << trace.crosslineNumber
              << " XY=" << trace.x << ',' << trace.y
              << " returnedSamples=" << trace.amplitudes.size() << '\n';
    // 全道数据在trace.amplitudes中；这里只打印前10项，避免刷满控制台。
    for (size_t i = 0; i < trace.amplitudes.size() && i < 10; ++i) {
        const double time = trace.firstTime + i * trace.sampleInterval;
        std::cout << "time=" << time << ' ' << trace.timeUnit
                  << " amplitude=" << trace.amplitudes[i] << '\n';
    }

    // 第4步：选择真实的中间INLINE。对于不等步长/未知网格，不伪造测线号。
    if (info.inlineCount < 1 || (info.inlineCount > 1 && info.inlineStep <= 0)) {
        std::cerr << "单道读取完成，但当前网格不能自动选中间INLINE。请核对网格或明确测线号。\n";
        return 6;
    }
    const long long middleInline = static_cast<long long>(info.inlineMin)
                                + static_cast<long long>(info.inlineCount / 2) * info.inlineStep;
    if (middleInline < std::numeric_limits<int>::min() || middleInline > std::numeric_limits<int>::max()) {
        std::cerr << "测线号超出当前接口范围。\n"; return 6;
    }
    const int inlineNumber = static_cast<int>(middleInline);
    SectionReadOptions lineOptions;
    lineOptions.firstTime = Missing; // 不裁剪首时间；这里的时间单位为文件采样轴的ms
    lineOptions.lastTime = Missing;  // 不裁剪末时间
    lineOptions.maxColumns = 32;     // 最多32道，沿XLINE轴抽样，包含两端
    lineOptions.maxValues = 262144;  // 振幅数组上限；不是要求加载整个测线/地震体
    SeismicSectionResult rawSection;
    if (!service.readSeismicLine(volumeId, "inline", inlineNumber, lineOptions, rawSection, error)) {
        std::cerr << "读取INLINE失败：" << error << '\n'; return 6;
    }
    std::cout << "INLINE=" << inlineNumber << " columns=" << rawSection.columns
              << " samples=" << rawSection.samples
              << " values=" << rawSection.amplitudes.size() << '\n';
    // 数组为列优先：先放第0道的所有样点，再放第1道，以此类推。
    // 第column道、第sample样点：amplitudes[column * samples + sample]。
    if (rawSection.columns > 0 && rawSection.samples > 0) {
        const size_t column = 0;
        const size_t sample = size_t(rawSection.samples / 2);
        const size_t index = column * size_t(rawSection.samples) + sample;
        std::cout << "sectionMiddleAmplitude=" << rawSection.amplitudes[index] << '\n';
    }
    // 要看横测线，只把方向改成"crossline"，传入真实XLINE号，返回类型不变。

    // 第5步：读取中间时间样点的8×8预览。每个网格位置只读一个振幅样点。
    const double timeMs = info.firstTime + (info.sampleCount / 2) * info.sampleInterval;
    TimeSliceReadOptions sliceOptions;
    sliceOptions.maxInlineCount = 8;
    sliceOptions.maxCrosslineCount = 8;
    sliceOptions.maxValues = 64;
    SeismicTimeSliceResult slice;
    if (!service.readSeismicTimeSlice(volumeId, timeMs, sliceOptions, slice, error)) {
        std::cerr << "读取时间切片失败：" << error << '\n'; return 7;
    }
    std::cout << "actualTime=" << slice.actualTime << ' ' << slice.timeUnit
              << " grid=" << slice.inlineNumbers.size() << 'x' << slice.crosslineNumbers.size()
              << " values=" << slice.amplitudes.size() << '\n';
    // 时间切片是行优先，和上面的竖向剖面不同：
    // amplitudes[inlineIndex * crosslineNumbers.size() + crosslineIndex]。
    // 0振幅仍是有效来源值；NaN才是缺失，不能把它强行补成0。

    // 第6步：只处理已经取出的rawSection数组，原始SEG-Y和rawSection都保持不变。
    SeismicProcessingOptions processing;
    processing.removeMean = false;  // 默认保留原有0振幅，不逐道减均值
    // 若明确设为true，会减去本道均值；原本为0的静音样点也会发生变化。
    processing.gain = 1.0;          // 固定增益；设置2.0即把振幅乘2
    processing.agcWindowSamples = 101; // 101样点滑窗RMS自动增益，窗口不跨道
    processing.clipAmplitude = 3.0; // 最后限制到[-3,+3]，Missing可关闭限幅
    SeismicSectionResult processedSection;
    if (!service.processSeismicSection(rawSection, processing, processedSection, error)) {
        std::cerr << "处理剖面失败：" << error << '\n'; return 8;
    }
    // 如只想处理一道，可用processSeismicTrace(trace, processing, outputTrace, error)。
    // 本例只做AGC→限幅。可选去均值在固定增益前执行；这不是偏移或反演。

    SeismicStatistics before, after;
    if (!service.seismicStatistics(rawSection.amplitudes, before, error)
        || !service.seismicStatistics(processedSection.amplitudes, after, error)) {
        std::cerr << "统计失败：" << error << '\n'; return 9;
    }
    printStatistics("原始剖面", before);
    printStatistics("处理剖面", after);

    // UI/算法可直接消费以上C++对象；需要JSON时由调用方另行序列化。
    // 全部读取方法均可附加取消回调：[] { return 用户是否已取消; }。
    // 一个调用线程使用一个DataService，不在多个线程共享同一服务实例。
    return 0;
}
