#pragma once

#include "fit/activity.h"

#include <string>

namespace Export {

// A GPX 1.1 track of the ride: position, elevation and time for each record
// that has a position, with heart rate, cadence and temperature in Garmin's
// TrackPointExtension and power as a plain <power> element, which is where the
// services that read power from GPX look for it. A stretch with no fix (a
// tunnel) starts a new segment instead of drawing a line across it.
//
// Empty string if the ride has no positions at all (an indoor session): there
// is nothing a GPX could hold.
std::string toGpx(const Fit::Activity &activity, const std::string &name);

// "2026-08-03T07:01:45Z"
std::string isoUtc(int64_t unixSeconds);

std::string xmlEscape(const std::string &text);

} // namespace Export
