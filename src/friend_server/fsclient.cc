/*******************************************************************************
 * libretroshare/src/file_sharing: fsclient.cc                                 *
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

#ifdef WINDOWS_SYS
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <netinet/tcp.h>
#endif

#include "pqi/pqithreadstreamer.h"
#include "retroshare/rspeers.h"
#include "retroshare/rsnotify.h"

#include "fsclient.h"
#include "pqi/authgpg.h"
#include "pqi/pqifdbin.h"
#include "pqi/pqiproxy.h"

#define DEBUG_FSCLIENT

bool FsClient::requestFriends(const std::string& address, uint16_t port,
                              const std::string& proxy_address, uint16_t proxy_port,
                              uint32_t reqs,
                              const std::string& pgp_passphrase,
                              const std::map<RsPeerId,RsFriendServer::PeerFriendshipLevel>& already_received_peers,
                              std::map<RsPeerId,std::pair<std::string, RsFriendServer::PeerFriendshipLevel> >& friend_certificates,
                              FsClientErrorCode& error_code)
{
    // Send our own certificate to publish and expects response from the server, decrypts it and returns friend list

    error_code = FsClientErrorCode::FS_CLIENT_NO_ERROR;
    RsFriendServerClientPublishItem *pitem = new RsFriendServerClientPublishItem();

    pitem->n_requested_friends = reqs;
    pitem->already_received_peers = already_received_peers;

    std::string pgp_base64_string,pgp_base64_checksum,short_invite;
    rsPeers->GetPGPBase64StringAndCheckSum(rsPeers->getGPGOwnId(),pgp_base64_string,pgp_base64_checksum);

    if(!rsPeers->getShortInvite(short_invite,RsPeerId(),RetroshareInviteFlags::RADIX_FORMAT | RetroshareInviteFlags::DNS))
    {
        RsErr() << "Cannot request own short invite! Something's very wrong." ;
        error_code = FsClientErrorCode::UNKNOWN_ERROR;
        return false;
    }

    pitem->pgp_public_key_b64 = pgp_base64_string;
    pitem->short_invite = short_invite;

    std::list<RsItem*> response;

    if(!sendItem(address,port,proxy_address,proxy_port,pitem,response))
    {
        error_code = FsClientErrorCode::NO_CONNECTION;
        return false;
    }

    // now decode the response

    friend_certificates.clear();

    for(auto item:response)
    {
        auto *encrypted_response_item = dynamic_cast<RsFriendServerEncryptedServerResponseItem*>(item);

        if(!encrypted_response_item)
        {
            RsErr() << "Received a response from the server that is not encrypted. Dropping that data." ;
            delete item;
            continue;
        }

        uint32_t decrypted_data_size = encrypted_response_item->bin_len;
        RsTemporaryMemory decrypted_data(decrypted_data_size);

        rsNotify->cachePgpPassphrase(pgp_passphrase) ;
        rsNotify->setDisableAskPassword(true);

        bool decrypted = AuthPGP::decryptDataBin(encrypted_response_item->bin_data,encrypted_response_item->bin_len,  decrypted_data,&decrypted_data_size);

        rsNotify->setDisableAskPassword(false);
        rsNotify->clearPgpPassphrase();

        if(!decrypted)
        {
            RsErr() << "Cannot decrypt incoming server response item. This is rather unexpected. Droping the data.";
            delete item;
            continue;
        }
        if(decrypted_data_size == 0)
        {
            RsErr() << "Decrypted incoming data is of length 0. This is rather unexpected. Droping the data.";
            delete item;
            continue;
        }

        auto decrypted_item = FsSerializer().deserialise(decrypted_data,&decrypted_data_size);

        // Check that the item has the correct type.

        auto *response_item = dynamic_cast<RsFriendServerServerResponseItem*>(decrypted_item);

        if(!response_item)
        {
            RsErr() << "Decrypted server response item is not a RsFriendServerResponse item. Somethings's wrong is going on.";
            delete item;
            continue;
        }

        RsDbg() << "Response item contains:" << *response_item;

        handleServerResponse(response_item,friend_certificates);

        delete item;
    }
    return true;
}

void FsClient::handleServerResponse(RsFriendServerServerResponseItem *item,std::map<RsPeerId,std::pair<std::string,RsFriendServer::PeerFriendshipLevel> >& friend_certificates)
{
    RsDbg() << "Received a response item from server: " << (void*)item ;

    for(const auto& it:item->friend_invites)
    {
        RsPeerDetails det;
        uint32_t err_code;

        if(!rsPeers->parseShortInvite(it.first,det,err_code))
        {
            RsErr() << "Friend server sent a buggy short invite: " << it.first << ". Err_code=" << err_code << ". Skipping it." ;
            continue;
        }
        RsDbg() << "  New certificate: " << det.id ;
        friend_certificates[det.id] = it;
    }
}

bool FsClient::sendItem(const std::string& server_address,uint16_t server_port,
                        const std::string& proxy_address,uint16_t proxy_port,
                        RsFriendServerItem *item,std::list<RsItem*>& response)
{
    // open a connection

    RsDbg() << "Sending item to friend server at \"" << server_address << ":" << server_port << " through proxy " << proxy_address << ":" << proxy_port;

    int CreateSocket = 0;
    char dataReceived[1024];
    struct sockaddr_in ipOfServer;

    memset(dataReceived, '0' ,sizeof(dataReceived));

    if((CreateSocket = socket(AF_INET, SOCK_STREAM, 0))< 0)
    {
        printf("Socket not created \n");
        return false;
    }

    int flags=1;
    setsockopt(CreateSocket,SOL_SOCKET,TCP_NODELAY,(char*)&flags,sizeof(flags));

    ipOfServer.sin_family = AF_INET;
    ipOfServer.sin_port = htons(proxy_port);
    ipOfServer.sin_addr.s_addr = inet_addr(proxy_address.c_str());

    if(connect(CreateSocket, (struct sockaddr *)&ipOfServer, sizeof(ipOfServer))<0)
    {
        printf("Connection to proxy failed due to port and ip problems, or proxy is not available\n");
        return false;
    }

    // Now connect to the proxy

    int ret=0;
    pqiproxyconnection proxy;
    proxy.setRemoteAddress(server_address);
    proxy.setRemotePort(server_port);

    while(1 != (ret = proxy.proxy_negociate_connection(CreateSocket)))
        if(ret < 0)
        {
            RsErr() << "FriendServer client: Connection problem to the proxy!" ;
            return false;
        }
        else
            std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // Serialise the item and send it.

    FsSerializer *fss = new FsSerializer;
    RsSerialiser *rss = new RsSerialiser();	// deleted by ~pqistreamer()
    rss->addSerialType(fss);

    RsFdBinInterface *bio = new RsFdBinInterface(CreateSocket,true);	// deleted by ~pqistreamer()

    pqithreadstreamer p(this,rss,RsPeerId(),bio,BIN_FLAGS_READABLE | BIN_FLAGS_WRITEABLE | BIN_FLAGS_NO_CLOSE);
    p.start();

    RsDbg() << "Sending item. size=" << fss->size(item) << ". Waiting for response..." ;

    // Now attempt to read and deserialize anything that comes back from that connexion until it gets closed by the server.

    uint32_t ss;
    p.SendItem(item,ss);
    time_t now = time(nullptr);
    bool got_response = false;

#ifdef DEBUG_FSCLIENT
    RsDbg() << "Ticking for response...";
#endif
    while(now + 15 > time(nullptr)) // wait 15 secs for a response.
    {
        p.tick(); // ticks bio

        RsItem *ritem = GetItem();

        if(ritem)
        {
            response.push_back(ritem);
            std::cerr << "Got a response item: " << std::endl;
            std::cerr << *ritem << std::endl;

            RsDbg() << "End of transmission. " ;
            got_response = true;
            break;
        }
        else
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    if(!got_response)
        std::cerr << "Sending timed out. Connection is dead?" << std::endl;

    RsDbg() << "  Stopping/killing pqistreamer" ;
    p.fullstop();

    RsDbg() << "  Closing socket." ;
    close(CreateSocket);
    CreateSocket=0;

    RsDbg() << "  Exiting loop." ;

    return true;
}

bool FsClient::checkProxyConnection(const std::string& onion_address,uint16_t port,const std::string& proxy_address,uint16_t proxy_port,uint32_t timeout_ms)
{
    int CreateSocket = 0;
    struct sockaddr_in ipOfServer;

    // Early check for server port and address syntax

    if(onion_address.length() != 62 || strcmp(onion_address.substr(56).c_str(),".onion"))
    {
        RsErr() << "Inconsistent onion address for friend server \"" << onion_address << "\"";
        return false;
    }
    if(port < 1025)
    {
        RsErr() << "Inconsistent (private) port " << port << " for friend server";
        return false;
    }

    if((CreateSocket = socket(AF_INET, SOCK_STREAM, 0))< 0)
    {
        RsErr() << "Socket not created";
        return false;
    }
    int flags=1;
    setsockopt(CreateSocket,SOL_SOCKET,TCP_NODELAY,(char*)&flags,sizeof(flags));

    ipOfServer.sin_family = AF_INET;
    ipOfServer.sin_port = htons(proxy_port);
    ipOfServer.sin_addr.s_addr = inet_addr(proxy_address.c_str());

    if(connect(CreateSocket, (struct sockaddr *)&ipOfServer, sizeof(ipOfServer))<0)
    {
        RsErr() << "Connection to proxy failed due to port and ip problems, or proxy is not available\n";
        return false;
    }

    int ret=0;
    pqiproxyconnection proxy;
    proxy.setRemoteAddress(onion_address);
    proxy.setRemotePort(port);

    // now try for 5 secs

    for(uint32_t i=0;i<timeout_ms/100;++i)
        if(1 == (ret = proxy.proxy_negociate_connection(CreateSocket)))
            return true;
        else if(ret < 0)
        {
            RsErr() << "FriendServer client: Connection problem to the proxy!" ;
            return false;
        }
        else
            std::this_thread::sleep_for(std::chrono::milliseconds(100));

    return false;
}

bool FsClient::RecvItem(RsItem *item)
{
    mIncomingItems.push_back(item);
    return true;
}

RsItem *FsClient::GetItem()
{
    if(mIncomingItems.empty())
        return nullptr;

    RsItem *item = mIncomingItems.front();
    mIncomingItems.pop_front();

    return item;
}
