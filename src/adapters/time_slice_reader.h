#pragma once
#include "paleo_data/service.h"
#include "segy_reader.h"

namespace paleo::data::segy_detail {
bool readSliceTiled(Reader& reader, double timeMs, const TimeSliceReadOptions& options,
                    SeismicTimeSliceResult& result, std::string& error,
                    const CancelCheck& cancel, const TimeSliceTileCallback& tileReady,
                    bool strictCoordinates=false, double residualLimit=3.0);
void clearSliceCache();
}
