// Repository fixtures are generated synthetic examples; no private source data is bundled.
#include "paleo_data/service.h"
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace paleo::data;
void check(bool good, const std::string& text) { if (!good) throw std::runtime_error(text); }
int main() {
    try {
        DataService service; std::string error;
        SeismicTraceResult raw; raw.traceIndex = 17; raw.firstTime = 100; raw.sampleInterval = 2;
        raw.amplitudes = {-3, -1, 1, 3, float(Missing)};
        SeismicTraceResult out; SeismicProcessingOptions options;
        options.gain = 2; options.clipAmplitude = 4;
        check(service.processSeismicTrace(raw, options, out, error), error);
        check(out.amplitudes[0] == -4 && out.amplitudes[3] == 4 && std::isnan(out.amplitudes[4]), "gain/clipping preserve signed values and missing");
        check(raw.amplitudes[0] == -3 && out.traceIndex == 17 && out.firstTime == 100, "original and axes preserved");
        SeismicStatistics stats;
        check(service.seismicStatistics(raw.amplitudes, stats, error), error);
        check(stats.finiteCount == 4 && stats.missingCount == 1 && stats.mean == 0 && std::abs(stats.rms - std::sqrt(5.0)) < 1e-12, "signed RMS and missing statistics");
        raw.amplitudes = {1, 3, float(Missing)}; options = {}; options.removeMean = true;
        check(service.processSeismicTrace(raw, options, raw, error), error);
        check(raw.amplitudes[0] == -1 && raw.amplitudes[1] == 1 && std::isnan(raw.amplitudes[2]), "in-place output and finite mean");
        SeismicSectionResult section; section.columns = 2; section.samples = 3;
        section.amplitudes = {1, 1, 1, 10, 10, 10}; options = {}; options.agcWindowSamples = 3;
        SeismicSectionResult processed;
        check(service.processSeismicSection(section, options, processed, error), error);
        for (float v : processed.amplitudes) check(std::abs(v - 1) < 1e-6, "AGC must not mix adjacent traces");
        section.amplitudes = {0, 0, 0, float(Missing), float(Missing), float(Missing)};
        check(service.processSeismicSection(section, options, processed, error), error);
        check(processed.amplitudes[0] == 0 && std::isnan(processed.amplitudes[5]), "zero and all-missing AGC");
        check(service.seismicStatistics({float(Missing)}, stats, error) && stats.finiteCount == 0 && std::isnan(stats.rms), "all missing stats");
        options.agcWindowSamples = 4;
        check(!service.processSeismicSection(section, options, processed, error) && processed.amplitudes.empty(), "invalid even window");
        options = {}; section.amplitudes.pop_back();
        check(!service.processSeismicSection(section, options, processed, error) && processed.amplitudes.empty(), "malformed dimensions");
        raw.amplitudes = {1, 2};
        check(!service.processSeismicTrace(raw, options, out, error, [] { return true; }) && out.amplitudes.empty(), "cancel clears output");
        options.gain = std::numeric_limits<double>::max();
        check(!service.processSeismicTrace(raw, options, out, error) && out.amplitudes.empty(), "overflow reported without partial output");
        check(!service.seismicStatistics(raw.amplitudes, stats, error, [] { return true; }) && stats.finiteCount == 0, "cancel statistics");
        std::cout << "Seismic processing checks passed\n"; return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
