/*******************************************************************************
 * libretroshare/src/pqi: pqithreadstreamer.cc                                 *
 *                                                                             *
 * libretroshare: retroshare core library                                      *
 *                                                                             *
 * Copyright 2004-2013 by Robert Fernie <retroshare@lunamutt.com>              *
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
#include "util/rstime.h"
#include "pqi/pqithreadstreamer.h"
#include <unistd.h>

// for timeBeginPeriod
#ifdef WINDOWS_SYS
#include <windows.h>
#include <mmsystem.h>
#endif

#define STREAMER_TIMEOUT_MIN                  0 //  non blocking
#define STREAMER_TIMEOUT_DELTA		   1000 //  1 ms
#define STREAMER_TIMEOUT_MAX              10000 // 10 ms

#define STREAMER_SLEEP_MIN		   1000 //  1 ms
#define STREAMER_SLEEP_DELTA               1000 //  1 ms
#define STREAMER_SLEEP_MAX                30000 // 30 ms

#define DEFAULT_STREAMER_IDLE_SLEEP	1000000 //  1 sec

// #define PQISTREAMER_DEBUG

pqithreadstreamer::pqithreadstreamer(PQInterface *parent, RsSerialiser *rss, const RsPeerId& id, BinInterface *bio_in, int bio_flags_in)
:pqistreamer(rss, id, bio_in, bio_flags_in), mParent(parent), mTimeout(0), mThreadMutex("pqithreadstreamer")
{
#ifdef WINDOWS_SYS
        // On Windows, the default system timer resolution is around 15 ms.
        // This call allows for sleep durations of less than 15 ms, which is
        // necessary for frequent polling and high-speed data transfer.
        timeBeginPeriod(1);
#endif
	mTimeout = STREAMER_TIMEOUT_MAX;
	mSleepPeriod = STREAMER_SLEEP_MAX;
}

bool pqithreadstreamer::RecvItem(RsItem *item)
{
	return mParent->RecvItem(item);
}

int	pqithreadstreamer::tick()
{
	// pqithreadstreamer mutex lock is not needed here
	// we only check if the connection is active, and if not we will try to establish it
	tick_bio();

	return 0;
}

void pqithreadstreamer::threadTick()
{
        static uint32_t recv_timeout = mTimeout;
        static uint32_t sleep_period = mSleepPeriod;
        static uint32_t readbytes = 0;
        static uint32_t sentbytes = 0;
	bool isactive = false;

	// Locked section to safely read shared variables
	{
		RsStackMutex stack(mStreamerMtx);
		isactive = mBio->isactive();
	}
    
	if (!isactive)
	{
		rstime::rs_usleep(DEFAULT_STREAMER_IDLE_SLEEP);
		return ;
	}

	updateRates();

	// Adaptive timeout and sleep
	// Check if any data was processed during previous cycle.
	if (readbytes > 0 || sentbytes > 0) 
	{
		// Activity detected: Switch to maximum reactivity immediately.
		// This prevents the thread from blocking in the next receive call,
		// ensuring fast throughput for data bursts.
		recv_timeout = STREAMER_TIMEOUT_MIN;
		sleep_period = STREAMER_SLEEP_MIN; 
	} 
	else 
	{
		// No activity: Gradually increase the timeout and sleep to save CPU cycles.
		if (recv_timeout < STREAMER_TIMEOUT_MAX) 
		        recv_timeout += STREAMER_TIMEOUT_DELTA;
		if (sleep_period < STREAMER_SLEEP_MAX)
			sleep_period += STREAMER_SLEEP_DELTA;
	}

	{
		RsStackMutex stack(mThreadMutex);
		readbytes = tick_recv(recv_timeout);
	}

	// Process incoming items, move them to appropriate service queue or shortcut to fast service
	RsItem *incoming = NULL;
	while((incoming = GetItem()))
	{
		RecvItem(incoming);
	}

	// Parse outgoing queue and send items
	{
		RsStackMutex stack(mThreadMutex);
		sentbytes = tick_send(0);
	}

	// RsDbg() << "PQISTREAMER pqithreadstreamer::threadTick() recv_timeout " << std::dec << recv_timeout / 1000 << " sleep_period " <<  sleep_period / 1000 << " readbytes " << readbytes << "  sentbytes " << sentbytes;

	if (sleep_period > 0)
	{
		rstime::rs_usleep(sleep_period);
	}
}
