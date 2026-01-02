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

#ifdef WINDOWS_SYS
#include <windows.h>
#include <mmsystem.h>
#endif

#define DEFAULT_STREAMER_TIMEOUT	  5000 // 10 ms
#define DEFAULT_STREAMER_SLEEP		  30000 // 30 ms
#define DEFAULT_STREAMER_IDLE_SLEEP	1000000 // 1 sec

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
	mTimeout = DEFAULT_STREAMER_TIMEOUT;
	mSleepPeriod = DEFAULT_STREAMER_SLEEP;
}

bool pqithreadstreamer::RecvItem(RsItem *item)
{
	return mParent->RecvItem(item);
}

int	pqithreadstreamer::tick()
{
	// pqithreadstreamer mutex lock is not needed here
	// we will only check if the connection is active, and if not we will try to establish it
	tick_bio();

	return 0;
}

void pqithreadstreamer::threadTick()
{
	uint32_t recv_timeout = 0;
	uint32_t sleep_period = 0;
	bool isactive = false;
	bool has_outgoing = false;

	// Locked section to safely read shared variables
	{
		RsStackMutex stack(mStreamerMtx);
		recv_timeout = mTimeout;
		sleep_period = mSleepPeriod;
		isactive = mBio->isactive();
		// Check if there are packets waiting in the outgoing queue
		has_outgoing = !mOutPkts.empty();
	}
    
	if (!isactive)
	{
		rstime::rs_usleep(DEFAULT_STREAMER_IDLE_SLEEP);
		return ;
	}

	updateRates();

	// ADAPTIVE TIMEOUT:
	// If we have data to send, we force the receive timeout to 0.
	// This ensures tick_recv returns immediately if no data is present,
	// allowing tick_send to be called without waiting for the 10ms recv timeout.
	uint32_t adaptive_timeout = has_outgoing ? 0 : recv_timeout;

	// Fill incoming queue
	int readbytes = 0;
	{
		RsStackMutex stack(mThreadMutex);
		readbytes = tick_recv(adaptive_timeout);
	}

	bool activity = false;

	// Process incoming items, move them to appropriate service queue or shortcut to fast service
	RsItem *incoming = NULL;
	while((incoming = GetItem()))
	{
		activity = true; // Activity detected (Download)
		RecvItem(incoming);
	}

	// Parse outgoing queue and send items
	int sentbytes = 0;
	{
		RsStackMutex stack(mThreadMutex);
		sentbytes = tick_send(0);
	}

	// ADAPTIVE SLEEP:
	// If data was moved in either direction, reset sleep to 1ms for max performance.
	// Otherwise, gradually increase sleep up to 30ms to save CPU resources.
	{
		RsStackMutex stack(mStreamerMtx);
		if (readbytes > 0 || sentbytes > 0)
		{
			mSleepPeriod = 1000;
		}
		else
		{
			// Increment sleep period up to the 30ms limit
			if (mSleepPeriod < 30000)
			{
				mSleepPeriod += 1000;
			}
		}
		sleep_period = mSleepPeriod;
	}

	if (readbytes > 0 || sentbytes > 0 || adaptive_timeout == 0 || sleep_period == 30000)
		RsDbg() << "PQISTREAMER pqithreadstreamer::threadTick() readbytes " << std::dec << readbytes << " sentbytes " << sentbytes << " adaptive_timeout " << adaptive_timeout / 1000 << " sleep_period " <<  sleep_period / 1000;

	if (sleep_period > 0)
	{
		rstime::rs_usleep(sleep_period);
	}
}
