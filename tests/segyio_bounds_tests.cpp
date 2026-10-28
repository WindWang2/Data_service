// Repository fixtures are generated synthetic examples; no private source data is bundled.
#include <segyio/segy.h>
#include <climits>
#include <iostream>

// 用只提供大小的抽象数据源核对边界；不创建巨型文件、不分配道数组。
int main() {
    segy_datasource source{};
    long long bytes = 0;
    source.stream = &bytes;
    source.size = [](segy_datasource* self, long long* size) { *size = *static_cast<long long*>(self->stream); return 0; };
    source.metadata.trace0 = 3600;
    source.metadata.traceheader_count = 1;
    source.metadata.trace_bsize = 4;
    int count = 0;
    bytes = 3600LL + static_cast<long long>(INT_MAX) * 244;
    if (segy_traces(&source,&count) != SEGY_OK || count != INT_MAX) return 1;
    bytes += 244;
    if (segy_traces(&source,&count) != SEGY_INVALID_FIELD_VALUE) return 2;
    source.metadata.traceheader_count = 0; source.metadata.trace_bsize = 0;
    if (segy_traces(&source,&count) != SEGY_INVALID_FIELD_VALUE) return 3;
    source.metadata.traceheader_count = INT_MAX; source.metadata.trace_bsize = INT_MAX;
    bytes = 3600LL + static_cast<long long>(INT_MAX) * 241;
    if (segy_traces(&source,&count) != SEGY_OK || count != 1) return 4;
    std::cout << "SEG-Y 64-bit record arithmetic and trace-count bounds passed\n";
    return 0;
}
