// 高清读取示例：返回真实振幅数组，不把放大后的图片当成新增数据。
// 输入资料始终在仓库外；本程序不会修改或复制原始 SEG-Y。
#include <paleo_data/service.h>
#include <QCoreApplication>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace paleo::data;

double number(const std::string& text) {
    size_t used = 0;
    const double value = std::stod(text, &used);
    if (used != text.size() || !std::isfinite(value))
        throw std::runtime_error("参数必须为有限数值");
    return value;
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    const auto args = app.arguments(); // Windows 下通过 Qt 取得 UTF-8 中文路径。
    if (args.size() < 5) {
        std::cerr << "用法：example_10_hd_seismic <工区> <地震体编号> inline <测线号>\n"
                     "      example_10_hd_seismic <工区> <地震体编号> slice <时间ms>\n"
                     "      example_10_hd_seismic <工区> <地震体编号> wall <X1> <Y1> <X2> <Y2> [X3 Y3 ...]\n";
        return 1;
    }
    try {
        DataService service;
        std::string error;
        if (!service.openProject(args[1].toUtf8().toStdString(), error))
            throw std::runtime_error(error);
        bool idValid = false;
        const long long volumeId = args[2].toLongLong(&idValid);
        if (!idValid || volumeId <= 0) throw std::runtime_error("地震体编号必须来自当前工区的资料目录");
        const auto mode = args[3];

        // fullResolution=true 保留本次请求的原始分辨率。
        // maxValues 是内存保护：6400 万个 float 约 256 MB，坐标/图像/处理副本另占内存。
        // 超限会返回 false，不会暗中降低道数；可通过 firstTime/lastTime 缩小剖面时间窗。
        SectionReadOptions options;
        options.fullResolution = true;
        options.maxValues = 64000000;
        SeismicSectionResult section;

        if (mode == "slice") {
            if (args.size() != 5) throw std::runtime_error("slice 只接受一个时间参数");
            TimeSliceReadOptions sliceOptions;
            sliceOptions.fullResolution = true;
            sliceOptions.maxValues = 64000000;
            SeismicTimeSliceResult slice;
            const double timeMs = number(args[4].toStdString());
            if (!service.readSeismicTimeSlice(volumeId, timeMs, sliceOptions, slice, error))
                throw std::runtime_error(error);
            // 切片为行优先：第 i 条 IL、第 j 条 XL 的值在 i * XL数量 + j。
            std::cout << "IL=" << slice.inlineNumbers.size() << " XL=" << slice.crosslineNumbers.size()
                      << " actualTimeMs=" << slice.actualTime << " values=" << slice.amplitudes.size()
                      << " decimated=" << slice.decimated << '\n';
            return 0;
        }
        if (mode == "inline") {
            if (args.size() != 5) throw std::runtime_error("inline 只接受一个实际测线号");
            const double line = number(args[4].toStdString());
            if (std::trunc(line) != line || line < std::numeric_limits<int>::min() || line > std::numeric_limits<int>::max())
                throw std::runtime_error("测线号必须是 int 范围内的整数");
            if (!service.readSeismicLine(volumeId, "inline", int(line), options, section, error))
                throw std::runtime_error(error);
        } else if (mode == "wall") {
            if (args.size() < 8 || (args.size() - 4) % 2 != 0)
                throw std::runtime_error("剖面墙至少需要两组 X Y，按路径顺序填写");
            if (volumeId > std::numeric_limits<int>::max())
                throw std::runtime_error("地震体编号超出当前路径对象的整数范围");
            SeismicVolume volume;
            if (!service.readVolume(volumeId, volume, error)) throw std::runtime_error(error);
            SeismicSection request;
            request.parentVolumeId = static_cast<int>(volumeId);
            request.sectionName = "临时多点剖面墙";
            // 此示例约定输入 XY 已经在地震体的已确认坐标系中，不执行投影换算。
            // 空坐标系或未确认时间约定会被服务拒绝，请先在界面核对资料。
            request.coordinateSystem = volume.geometry.coordinateSystem;
            request.coordinateUnit = volume.geometry.coordinateUnit;
            for (int i = 4; i < args.size(); i += 2)
                request.pathPoints.push_back({number(args[i].toStdString()), number(args[i + 1].toStdString())});
            // 直接传对象，无需先写入数据库。若要保存路径，可调用 saveSection。
            if (!service.extractSection(request, options, section, error)) throw std::runtime_error(error);
        } else {
            throw std::runtime_error("模式应为 inline / slice / wall");
        }

        std::cout << "columns=" << section.columns << " samples=" << section.samples
                  << " values=" << section.amplitudes.size() << " decimated=" << section.decimated << '\n';
        // 列优先：amplitudes[column * samples + sample] 是来源道的真实振幅。
        // pathX/pathY：墙的位置；distances：沿路径距离；vertexColumns：原始转折点列号。
        // x/y 和 traceIndexes：实际取样的最近道中心及其文件位置，便于检查偏移。
        // 把 pathX/pathY + 时间轴 + amplitudes 交给图形控件即可构建剖面墙。
        for (int column : section.vertexColumns)
            std::cout << "vertex column=" << column << " XY=" << section.pathX[column] << ',' << section.pathY[column]
                      << " distance=" << section.distances[column] << '\n';
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n';
        return 2;
    }
}
