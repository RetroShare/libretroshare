/*******************************************************************************
 * RetroShare Broadcast Domain Discovery                                       *
 *                                                                             *
 * Copyright (C) 2019-2022  Gioacchino Mazzurco <gio@altermundi.net>           *
 * Copyright (C) 2019-2022  Asociación Civil Altermundi <info@altermundi.net>  *
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

#include <cstdint>
#include <map>
#include <iostream>
#include <forward_list>
#include <system_error>

#include <udp_discovery_peer.hpp>

#include "retroshare/rsbroadcastdiscovery.h"
#include "util/rsmemory.h"
#include "util/rsthreads.h"
#include "util/rsdebug.h"

#ifdef __ANDROID__
#	include <jni/jni.hpp>
#	include "rs_android/rsjni.hpp"
#endif // def __ANDROID__


namespace UDC = udpdiscovery;
class RsPeers;

class BroadcastDiscoveryService :
        public RsBroadcastDiscovery, public RsTickingThread
{
public:
	explicit BroadcastDiscoveryService(RsPeers& pRsPeers);
	~BroadcastDiscoveryService() override;

	/// @see RsBroadcastDiscovery
	std::vector<RsBroadcastDiscoveryResult> getDiscoveredPeers() override;

	/// @see RsBroadcastDiscovery
	bool isMulticastListeningEnabled() override;

	/// @see RsBroadcastDiscovery
	bool enableMulticastListening() override;

	/// @see RsBroadcastDiscovery
	bool disableMulticastListening() override;

	void threadTick() override; /// @see RsTickingThread

protected:
	constexpr static uint16_t port = 36405;
	constexpr static uint32_t appId = 904571;

	void updatePublishedData();

	UDC::PeerParameters mUdcParameters;
	UDC::Peer mUdcPeer;

	std::map<UDC::IpPort, std::string> mDiscoveredData;
	RsMutex mDiscoveredDataMutex;

	RsPeers& mRsPeers;

	/**
	 * @brief Create result object from data
	 * @param[in] ipp peer IP and port
	 * @param[in] uData serialized data associated to the peer
	 * @param[out] ec Optional storage for eventual error code,
	 *	meaningful only on failure, if a nullptr is passed ther error is treated
	 *	as fatal downstream, otherwise it bubble up to be treated upstream
	 * @return nullptr on failure, pointer to the generated result otherwise
	 */
	static std::unique_ptr<RsBroadcastDiscoveryResult> createResult(
	        const UDC::IpPort& ipp, const std::string& uData,
	        rs_view_ptr<std::error_condition> ec = nullptr );

#ifdef __ANDROID__
	struct AndroidMulticastLock
	{
		static constexpr auto Name()
		{ return "android/net/wifi/WifiManager$MulticastLock"; }
	};

	jni::Global<jni::Object<AndroidMulticastLock>> mAndroidWifiMulticastLock;

	/** Initialize the wifi multicast lock without acquiring it
	 * Needed to enable multicast listening in Android, for RetroShare broadcast
	 * discovery inspired by:
	 * https://github.com/flutter/flutter/issues/16335#issuecomment-420547860
	 */
	bool createAndroidMulticastLock();
#endif

	RS_SET_CONTEXT_DEBUG_LEVEL(3)
};
