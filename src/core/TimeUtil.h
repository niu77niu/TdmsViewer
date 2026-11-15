// ---------------------------------------------------------------------------
// TimeUtil.h - formatting helpers shared by the plot axis, tooltips and status.
// ---------------------------------------------------------------------------
#pragma once

#include <QString>

namespace tdms {

// Tick label for a relative time axis (x in seconds). `span` is the visible
// range in seconds and selects the textual form ("12.5 s", "03:20", "1:02:03").
QString formatRelativeTick(double seconds, double span);

// Tick label for an absolute time axis (value in msecs since epoch). `spanMs`
// is the visible range and selects the date/time pattern.
QString formatAbsoluteTick(qint64 msecs, double spanMs);

// Readout for the cursor tracker: "+12.345 s" / "+01:02:03.500".
QString formatElapsed(double seconds);

// Human readable sample interval: "10 ms（100 Hz）".
QString formatSampleInterval(double seconds);

// Duration without unit decoration, used for tooltips.
QString formatDurationShort(double seconds);

} // namespace tdms
