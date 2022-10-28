/*******************************************************************************
 * libretroshare/src/file_sharing: fsclient.h                                  *
 *                                                                             *
 * libretroshare: retroshare core library                                      *
 *                                                                             *
 * Copyright 2021 by retroshare team <contact@retroshare.cc>            *
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
 ******************************************************************************/

#include <string>
#include <map>
#include "fsitem.h"
#include "pqi/pqi_base.h"

// This class runs a client connection to the friend server. It opens a socket at each connection.

class FsClient: public PQInterface
{
public:
    FsClient() :PQInterface(RsPeerId()) {}

    enum class FsClientErrorCode: uint8_t {
        FS_CLIENT_NO_ERROR  = 0x00,
        NO_CONNECTION       = 0x01,
        UNKNOWN_ERROR       = 0x02,
    };

    /*!
     * \brief requestFriends
     * \param address
     * \param port
     * \param proxy_address
     * \param proxy_port
     * \param reqs
     * \param pgp_passphrase
     * \param already_received_peers
     * \param friend_certificates
     * \param error_code
     * \return
     */
    bool requestFriends(const std::string& address, uint16_t port,
                        const std::string &proxy_address, uint16_t proxy_port,
                        uint32_t reqs,
                        const std::string &pgp_passphrase,
                        const std::map<RsPeerId,RsFriendServer::PeerFriendshipLevel>& already_received_peers,
                        std::map<RsPeerId, std::pair<std::string, RsFriendServer::PeerFriendshipLevel> > &friend_certificates, FsClientErrorCode &error_code);

    static bool checkProxyConnection(const std::string& onion_address, uint16_t port, const std::string &proxy_address, uint16_t proxy_port, uint32_t timeout_ms);
protected:
    // Implements PQInterface

    bool RecvItem(RsItem *item) override;
    int  SendItem(RsItem *) override { RsErr() << "FsClient::SendItem() called although it should not." ; return 0;}
    RsItem *GetItem() override;

private:
    bool sendItem(const std::string &server_address, uint16_t server_port,
                  const std::string &proxy_address, uint16_t proxy_port,
                  RsFriendServerItem *item, std::list<RsItem *> &response);

    void handleServerResponse(RsFriendServerServerResponseItem *item, std::map<RsPeerId, std::pair<std::string, RsFriendServer::PeerFriendshipLevel> > &friend_certificates);

    std::list<RsItem*> mIncomingItems;
};

