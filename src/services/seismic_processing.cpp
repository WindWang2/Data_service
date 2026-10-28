// 业务层：只处理调用方已经读取的有限数值，不访问/修改SEG-Y源文件。
#include "paleo_data/service.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace paleo::data {
namespace {
constexpr size_t ValueLimit = 64000000; // 与高清读取的振幅预算保持一致。
bool fail(std::string& error, const char* text) { error = text; return false; }
bool cancelled(const CancelCheck& check, std::string& error) {
    if (check && check()) { error = "地震处理已取消"; return true; }
    return false;
}
bool validate(const SeismicProcessingOptions& o, std::string& error) {
    if (!std::isfinite(o.gain)) return fail(error, "增益必须是有限数值");
    if (o.agcWindowSamples != 0 && (o.agcWindowSamples < 3 || o.agcWindowSamples > 1000001 || o.agcWindowSamples % 2 == 0))
        return fail(error, "AGC窗口须为3到1000001之间的奇数，或0表示关闭");
    if (!std::isnan(o.clipAmplitude) && (!std::isfinite(o.clipAmplitude) || o.clipAmplitude <= 0))
        return fail(error, "限幅必须大于0，或Missing表示关闭");
    return true;
}
bool process(std::vector<float>& values, size_t samples, const SeismicProcessingOptions& o,
             std::string& error, const CancelCheck& check) {
    const float missing = std::numeric_limits<float>::quiet_NaN();
    // 每次只为一道建立前缀和，AGC不会把相邻地震道混在同一窗口里。
    std::vector<double> sums;
    std::vector<size_t> counts;
    if (o.agcWindowSamples) { sums.resize(samples + 1); counts.resize(samples + 1); }
    for (size_t base = 0; base < values.size(); base += samples) {
        if (cancelled(check, error)) return false;
        double mean = 0;
        size_t finiteCount = 0;
        if (o.removeMean) {
            for (size_t j = 0; j < samples; ++j) {
                if ((j % 4096) == 0 && cancelled(check, error)) return false;
                const float value = values[base + j];
                if (std::isfinite(value)) { mean += value; ++finiteCount; }
            }
            if (finiteCount) mean /= double(finiteCount);
        }
        for (size_t j = 0; j < samples; ++j) {
            if ((j % 4096) == 0 && cancelled(check, error)) return false;
            float& value = values[base + j];
            if (!std::isfinite(value)) { value = missing; continue; }
            const double changed = (double(value) - mean) * o.gain;
            if (!std::isfinite(changed) || std::abs(changed) > std::numeric_limits<float>::max())
                return fail(error, "增益导致振幅超出float范围，请降低增益");
            value = static_cast<float>(changed);
        }
        if (o.agcWindowSamples) {
            sums[0] = 0; counts[0] = 0;
            for (size_t j = 0; j < samples; ++j) {
                if ((j % 4096) == 0 && cancelled(check, error)) return false;
                const double value = values[base + j];
                sums[j + 1] = sums[j] + (std::isfinite(value) ? value * value : 0);
                counts[j + 1] = counts[j] + size_t(std::isfinite(value));
            }
            const size_t radius = size_t(o.agcWindowSamples / 2);
            for (size_t j = 0; j < samples; ++j) {
                if ((j % 4096) == 0 && cancelled(check, error)) return false;
                float& value = values[base + j];
                if (!std::isfinite(value)) continue;
                const size_t begin = j > radius ? j - radius : 0;
                const size_t end = std::min(samples, j + radius + 1);
                const size_t count = counts[end] - counts[begin];
                const double square = std::max(0.0, sums[end] - sums[begin]);
                const double rms = count ? std::sqrt(square / double(count)) : 0;
                if (rms > 0) value = static_cast<float>(double(value) / rms);
            }
        }
        if (std::isfinite(o.clipAmplitude)) {
            for (size_t j = 0; j < samples; ++j) {
                if ((j % 4096) == 0 && cancelled(check, error)) return false;
                float& value = values[base + j];
                if (std::isfinite(value)) value = static_cast<float>(std::clamp(double(value), -o.clipAmplitude, o.clipAmplitude));
            }
        }
    }
    return !cancelled(check, error);
}
}

bool DataService::processSeismicTrace(const SeismicTraceResult& input, const SeismicProcessingOptions& options,
                                     SeismicTraceResult& result, std::string& error, const CancelCheck& check) const {
    error.clear();
    // 先复制再清空输出，使 processSeismicTrace(trace, options, trace, ...) 也能安全使用。
    if (input.amplitudes.empty() || input.amplitudes.size() > ValueLimit) {
        result = {}; return fail(error, "待处理地震道需有1到64000000个样点");
    }
    if (!validate(options, error) || cancelled(check, error)) { result = {}; return false; }
    auto output = input; result = {};
    if (!process(output.amplitudes, output.amplitudes.size(), options, error, check)) return false;
    result = std::move(output); return true;
}

bool DataService::processSeismicSection(const SeismicSectionResult& input, const SeismicProcessingOptions& options,
                                       SeismicSectionResult& result, std::string& error, const CancelCheck& check) const {
    error.clear();
    if (input.columns <= 0 || input.samples <= 0 || input.amplitudes.size() > ValueLimit ||
        static_cast<unsigned long long>(input.columns) * input.samples != input.amplitudes.size()) {
        result = {}; return fail(error, "剖面尺寸与振幅数组不一致，或超过64000000值上限");
    }
    const auto validColumns = [&](size_t count) { return count == 0 || count == size_t(input.columns); };
    if (!validColumns(input.x.size()) || !validColumns(input.y.size()) || !validColumns(input.traceIndexes.size()) ||
        !validColumns(input.inlineNumbers.size()) || !validColumns(input.crosslineNumbers.size()) ||
        !validColumns(input.pathX.size()) || !validColumns(input.pathY.size()) || !validColumns(input.distances.size()) ||
        std::any_of(input.vertexColumns.begin(),input.vertexColumns.end(),[&](int c){return c<0||c>=input.columns;})) {
        result = {}; return fail(error, "剖面位置数组与列数不一致");
    }
    if (!validate(options, error) || cancelled(check, error)) { result = {}; return false; }
    auto output = input; result = {};
    if (!process(output.amplitudes, size_t(output.samples), options, error, check)) return false;
    result = std::move(output); return true;
}

bool DataService::seismicStatistics(const std::vector<float>& amplitudes, SeismicStatistics& result,
                                    std::string& error, const CancelCheck& check) const {
    result = {}; error.clear();
    if (amplitudes.size() > ValueLimit) return fail(error, "统计数组超过64000000值上限");
    SeismicStatistics output; double sum = 0, square = 0;
    for (size_t i = 0; i < amplitudes.size(); ++i) {
        if ((i % 4096) == 0 && cancelled(check, error)) return false;
        const double value = amplitudes[i];
        if (!std::isfinite(value)) { ++output.missingCount; continue; }
        if (output.finiteCount == 0) output.minimum = output.maximum = value;
        else { output.minimum = std::min(output.minimum, value); output.maximum = std::max(output.maximum, value); }
        ++output.finiteCount; sum += value; square += value * value;
    }
    if (output.finiteCount) { output.mean = sum / output.finiteCount; output.rms = std::sqrt(square / output.finiteCount); }
    if (cancelled(check, error)) return false;
    result = output; return true;
}
}
