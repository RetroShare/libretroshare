/*******************************************************************************
 * libretroshare/src/rsserver: p3face-config.cc                                *
 *                                                                             *
 * libretroshare: retroshare core library                                      *
 *                                                                             *
 * Copyright 2004-2006 by Robert Fernie.                                       *
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

#include "rsserver/p3face.h"

#include <iostream>
#include "pqi/authssl.h"
#include "pqi/authgpg.h"
#include "retroshare/rsinit.h"
#include "plugins/pluginmanager.h"
#include "util/rsdebug.h"

#ifdef RS_JSONAPI
#	include "jsonapi/jsonapi.h"
#endif // ifdef RS_JSONAPI

#include <sys/time.h>
#include "util/rstime.h"

#include "pqi/p3peermgr.h"
#include "pqi/p3netmgr.h"


// TO SHUTDOWN THREADS.
#ifdef RS_ENABLE_GXS

#include "services/autoproxy/rsautoproxymonitor.h"

#include "services/p3idservice.h"
#include "services/p3gxscircles.h"
#include "services/p3wiki.h"
#include "services/p3posted.h"
#include "services/p3photoservice.h"
#include "services/p3gxsforums.h"
#include "services/p3gxschannels.h"
#include "services/p3wire.h"

#endif

/****************************************/
/* RsIface Config */
/* Config */

void RsServer::ConfigFinalSave()
{
	//TODO: force saving of transfers
	//ftserver->saveFileTransferStatus();

#ifdef RS_AUTOLOGIN
	if(!RsInit::getAutoLogin()) RsInit::RsClearAutoLogin();
#endif // RS_AUTOLOGIN

	//AuthSSL::getAuthSSL()->FinalSaveCertificates();
	mConfigMgr->completeConfiguration();
}

void RsServer::startServiceThread(RsTickingThread *t, const std::string &threadName)
{
    t->start(threadName) ;
    mRegisteredServiceThreads.push_back(t) ;
}

void RsServer::rsGlobalShutDown()
{
	bool wasReady = coreReady;
	coreReady = false;

	if(wasReady)
	{
		/* Close the incoming-connection listener FIRST, before anything else.
		 * The steps below (config save, plugin stop, UPnP teardown and above all
		 * the auto-proxy shutdown) can take >20s, during which the RsServer tick
		 * thread is still alive and keeps ticking the listener. Left open, it goes
		 * on accepting TCP connections and completing full SSL+PGP handshakes right
		 * up to the last second before the databases close -- creating per-peer
		 * state (sockets, streamer threads) that races the teardown of the static
		 * SmallObject allocator. Closing the accept socket now admits no new peer
		 * for the whole shutdown; fullstopAllThreads() below then drains the
		 * already-connected ones. */
		if(pqih) pqih->stopListener();

		// save configuration before exit
		ConfigFinalSave();

		mPluginsManager->stopPlugins(pqih);

		/* Handles UPnP */
		mNetMgr->shutdown();

		rsAutoProxyMonitor::instance()->stopAllRSShutdown();

		// kill all registered service threads
		for(RsTickingThread* service: mRegisteredServiceThreads)
			service->fullstop();
	}

	fullstop();

	/* Stop the per-peer network I/O threads (pqithreadstreamer). They are
	 * otherwise never stopped at shutdown and keep deserialising incoming
	 * packets, which crashes/floods the log once the static SmallObject
	 * allocator and its mutex are destroyed during process teardown.
	 * Must run after fullstop() so the RsServer tick thread is no longer
	 * iterating the peer list concurrently. */
	if(pqih) pqih->fullstopAllThreads();

#ifdef RS_JSONAPI
	if(rsJsonApi) rsJsonApi->fullstop();
#endif

	AuthPGP::exit();

    // close all databases

    for(auto db:mRegisteredDataServices)
        delete db;

	mShutdownCallback(0);
}
