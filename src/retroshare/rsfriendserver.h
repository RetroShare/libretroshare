/*******************************************************************************
 * libretroshare/src/retroshare: rsfriendserver.h                              *
 *                                                                             *
 * libretroshare: retroshare core library                                      *
 *                                                                             *
 * Copyright 2022 by Retroshare Team <contact@retroshare.cc>                   *
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

#include <functional>
#include <thread>
#include <map>
#include "retroshare/rspeers.h"
#include "util/rstime.h"

// The Friend Server component of Retroshare automatically adds/removes some friends so that the
//
// The current strategy is:
//
//   - if total nb of friends < S
//         request new friends to the FS
//   - if total nb of friends >= S
//         do not request anymore (and unpublish the key), but keep the friends already here
//
// Possible states:
//   - not started
//   - maintain friend list
//   - actively request friends
//
// The friend server internally keeps track of which friends have been added using the friend server.
// It's important to keep the ones that are already connected because they may count on us.
// Friends supplied by the FS who never connected for a few days should be removed automatically.

enum class RsFriendServerStatus: uint8_t
{
    UNKNOWN              = 0x00,
    OFFLINE              = 0x01,
    ONLINE               = 0x02,
};

enum class RsFriendServerEventCode: uint8_t
{
    UNKNOWN                      = 0x00,
    PEER_INFO_CHANGED            = 0x01,
    FRIEND_SERVER_STATUS_CHANGED = 0x02,
};

struct RsFriendServerEvent: public RsEvent
{
    RsFriendServerEvent(): RsEvent(RsEventType::FRIEND_SERVER), mFriendServerEventType(RsFriendServerEventCode::UNKNOWN) {}
    ~RsFriendServerEvent() = default;

    RsFriendServerEventCode mFriendServerEventType;
    RsFriendServerStatus mFriendServerStatus;

    void serial_process( RsGenericSerializer::SerializeJob j, RsGenericSerializer::SerializeContext& ctx ) override
    {
        RsEvent::serial_process(j, ctx);

        RS_SERIAL_PROCESS(mFriendServerEventType);
        RS_SERIAL_PROCESS(mFriendServerStatus);
    }
};

class RsFriendServer
{
public:
    enum class PeerFriendshipLevel {
        UNKNOWN          =  0x00,
        NO_KEY           =  0x01,
        HAS_KEY          =  0x02,
        HAS_ACCEPTED_KEY =  0x03,
    };

    // Data structure to communicate internal states of the FS to the UI client.

    struct RsFsPeerInfo
    {
        RsFsPeerInfo(): mPeerLevel(PeerFriendshipLevel::UNKNOWN),mOwnLevel(PeerFriendshipLevel::UNKNOWN) {}
        RsFsPeerInfo(const std::string& invite,PeerFriendshipLevel peer_level,PeerFriendshipLevel own_level)
            : mInvite(invite),mPeerLevel(peer_level),mOwnLevel(own_level) {}

        std::string mInvite;
        PeerFriendshipLevel mPeerLevel ;
        PeerFriendshipLevel mOwnLevel ;
    };

    virtual void startServer() =0;
    virtual void stopServer() =0;

    // Testing system. Since the test can take some time (contacting the proxy, setting the connection,
    // getting some ack from the server). The whole test is synchronous and might be blocking for a while.
    // Consequently, the client needs to take care to avoid blocking e.g. the UI when calling this.
    //
    virtual bool checkServerAddress(const std::string& addr,uint16_t port, uint32_t timeout_ms) =0;

    virtual void setServerAddress(const std::string&,uint16_t) =0;
    virtual void setFriendsToRequest(uint32_t) =0;

    virtual bool autoAddFriends() const =0;
    virtual void setAutoAddFriends(bool b) =0;
    /*!
     * \brief setProfilePassphrase
     * 		Needs to be called as least once, and before the friend server is enabled, so as to be able to decrypt incoming information
     * 		sent by the server. If not available, the passphrase will be asked by the GUI, which may cause some annoying
     *      side effects.
     */
    virtual void setProfilePassphrase(const std::string& passphrase) =0;

    virtual uint32_t friendsToRequest() =0;
    virtual uint16_t friendsServerPort() =0;
    virtual std::string friendsServerAddress() =0;

    /*!
     * \brief allowPeer
     * 			Allows the friend server to make the given peer as friend.
     */
    virtual void allowPeer(const RsPeerId& pid) =0;

    virtual std::map<RsPeerId,RsFsPeerInfo> getPeersInfo() =0 ;
};

extern RsFriendServer *rsFriendServer;
