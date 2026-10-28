#pragma once
#include "model.h"

namespace paleo::data {

// SEG-Y 文件自身提供的事实。快速检查只读取文件头和少量道头，不读取整个体。
// geometryStatus 明确区分抽样规则网格与完整索引，不能把抽样检查称为全文件校验。
struct SeismicFileInfo {
    std::string engine;             // 实际使用的 segyio 版本及本地补丁说明。
    std::string sourceFile;
    long long fileBytes = 0;
    long long traceCount = 0;      // 固定道长布局核验后计算；不是振幅数组长度。
    int sampleCount = 0;
    int sampleFormat = 0;          // SEG-Y 格式码，例如 1=IBM32、5=IEEE32。
    std::string byteOrder;         // big-endian / little-endian。
    long long firstTraceOffset = 0;
    int extendedTextHeaders = 0;
    double firstTime = Missing;   // 来源道头的延迟时间；不代表已确认地质参考面。
    double sampleInterval = Missing;
    std::string timeUnit;          // 时间轴使用 ms；OWT/TWT 仍由地震体约定单独保存。
    std::string textHeader;        // 已解码的主文本头，供人工核对坐标与处理说明。
    std::string geometryStatus;    // sampled-regular / full-header-index / unavailable。
    int inlineMin = 0, inlineMax = 0, inlineStep = 0, inlineCount = 0;
    int crosslineMin = 0, crosslineMax = 0, crosslineStep = 0, crosslineCount = 0;
    int checkedHeaders = 0;
    int coordinateUnitsCode = 0;    // SEG-Y 道头单位码；1=长度，但不能仅据此断言是米。
    int coordinateScalar = 0;      // 原始缩放码：正数乘，负数除以绝对值，0视为1。
    std::vector<std::string> warnings;
};

struct SeismicTraceReadOptions {
    int firstSample = 0;           // 从第几个样点开始，0为首样点。
    int sampleCount = 0;           // 0表示读取到本道末尾。
    int maxSamples = 1000000;      // 本次最多返回的样点数，超限报错，不暗中截断。
};

// 一道真实数值及其自身时间轴。traceIndex是文件中从0开始的位置，不是来源道号。
struct SeismicTraceResult {
    long long traceIndex = -1;
    long long traceNumber = -1;    // 原道头5–8字节的值，0保留；不保证它等于文件道位置。
    int inlineNumber = 0, crosslineNumber = 0;
    double x = Missing, y = Missing; // 已应用来源缩放码；不自动转换坐标系。
    double firstTime = Missing, sampleInterval = Missing;
    std::string timeUnit;
    std::vector<float> amplitudes; // 负振幅正常保留；缺失/非有限值为NaN。
};

// 基础处理只作用于本次返回的小数组。原始文件和数据库中的登记信息不变。
// 顺序：逐道去均值 → 固定增益 → 滑窗RMS自动增益 → 可选对称限幅。
struct SeismicProcessingOptions {
    bool removeMean = false;      // 开启会改变原本为0的样点；不会自动识别静音/补零区段。
    double gain = 1.0;
    int agcWindowSamples = 0;      // 0关闭AGC，否则为>=3的奇数；窗口不跨道。
    double clipAmplitude = Missing; // NaN关闭，否则须>0；限幅到[-值,+值]。
};

struct SeismicStatistics {
    long long finiteCount = 0, missingCount = 0;
    double minimum = Missing, maximum = Missing;
    double mean = Missing, rms = Missing; // 忽略NaN；全缺失时保持NaN。
};

}
