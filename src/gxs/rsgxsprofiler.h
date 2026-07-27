/*******************************************************************************
 * libretroshare/src/gxs: rsgxsprofiler.h                                      *
 *                                                                             *
 * libretroshare: retroshare core library                                      *
 *                                                                             *
 * Copyright (C) 2026  Retroshare Team <contact@retroshare.cc>                 *
 *                                                                             *
 * This program is free software: you can redistribute it and/or modify        *
 * it under the terms of the GNU Lesser General Public License as              *
 * published by the Free Software Foundation, either version 3 of the          *
 * License, or (at your option) any later version.                             *
 *                                                                             *
 * This program is distributed in the hope that it will be useful,             *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of              *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the                *
 * GNU Lesser General Public License for more details.                         *
 *                                                                             *
 * You should have received a copy of the GNU Lesser General Public License    *
 * along with this program. If not, see <https://www.gnu.org/licenses/>.       *
 *                                                                             *
 *******************************************************************************/
#pragma once

#include <chrono>
#include <cstdlib>

#include "util/rsdebug.h"

/**
 * Opt-in profiling for the GXS message loading path.
 *
 * Loading a group with many messages (a channel with thousands of posts) goes
 * through several layers, each with its own cost: SQL retrieval, deserialisation,
 * conversion to service structures, then the model update in the GUI thread.
 * These helpers let each layer report its own timing so the breakdown can be
 * read directly from the log.
 *
 * Profiling is off unless the RS_GXS_PROFILE environment variable is set. Its
 * value is a reporting threshold in milliseconds, so that only the operations
 * worth looking at are reported:
 *
 *   RS_GXS_PROFILE=0     report everything
 *   RS_GXS_PROFILE=100   report operations taking 100ms or more
 */
namespace RsGxsProfiler {

/** @return -1 when profiling is disabled, otherwise the threshold in ms. */
inline long thresholdMs()
{
	static const long sThreshold = []() -> long
	{
		const char* env = getenv("RS_GXS_PROFILE");
		if(!env) return -1;

		char* end = nullptr;
		const long value = strtol(env, &end, 10);

		return (end == env) ? 0 : value;	// set but not a number: report everything
	}();

	return sThreshold;
}

inline bool enabled() { return thresholdMs() >= 0; }

/** Monotonic stopwatch, cheap enough to be constructed unconditionally. */
class Timer
{
public:
	Timer(): mStart(std::chrono::steady_clock::now()) {}

	void restart() { mStart = std::chrono::steady_clock::now(); }

	long ms() const
	{
		return std::chrono::duration_cast<std::chrono::milliseconds>(
		            std::chrono::steady_clock::now() - mStart ).count();
	}

	/// Elapsed time in ms, then restart. Handy to time consecutive phases.
	long lap()
	{
		const long elapsed = ms();
		restart();
		return elapsed;
	}

private:
	std::chrono::steady_clock::time_point mStart;
};

} // namespace RsGxsProfiler

/**
 * Report one profiling line when enabled and the measured duration reaches the
 * threshold. Usage:
 *
 *   RS_GXS_PROF(total_ms, "retrieveNxsMsgs msgs=" << count << " in " << total_ms << "ms");
 */
#define RS_GXS_PROF(duration_ms, ...) do { \
		if( RsGxsProfiler::enabled() && \
		    (duration_ms) >= RsGxsProfiler::thresholdMs() ) \
			RsDbg() << "GXS-PROF " << __VA_ARGS__; \
	} while(false)
