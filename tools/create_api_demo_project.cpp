// Creates an explicitly synthetic project for demonstrating the public C++ API.
// No SQL, private parsers, or original survey files are used by this program.
#include "paleo_data/service.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QList>
#include <QtEndian>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>

using namespace paleo::data;

namespace {
constexpr int InlineCount = 24;
constexpr int CrosslineCount = 32;
constexpr int SampleCount = 256;
constexpr int SampleIntervalUs = 2000;
constexpr double SampleIntervalMs = 2.0;
constexpr double StartX = 500000.0;
constexpr double StartY = 4000000.0;
constexpr double Spacing = 25.0;

std::string utf8(const QString& value) { return value.toUtf8().toStdString(); }
void require(bool success, const std::string& error) {
    if (!success) throw std::runtime_error(error.empty() ? "Operation failed" : error);
}
void writeAll(QFile& file, const QByteArray& bytes) {
    require(file.write(bytes) == bytes.size(), utf8(file.errorString()));
}
void writeNewFile(const QString& path, const QByteArray& bytes) {
    QFile file(path);
    require(file.open(QIODevice::WriteOnly | QIODevice::NewOnly), utf8(file.errorString()));
    writeAll(file, bytes);
    require(file.flush(), utf8(file.errorString()));
}
void setU16(QByteArray& bytes, int offset, quint16 value) {
    qToBigEndian(value, bytes.data() + offset);
}
void setU32(QByteArray& bytes, int offset, quint32 value) {
    qToBigEndian(value, bytes.data() + offset);
}

void writeLas(const QString& path) {
    QByteArray bytes(
        "~Version\nVERS. 2.0 : synthetic demonstration\nWRAP. NO : one row per sample\n"
        "~Well\nWELL. DEMO-1 : SYNTHETIC WELL - NOT REAL DATA\n"
        "STRT.M 1000 : start\nSTOP.M 1300 : stop\nSTEP.M 1 : step\n"
        "NULL. -999.25 : explicit source null\n"
        "~Curve\nDEPT.M : synthetic measured depth\nGR.API : synthetic gamma ray\n"
        "SP.MV : synthetic spontaneous potential\n~Ascii\n");
    for (int index = 0; index <= 300; ++index) {
        const double gr = index == 137 ? -999.25
            : 70.0 + 24.0 * std::sin(index / 22.0) + 9.0 * std::sin(index / 5.0);
        const double sp = -55.0 + 20.0 * std::sin(index / 35.0);
        bytes += QByteArray::number(1000 + index) + ' '
            + QByteArray::number(gr, 'f', 6) + ' ' + QByteArray::number(sp, 'f', 6) + '\n';
    }
    writeNewFile(path, bytes);
}

double ricker(double sampleOffset, double width) {
    const double a = sampleOffset / width;
    return (1.0 - 2.0 * a * a) * std::exp(-a * a);
}

void writeSeismic(const QString& path) {
    QFile file(path);
    require(file.open(QIODevice::WriteOnly | QIODevice::NewOnly), utf8(file.errorString()));
    QByteArray text(3200, ' ');
    const QList<QByteArray> descriptions{
        "C01 SYNTHETIC API DEMONSTRATION - NOT REAL SURVEY DATA",
        "C02 24 INLINE X 32 CROSSLINE X 256 IEEE FLOAT SAMPLES",
        "C03 XY: SYNTHETIC_LOCAL_XY; METRES; SPACING 25 M",
        "C04 TIME: SYNTHETIC T=0; TWT MILLISECONDS; SAMPLE INTERVAL 2 MS",
        "C05 DETERMINISTIC THREE RICKER REFLECTORS; NO GEOLOGICAL INTERPRETATION"
    };
    for (int line = 0; line < descriptions.size(); ++line)
        std::memcpy(text.data() + line * 80, descriptions[line].constData(),
                    static_cast<size_t>(std::min<qsizetype>(80, descriptions[line].size())));
    writeAll(file, text);
    QByteArray binary(400, '\0');
    setU16(binary, 16, SampleIntervalUs);
    setU16(binary, 18, SampleIntervalUs);
    setU16(binary, 20, SampleCount);
    setU16(binary, 22, SampleCount);
    setU16(binary, 24, 5); // IEEE float32, big endian.
    setU16(binary, 54, 1); // Metres.
    setU16(binary, 300, 0x0100);
    setU16(binary, 302, 1); // Fixed length traces.
    setU16(binary, 304, 0); // No extended textual headers.
    writeAll(file, binary);
    for (int il = 0; il < InlineCount; ++il) {
        for (int xl = 0; xl < CrosslineCount; ++xl) {
            const quint32 sequence = static_cast<quint32>(il * CrosslineCount + xl + 1);
            const quint32 x = static_cast<quint32>(StartX + xl * Spacing);
            const quint32 y = static_cast<quint32>(StartY + il * Spacing);
            QByteArray header(240, '\0');
            setU32(header, 0, sequence);
            setU32(header, 4, sequence);
            setU32(header, 20, sequence);
            setU16(header, 28, 1); // Seismic trace.
            setU16(header, 70, 1); // Coordinate scalar.
            setU32(header, 72, x);
            setU32(header, 76, y);
            setU16(header, 88, 1); // Length coordinates.
            setU16(header, 108, 0); // Delay recording time in ms.
            setU16(header, 114, SampleCount);
            setU16(header, 116, SampleIntervalUs);
            setU32(header, 180, x);
            setU32(header, 184, y);
            setU32(header, 188, static_cast<quint32>(1000 + il));
            setU32(header, 192, static_cast<quint32>(2000 + xl));
            writeAll(file, header);
            QByteArray amplitudes(SampleCount * 4, '\0');
            const double reflector1 = 45.0 + 0.65 * xl + 0.25 * il;
            const double reflector2 = 100.0 + 0.30 * xl - 0.35 * il;
            const double reflector3 = 170.0 + 0.60 * xl + 6.0 * std::sin(il * 0.25);
            for (int sample = 0; sample < SampleCount; ++sample) {
                const float amplitude = static_cast<float>(
                    ricker(sample - reflector1, 3.8)
                    - 0.8 * ricker(sample - reflector2, 4.6)
                    + 0.65 * ricker(sample - reflector3, 5.0)
                    + 0.025 * std::sin(sample * 0.22 + il * 0.13 + xl * 0.09));
                quint32 bits = 0;
                std::memcpy(&bits, &amplitude, sizeof(bits));
                setU32(amplitudes, sample * 4, bits);
            }
            writeAll(file, amplitudes);
        }
    }
    require(file.flush(), utf8(file.errorString()));
}

void importSource(DataService& service, const QString& path, std::string& error) {
    ImportOptions options;
    options.copySources = false; // Inputs already live in this separate synthetic project.
    options.parsing.depthUnit = "m";
    ImportReport result;
    require(service.importFile(utf8(path), options, result, error), error);
    require(result.failed == 0 && result.seen == 1, "Synthetic source was not imported");
}
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName("paleo-synthetic-api-demo-generator");
    try {
        const auto arguments = app.arguments();
        if (arguments.size() != 2) {
            std::cerr << "usage: create_api_demo_project <new project directory>\n";
            return 2;
        }
        const QString root = QDir::cleanPath(QFileInfo(arguments[1]).absoluteFilePath());
        require(!QFileInfo::exists(QDir(root).filePath("data.sqlite")),
                "Project already exists; refusing to overwrite data.sqlite. Choose a new directory.");
        std::string error;
        DataService service;
        require(service.createProject(utf8(root), "合成数据接口演示（非真实工区）", error), error);
        QDir project(root);
        require(project.mkpath("fixture_inputs"), "Cannot create synthetic input directory");
        const QString las = project.filePath("fixture_inputs/DEMO-1.las");
        const QString formations = project.filePath("fixture_inputs/synthetic_formations.csv");
        const QString segy = project.filePath("fixture_inputs/SYNTHETIC_demo_volume.sgy");
        writeLas(las);
        writeNewFile(formations, QStringLiteral(
            "井号,地层,顶深,底深,相类型\n"
            "DEMO-1,合成层A,1000,1100,合成演示\n"
            "DEMO-1,合成层B,1100,1200,合成演示\n"
            "DEMO-1,合成层C,1200,1300,合成演示\n").toUtf8());
        writeSeismic(segy);
        importSource(service, las, error);
        importSource(service, formations, error);
        importSource(service, segy, error);

        std::vector<WellSummary> wells;
        require(service.listWells(wells, error), error);
        require(wells.size() == 1 && wells.front().name == "DEMO-1" && wells.front().curves == 2,
                "Synthetic well/curve registration mismatch");
        const auto wellId = wells.front().id;
        WellInfo well;
        require(service.readWellInfo(wellId, well, error), error);
        well.wellType = "合成演示井";
        well.remark = "仅演示接口与图形，不是真实测井资料。";
        well.wellheadX = StartX + 12 * Spacing;
        well.wellheadY = StartY + 10 * Spacing;
        well.coordinateSystem = "SYNTHETIC_LOCAL_XY";
        well.coordinateUnit = "m";
        require(service.saveWellInfo(wellId, well, error), error);

        std::vector<DataEntry> volumes;
        require(service.listData("volumes", 0, volumes, error), error);
        require(volumes.size() == 1, "Synthetic volume registration mismatch");
        const auto volumeId = volumes.front().id;
        SeismicVolume volume;
        require(service.readVolume(volumeId, volume, error), error);
        volume.volumeName = "合成演示地震体（非真实资料）";
        volume.amplitudeUnit = "synthetic relative amplitude";
        volume.geometry.traceCount = InlineCount * CrosslineCount;
        volume.geometry.inlineMin = 1000;
        volume.geometry.inlineMax = 1000 + InlineCount - 1;
        volume.geometry.crosslineMin = 2000;
        volume.geometry.crosslineMax = 2000 + CrosslineCount - 1;
        volume.geometry.coordinateSystem = "SYNTHETIC_LOCAL_XY";
        volume.geometry.coordinateUnit = "m";
        volume.sampling.sampleCount = SampleCount;
        volume.sampling.firstTime = 0;
        volume.sampling.sampleInterval = SampleIntervalMs;
        volume.sampling.timeUnit = "ms";
        volume.sampling.timeType = "TWT";
        volume.sampling.timeReference = "synthetic t=0";
        require(service.saveVolume(volumeId, volume, error), error);

        SeismicSection section;
        section.sectionName = "合成演示对角剖面（非真实资料）";
        section.parentVolumeId = static_cast<int>(volumeId);
        section.coordinateSystem = "SYNTHETIC_LOCAL_XY";
        section.coordinateUnit = "m";
        section.pathPoints = {{StartX, StartY},
            {StartX + (CrosslineCount - 1) * Spacing, StartY + (InlineCount - 1) * Spacing}};
        long long sectionId = 0;
        require(service.saveSection(sectionId, section, error), error);

        // Validate the actual public paths that the UI will call; do not fabricate results.
        std::vector<DataEntry> curves;
        require(service.listData("curves", wellId, curves, error), error);
        long long grId = 0;
        long long spId = 0;
        for (const auto& curve : curves) {
            if (curve.name == "GR") grId = curve.id;
            if (curve.name == "SP") spId = curve.id;
        }
        require(grId > 0 && spId > 0, "Synthetic curve IDs missing");
        CurveSlice curve;
        require(service.readCurveSlice(grId, 1000, 1300, true, curve, error), error);
        require(curve.values.size() == 301 && std::isnan(curve.values[137]),
                "Synthetic curve/null round trip failed");
        require(service.readCurveSlice(spId, 1000, 1300, true, curve, error), error);
        require(curve.values.size() == 301 && curve.values.front() < 0,
                "Legitimate negative synthetic SP was lost");
        SectionReadOptions sectionOptions;
        sectionOptions.fullResolution = true;
        sectionOptions.maxColumns = 64;
        sectionOptions.maxValues = 64 * SampleCount;
        SeismicSectionResult sectionResult;
        require(service.extractSection(sectionId, sectionOptions, sectionResult, error), error);
        require(sectionResult.columns > 1 && sectionResult.columns == sectionResult.nativeColumns
                && !sectionResult.decimated && sectionResult.samples == SampleCount
                && sectionResult.vertexColumns == std::vector<int>({0,sectionResult.columns-1})
                && std::all_of(sectionResult.amplitudes.begin(), sectionResult.amplitudes.end(),
                    [](float value) { return std::isfinite(value); }),
                "Synthetic section shape or values incorrect");
        TimeSliceReadOptions sliceOptions;
        sliceOptions.maxInlineCount = InlineCount;
        sliceOptions.maxCrosslineCount = CrosslineCount;
        sliceOptions.maxValues = InlineCount * CrosslineCount;
        SeismicTimeSliceResult timeResult;
        require(service.readTimeSlice(volumeId, 200, sliceOptions, timeResult, error), error);
        require(timeResult.amplitudes.size() == InlineCount * CrosslineCount
                && std::all_of(timeResult.amplitudes.begin(), timeResult.amplitudes.end(),
                    [](float value) { return std::isfinite(value); }),
                "Synthetic time slice shape or values incorrect");

        writeNewFile(project.filePath("README.txt"), QStringLiteral(
            "合成数据接口演示（非真实工区）\n\n"
            "所有曲线、地层、地震振幅和坐标均由固定数学公式生成，不来自用户真实资料。\n"
            "用途：演示 C++ SDK 的导入、查询、返回对象和图形化，不用于地质解释。\n"
            "DEMO-1 井：1000—1300 m，1 m 步长；GR / SP；GR 第137样点是声明空值，SP 保留合法负值。\n"
            "地震体：24 × 32 道，每道256样点，2 ms，0—510 ms TWT；三组倾斜 Ricker 反射。\n"
            "坐标：SYNTHETIC_LOCAL_XY，25 m 网格；不是 EPSG 或真实测区坐标。\n"
            "可直接调用 readCurveSlice、extractSection、readTimeSlice；时间切片建议200 ms。\n"
            "fixture_inputs 保存生成的原文件，当前服务登记的是绝对来源路径。\n"
            "如移动此工区，请通过“重新定位来源”校验并更新 SEG-Y 路径，或在新位置重新生成演示工区。\n"
            "生成器不会覆盖已存在的 data.sqlite。真实资料演示工区与本工区相互独立。\n"
        ).toUtf8());
        std::cout << "project=" << utf8(root) << '\n'
                  << "synthetic=true well_id=" << wellId << " gr_curve_id=" << grId
                  << " sp_curve_id=" << spId << " volume_id=" << volumeId
                  << " section_id=" << sectionId << '\n'
                  << "segy_bytes=" << QFileInfo(segy).size()
                  << " section_values=" << sectionResult.amplitudes.size()
                  << " slice_values=" << timeResult.amplitudes.size()
                  << " suggested_slice_time_ms=200\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n';
        return 1;
    }
}
