/*******************************************************************************
 * libretroshare/src/chat: chatservice.cc                                      *
 *                                                                             *
 * libretroshare: retroshare core library                                      *
 *                                                                             *
 * Copyright 2004-2008 by Robert Fernie.                                       *
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

#include <math.h>
#include <sstream>
#include <unistd.h>
#include <iomanip>
#include <algorithm>

#include "util/rsdir.h"
#include "util/radix64.h"
#include "crypto/rsaes.h"
#include "util/rsrandom.h"
#include "util/rsstring.h"
#include "retroshare/rsiface.h"
#include "retroshare/rspeers.h"
#include "retroshare/rsstatus.h"
#include "pqi/pqibin.h"
#include "pqi/pqistore.h"
#include "pqi/p3linkmgr.h"
#include "pqi/p3historymgr.h"
#include "rsserver/p3face.h"
#include "services/p3idservice.h"
#include "gxstrans/p3gxstrans.h"

#include "chat/p3chatservice.h"
#include "rsitems/rsconfigitems.h"

//#define CHAT_DEBUG 1

RsChats *rsChats = nullptr;

static const uint32_t MAX_MESSAGE_SECURITY_SIZE         = 31000 ; // Max message size to forward other friends
static const uint32_t MAX_AVATAR_JPEG_SIZE              = 32767; // Maximum size in bytes for an avatar. Too large packets 
                                                                 // don't transfer correctly and can kill the system.
ChatId::ChatId():
    type(TYPE_NOT_SET),
    lobby_id(0)
{

}

ChatId::ChatId(RsPeerId id):
    lobby_id(0)
{
    type = TYPE_PRIVATE;
    peer_id = id;
}

ChatId::ChatId(DistantChatPeerId id):
    lobby_id(0)
{
    type = TYPE_PRIVATE_DISTANT;
    distant_chat_id = id;
}

ChatId::ChatId(ChatLobbyId id):
    lobby_id(0)
{
    type = TYPE_LOBBY;
    lobby_id = id;
}

ChatId::ChatId(std::string str) : lobby_id(0)
{
    type = TYPE_NOT_SET;
    if(str.empty()) return;

    if(str[0] == 'P')
    {
        type = TYPE_PRIVATE;
        peer_id = RsPeerId(str.substr(1));
    }
    else if(str[0] == 'D')
    {
        type = TYPE_PRIVATE_DISTANT;
        distant_chat_id = DistantChatPeerId(str.substr(1));
    }
    else if(str[0] == 'L')
    {
        if(sizeof(ChatLobbyId) != 8)
        {
            std::cerr << "ChatId::ChatId(std::string) Error: sizeof(ChatLobbyId) != 8. please report this" << std::endl;
            return;
        }
        str = str.substr(1);
        if(str.size() != 16)
            return;
        ChatLobbyId id = 0;
        for(int i = 0; i<16; i++)
        {
            uint8_t c = str[i];
            if(c <= '9')
                c -= '0';
            else
                c -= 'A' - 10;
            id = id << 4;
            id |= c;
        }
        type = TYPE_LOBBY;
        lobby_id = id;
    }
    else if(str[0] == 'B')
    {
        type = TYPE_BROADCAST;
    }
}

ChatId ChatId::makeBroadcastId()
{
    ChatId id;
    id.type = TYPE_BROADCAST;
    return id;
}

std::string ChatId::toStdString() const
{
    std::string str;
    if(type == TYPE_PRIVATE)
    {
        str += "P";
        str += peer_id.toStdString();
    }
    else if(type == TYPE_PRIVATE_DISTANT)
    {
        str += "D";
        str += distant_chat_id.toStdString();
    }
    else if(type == TYPE_LOBBY)
    {
        if(sizeof(ChatLobbyId) != 8)
        {
            std::cerr << "ChatId::toStdString() Error: sizeof(ChatLobbyId) != 8. please report this" << std::endl;
            return "";
        }
        str += "L";

        ChatLobbyId id = lobby_id;
        for(int i = 0; i<16; i++)
        {
            uint8_t c = id >>(64-4);
            if(c > 9)
                c += 'A' - 10;
            else
                c += '0';
            str += c;
            id = id << 4;
        }
    }
    else if(type == TYPE_BROADCAST)
    {
        str += "B";
    }
    return str;
}

bool ChatId::operator <(const ChatId& other) const
{
    if(type != other.type)
        return type < other.type;
    else
    {
        switch(type)
        {
        case TYPE_NOT_SET:
            return false;
        case TYPE_PRIVATE:
            return peer_id < other.peer_id;
        case TYPE_PRIVATE_DISTANT:
            return distant_chat_id < other.distant_chat_id;
        case TYPE_LOBBY:
            return lobby_id < other.lobby_id;
        case TYPE_BROADCAST:
            return false;
        default:
            return false;
        }
    }
}

bool ChatId::isSameEndpoint(const ChatId &other) const
{
    if(type != other.type)
        return false;
    else
    {
        switch(type)
        {
        case TYPE_NOT_SET:
            return false;
        case TYPE_PRIVATE:
            return peer_id == other.peer_id;
        case TYPE_PRIVATE_DISTANT:
            return distant_chat_id == other.distant_chat_id;
        case TYPE_LOBBY:
            return lobby_id == other.lobby_id;
        case TYPE_BROADCAST:
            return true;
        default:
            return false;
        }
    }
}

bool ChatId::isNotSet() const
{
    return type == TYPE_NOT_SET;
}
bool ChatId::isPeerId() const
{
    return type == TYPE_PRIVATE;
}
bool ChatId::isDistantChatId()  const
{
    return type == TYPE_PRIVATE_DISTANT;
}
bool ChatId::isLobbyId() const
{
    return type == TYPE_LOBBY;
}
bool ChatId::isBroadcast() const
{
    return type == TYPE_BROADCAST;
}
RsPeerId    ChatId::toPeerId()  const
{
    if(type == TYPE_PRIVATE)
        return peer_id;
    else
    {
        std::cerr << "ChatId Warning: conversation to RsPeerId requested, but type is different. Current value=\"" << toStdString() << "\"" << std::endl;
        return RsPeerId();
    }
}

DistantChatPeerId     ChatId::toDistantChatId()   const
{
    if(type == TYPE_PRIVATE_DISTANT)
        return distant_chat_id;
    else
    {
        std::cerr << "ChatId Warning: conversation to DistantChatPeerId requested, but type is different. Current value=\"" << toStdString() << "\"" << std::endl;
        return DistantChatPeerId();
    }
}
ChatLobbyId ChatId::toLobbyId() const
{
    if(type == TYPE_LOBBY)
        return lobby_id;
    else
    {
        std::cerr << "ChatId Warning: conversation to ChatLobbyId requested, but type is different. Current value=\"" << toStdString() << "\"" << std::endl;
        return 0;
    }
}


p3ChatService::p3ChatService( p3ServiceControl *sc, p3IdService *pids,
                              p3LinkMgr *lm, p3HistoryMgr *historyMgr,
                              p3GxsTrans& gxsTransService ) :
    DistributedChatService(getServiceInfo().mServiceType, sc, historyMgr,pids),
    mChatMtx("p3ChatService"), mServiceCtrl(sc), mLinkMgr(lm),
    mHistoryMgr(historyMgr), _own_avatar(NULL),
    _serializer(new RsChatSerialiser()),
    mDGMutex("p3ChatService distant id - gxs id map mutex"),
    mGxsTransport(gxsTransService)
{
	addSerialType(_serializer);
	mGxsTransport.registerGxsTransClient( GxsTransSubServices::P3_CHAT_SERVICE,
	                                      this );
}

RsServiceInfo p3ChatService::getServiceInfo()
{ return RsServiceInfo(RS_SERVICE_TYPE_CHAT, "chat", 1, 0, 1, 0); }

int	p3ChatService::tick()
{
	if(receivedItems()) receiveChatQueue();

	DistributedChatService::flush();

	return 0;
}

/***************** Chat Stuff **********************/

void p3ChatService::sendPublicChat(const std::string &msg)
{
    /* go through all the peers */

    std::set<RsPeerId> ids;
    std::set<RsPeerId>::iterator it;
    mServiceCtrl->getPeersConnected(getServiceInfo().mServiceType, ids);

    /* add in own id -> so get reflection */
    RsPeerId ownId = mServiceCtrl->getOwnId();
    ids.insert(ownId);

#ifdef CHAT_DEBUG
    std::cerr << "p3ChatService::sendChat()";
    std::cerr << std::endl;
#endif

    for(it = ids.begin(); it != ids.end(); ++it)
    {
	    RsChatMsgItem *ci = new RsChatMsgItem();

	    ci->PeerId(*it);
	    ci->chatFlags = RS_CHAT_FLAG_PUBLIC;
	    ci->sendTime = time(NULL);
	    ci->recvTime = ci->sendTime;
	    ci->message = msg;

#ifdef CHAT_DEBUG
	    std::cerr << "p3ChatService::sendChat() Item:";
	    std::cerr << std::endl;
	    ci->print(std::cerr);
	    std::cerr << std::endl;
#endif

	    if (*it == ownId) 
	    {
		    //mHistoryMgr->addMessage(false, RsPeerId(), ownId, ci);
		    ChatMessage message;
		    initChatMessage(ci, message);
		    message.incoming = false;
		    message.online = true;

    auto ev = std::make_shared<RsChatServiceEvent>();
    ev->mEventCode = RsChatServiceEventCode::CHAT_MESSAGE_RECEIVED;
    ev->mMsg = message;
    rsEvents->postEvent(ev);
            mHistoryMgr->addMessage(message);
	    }
	    else
		    checkSizeAndSendMessage(ci);
    }
}

/* Inside p3ChatService class definition */
class p3ChatService::AvatarInfo
{
public:
    /* Fix: Initialize in the exact order of declaration (_image_size then _image_data) */
    AvatarInfo() : _image_size(0), _image_data(NULL), _peer_is_new(false), _own_is_new(false), _last_request_time(0), _timestamp(0) {}

    ~AvatarInfo() { if (_image_data) free(_image_data); }

    /* Fix: Constructor for combined TS + Radix64 loading */
    AvatarInfo(const std::string& encoded_data) : _image_size(0), _image_data(NULL)
    {
        if (encoded_data.length() > 16)
        {
            std::string ts_hex = encoded_data.substr(0, 16);
            std::string r64_data = encoded_data.substr(16);

            try {
                _timestamp = std::stoull(ts_hex, nullptr, 16);
            } catch (...) {
                _timestamp = 0;
            }

            std::vector<unsigned char> tmp = Radix64::decode(r64_data);
            if (!tmp.empty()) {
                init(tmp.data(), (int)tmp.size());
            }
        }
        else if (!encoded_data.empty())
        {
            /* Backward compatibility: if no TS prefix, just decode as Radix64 */
            std::vector<unsigned char> tmp = Radix64::decode(encoded_data);
            if (!tmp.empty()) {
                init(tmp.data(), (int)tmp.size());
            }
            /* Assign current time as timestamp for old avatars to prevent re-download */
            _timestamp = time(NULL);
        }
        else
        {
            _timestamp = 0;
        }
        _peer_is_new = false;
        _own_is_new = false;
        _last_request_time = 0;
    }

    /* Fix: Returns TS (16 hex chars) + Radix64 image data */
    std::string toRadix64() const
    {
        std::stringstream ss;
        ss << std::setfill('0') << std::setw(16) << std::hex << (uint64_t)_timestamp;
        
        std::string out;
        if (_image_data && _image_size > 0) {
            Radix64::encode(_image_data, (size_t)_image_size, out);
        }
        ss << out;
        return ss.str();
    }

    void init(const unsigned char *jpeg_data, int size)
    {
        if (_image_data) { free(_image_data); _image_data = NULL; _image_size = 0; }
        if (size > 0) {
            _image_size = size;
            _image_data = (unsigned char*)rs_malloc(size);
            memcpy(_image_data, jpeg_data, size);
            _timestamp = time(NULL);  // Update timestamp when image is set
        }
    }

    /* Order must be identical here too */
    AvatarInfo(const unsigned char *jpeg_data, int size) : _image_size(0), _image_data(NULL) 
    { 
        init(jpeg_data, size); 
        _peer_is_new = false;
        _own_is_new = false;
        _last_request_time = 0;
        _timestamp = time(NULL);
    }

    void toUnsignedChar(unsigned char *& data, uint32_t& size) const
    {
        if (_image_size == 0) { size = 0; data = NULL; return; }
        data = (unsigned char *)rs_malloc(_image_size);
        size = _image_size;
        memcpy(data, _image_data, size);
    }

    uint32_t _image_size;
    unsigned char *_image_data;
    bool _peer_is_new;
    bool _own_is_new;
    time_t _last_request_time;
    time_t _timestamp;
};

void p3ChatService::sendGroupChatStatusString(const std::string& status_string)
{
	std::set<RsPeerId> ids;
	mServiceCtrl->getPeersConnected(getServiceInfo().mServiceType, ids);

#ifdef CHAT_DEBUG
	std::cerr << "p3ChatService::sendChat(): sending group chat status string: " << status_string << std::endl ;
	std::cerr << std::endl;
#endif

	for(std::set<RsPeerId>::iterator it = ids.begin(); it != ids.end(); ++it)
	{
		RsChatStatusItem *cs = new RsChatStatusItem ;

		cs->status_string = status_string ;
		cs->flags = RS_CHAT_FLAG_PUBLIC ;

		cs->PeerId(*it);

		sendItem(cs);
	}
}

void p3ChatService::sendStatusString( const ChatId& id,
                                      const std::string& status_string )
{
	if(id.isLobbyId()) sendLobbyStatusString(id.toLobbyId(),status_string);
	else if(id.isBroadcast()) sendGroupChatStatusString(status_string);
	else if(id.isPeerId() || id.isDistantChatId())
	{
		RsPeerId vpid;
		if(id.isDistantChatId()) vpid = RsPeerId(id.toDistantChatId());
		else vpid = id.toPeerId();

		if(isOnline(vpid))
		{
			RsChatStatusItem *cs = new RsChatStatusItem;

			cs->status_string = status_string;
			cs->flags = RS_CHAT_FLAG_PRIVATE;
			cs->PeerId(vpid);

#ifdef CHAT_DEBUG
			std::cerr  << "sending chat status packet:" << std::endl;
			cs->print(std::cerr);
#endif
			sendChatItem(cs);
		}
	}
	else
	{
		std::cerr << "p3ChatService::sendStatusString() Error: chat id of this "
		          << "type is not handled, is it empty?" << std::endl;
		return;
	}
}

void p3ChatService::clearChatLobby(const ChatId& /*id */)
{
    RsWarn() << __PRETTY_FUNCTION__ << " not implemented, and shouldn't be called." ;
}

bool p3ChatService::joinVisibleChatLobby(const ChatLobbyId& lobby_id,const RsGxsId& own_id)
{
    return DistributedChatService::joinVisibleChatLobby(lobby_id,own_id);
}
bool p3ChatService::getChatLobbyInfo(const ChatLobbyId& id,ChatLobbyInfo& info)
{
    return DistributedChatService::getChatLobbyInfo(id,info);
}
void p3ChatService::getListOfNearbyChatLobbies(std::vector<VisibleChatLobbyRecord>& public_lobbies)
{
    DistributedChatService::getListOfNearbyChatLobbies(public_lobbies);
}
void p3ChatService::invitePeerToLobby(const ChatLobbyId &lobby_id, const RsPeerId &peer_id)
{
    DistributedChatService::invitePeerToLobby(lobby_id,peer_id);
}
bool p3ChatService::acceptLobbyInvite(const ChatLobbyId& id,const RsGxsId& gxs_id)
{
    return DistributedChatService::acceptLobbyInvite(id,gxs_id) ;
}
void p3ChatService::getChatLobbyList(std::list<ChatLobbyId>& lids)
{
    DistributedChatService::getChatLobbyList(lids) ;
}

bool p3ChatService::denyLobbyInvite(const ChatLobbyId &id)
{
    return DistributedChatService::denyLobbyInvite(id);
}
void p3ChatService::getPendingChatLobbyInvites(std::list<ChatLobbyInvite> &invites)
{
    DistributedChatService::getPendingChatLobbyInvites(invites);
}


void p3ChatService::unsubscribeChatLobby(const ChatLobbyId& lobby_id)
{
    DistributedChatService::unsubscribeChatLobby(lobby_id) ;
}
void p3ChatService::sendLobbyStatusPeerLeaving(const ChatLobbyId& lobby_id)
{
    DistributedChatService::sendLobbyStatusPeerLeaving(lobby_id) ;
}

bool p3ChatService::setIdentityForChatLobby(const ChatLobbyId& lobby_id,const RsGxsId& nick)
{
    return DistributedChatService::setIdentityForChatLobby(lobby_id,nick) ;
}
bool p3ChatService::getIdentityForChatLobby(const ChatLobbyId& lobby_id,RsGxsId& nick_name)
{
    return DistributedChatService::getIdentityForChatLobby(lobby_id,nick_name) ;
}
bool p3ChatService::setDefaultIdentityForChatLobby(const RsGxsId& nick)
{
    return DistributedChatService::setDefaultIdentityForChatLobby(nick) ;
}
void p3ChatService::getDefaultIdentityForChatLobby(RsGxsId& nick_name)
{
    DistributedChatService::getDefaultIdentityForChatLobby(nick_name) ;
}
void p3ChatService::setLobbyAutoSubscribe(const ChatLobbyId& lobby_id, const bool autoSubscribe)
{
    DistributedChatService::setLobbyAutoSubscribe(lobby_id, autoSubscribe);
}

bool p3ChatService::getLobbyAutoSubscribe(const ChatLobbyId& lobby_id)
{
    return DistributedChatService::getLobbyAutoSubscribe(lobby_id);
}
bool p3ChatService::setDistantChatPermissionFlags(uint32_t flags)
{
    return DistantChatService::setDistantChatPermissionFlags(flags) ;
}
uint32_t p3ChatService::getDistantChatPermissionFlags()
{
    return DistantChatService::getDistantChatPermissionFlags() ;
}
bool p3ChatService::getDistantChatStatus(const DistantChatPeerId& pid,DistantChatPeerInfo& info)
{
    return DistantChatService::getDistantChatStatus(pid,info) ;
}
bool p3ChatService::closeDistantChatConnexion(const DistantChatPeerId &pid)
{
    return DistantChatService::closeDistantChatConnexion(pid) ;
}
ChatLobbyId p3ChatService::createChatLobby(const std::string& lobby_name,const RsGxsId& lobby_identity,const std::string& lobby_topic,const std::set<RsPeerId>& invited_friends,ChatLobbyFlags privacy_type)
{
    return DistributedChatService::createChatLobby(lobby_name,lobby_identity,lobby_topic,invited_friends,privacy_type) ;
}


void p3ChatService::sendChatItem(RsChatItem *item)
{
	if(DistantChatService::handleOutgoingItem(item)) return;
#ifdef CHAT_DEBUG
	std::cerr << "p3ChatService::sendChatItem(): sending to " << item->PeerId()
	          << ": interpreted as friend peer id." << std::endl;
#endif
	sendItem(item);
}

void p3ChatService::checkSizeAndSendMessage(RsChatMsgItem *msg)
{
	// We check the message item, and possibly split it into multiple messages, if the message is too big.

	static const uint32_t MAX_STRING_SIZE = 15000 ;

#ifdef CHAT_DEBUG
	std::cerr << "Sending message: size=" << msg->message.size() << ", sha1sum=" << RsDirUtil::sha1sum((uint8_t*)msg->message.c_str(),msg->message.size()) << std::endl;
#endif

	while(msg->message.size() > MAX_STRING_SIZE)
	{
		// chop off the first 15000 wchars

		RsChatMsgItem *item = new RsChatMsgItem(*msg) ;

		item->message = item->message.substr(0,MAX_STRING_SIZE) ;
		msg->message = msg->message.substr(MAX_STRING_SIZE,msg->message.size()-MAX_STRING_SIZE) ;

		// Clear out any one time flags that should not be copied into multiple objects. This is 
		// a precaution, in case the receivign peer does not yet handle split messages transparently.
		//
		item->chatFlags &= (RS_CHAT_FLAG_PRIVATE | RS_CHAT_FLAG_PUBLIC | RS_CHAT_FLAG_LOBBY) ;

#ifdef CHAT_DEBUG
		std::cerr << "Creating slice of size " << item->message.size() << std::endl;
#endif
		// Indicate that the message is to be continued.
		//
		item->chatFlags |= RS_CHAT_FLAG_PARTIAL_MESSAGE ;
		sendChatItem(item) ;
	}
#ifdef CHAT_DEBUG
	std::cerr << "Creating slice of size " << msg->message.size() << std::endl;
#endif
	sendChatItem(msg) ;
}


bool p3ChatService::isOnline(const RsPeerId& pid)
{
	// check if the id is a tunnel id or a peer id.
	DistantChatPeerInfo dcpinfo;
    if(DistantChatService::getDistantChatStatus(DistantChatPeerId(pid),dcpinfo))
		return dcpinfo.status == RS_DISTANT_CHAT_STATUS_CAN_TALK;
	else return mServiceCtrl->isPeerConnected(getServiceInfo().mServiceType, pid);
}

bool p3ChatService::sendChat(ChatId destination, std::string msg)
{
    if(destination.isLobbyId())
        return DistributedChatService::sendLobbyChat(destination.toLobbyId(), msg);
    else if(destination.isBroadcast())
    {
        sendPublicChat(msg);
        return true;
    }
    else if(destination.isPeerId()==false && destination.isDistantChatId()==false)
    {
        std::cerr << "p3ChatService::sendChat() Error: chat id type not handled. Is it empty?" << std::endl;
        return false;
    }
    // destination is peer or distant
#ifdef CHAT_DEBUG
	std::cerr << "p3ChatService::sendChat()" << std::endl;
#endif

    RsPeerId vpid;
    if(destination.isDistantChatId())
        vpid = RsPeerId(destination.toDistantChatId()); // convert to virtual peer id
    else
        vpid = destination.toPeerId();

    RsChatMsgItem *ci = new RsChatMsgItem();
    ci->PeerId(vpid);
    ci->chatFlags = RS_CHAT_FLAG_PRIVATE;
    ci->sendTime = time(NULL);
    ci->recvTime = ci->sendTime;
    ci->message = msg;

    ChatMessage message;
    initChatMessage(ci, message);
    message.incoming = false;
    message.online = true;

	if(!isOnline(vpid)  && !destination.isDistantChatId())
	{
		message.online = false;

        auto ev = std::make_shared<RsChatServiceEvent>();
        ev->mEventCode = RsChatServiceEventCode::CHAT_MESSAGE_RECEIVED;
        ev->mMsg = message;
        rsEvents->postEvent(ev);

        // use the history to load pending messages to the gui
		// this is not very nice, because the user may think the message was send, while it is still in the queue
		mHistoryMgr->addMessage(message);

		RsGxsTransId tId = RSRandom::random_u64();

#ifdef SUSPENDED_CODE
        // this part of the code was formerly used to send the traffic over GxsTransport. The problem is that
        // gxstunnel takes care of reaching the peer already, so GxsTransport would only be needed when the
        // current peer is offline. So we need to fin a way to quickly push the items to friends when quitting RS.

		if(destination.isDistantChatId())
		{
			RS_STACK_MUTEX(mDGMutex);
			DIDEMap::const_iterator it = mDistantGxsMap.find(destination.toDistantChatId());
			if(it != mDistantGxsMap.end())
			{
				const DistantEndpoints& de(it->second);
				uint32_t sz = _serializer->size(ci);
				std::vector<uint8_t> data; data.resize(sz);
				_serializer->serialise(ci, &data[0], &sz);
				mGxsTransport.sendData(tId, GxsTransSubServices::P3_CHAT_SERVICE,
				                       de.from, de.to, &data[0], sz);
			}
			else
				std::cout << "p3ChatService::sendChat(...) can't find distant"
				          << "chat id in mDistantGxsMap this is unxpected!"
				          << std::endl;
		}
#endif

		// peer is offline, add to outgoing list
		{
			RS_STACK_MUTEX(mChatMtx);
			privateOutgoingMap.insert(outMP::value_type(tId, ci));
		}

		IndicateConfigChanged();
		return false;
	}

	{
		RS_STACK_MUTEX(mChatMtx);
		std::map<RsPeerId,AvatarInfo*>::iterator it = _avatars.find(vpid);

        if(it == _avatars.end())
        {
            _avatars[vpid] = new AvatarInfo ;
            it = _avatars.find(vpid) ;
        }
        if(it->second->_own_is_new)
        {
#ifdef CHAT_DEBUG
            std::cerr << "p3ChatService::sendChat: new avatar never sent to peer " << vpid << ". Setting <new> flag to packet." << std::endl;
#endif

            ci->chatFlags |= RS_CHAT_FLAG_AVATAR_AVAILABLE ;
            it->second->_own_is_new = false ;
        }
    }

#ifdef CHAT_DEBUG
    std::cerr << "Sending msg to (maybe virtual) peer " << vpid << ", flags = " << ci->chatFlags << std::endl ;
    std::cerr << "p3ChatService::sendChat() Item:";
    std::cerr << std::endl;
    ci->print(std::cerr);
    std::cerr << std::endl;
#endif

    auto ev = std::make_shared<RsChatServiceEvent>();
    ev->mEventCode = RsChatServiceEventCode::CHAT_MESSAGE_RECEIVED;
    ev->mMsg = message;
    rsEvents->postEvent(ev);
    
    // cyril: history is temporarily disabled for distant chat, since we need to store the full tunnel ID, but then
    // at loading time, the ID is not known so that chat window shows 00000000 as a peer.
    
    //if(!message.chat_id.isDistantChatId())
	    mHistoryMgr->addMessage(message);

    checkSizeAndSendMessage(ci);

    // Check if custom state string has changed, in which case it should be sent to the peer.
    bool should_send_state_string = false ;
    {
        RsStackMutex stack(mChatMtx); /********** STACK LOCKED MTX ******/

        std::map<RsPeerId,StateStringInfo>::iterator it = _state_strings.find(vpid) ;

        if(it == _state_strings.end())
        {
            _state_strings[vpid] = StateStringInfo() ;
            it = _state_strings.find(vpid) ;
            it->second._own_is_new = true ;
        }
        if(it->second._own_is_new)
        {
            should_send_state_string = true ;
            it->second._own_is_new = false ;
        }
    }

    if(should_send_state_string)
    {
#ifdef CHAT_DEBUG
        std::cerr << "own status string is new for peer " << vpid << ": sending it." << std::endl ;
#endif
    /* Lock specifically to access the shared status string safely */
        RS_STACK_MUTEX(mChatMtx); 
        RsChatStatusItem *cs = locked_makeOwnCustomStateStringItem() ;
        cs->PeerId(vpid) ;
        sendChatItem(cs) ;
    }

    return true;
}

// This method might take control over the memory, or modify it, possibly adding missing parts.
// This function looks weird because it cannot duplicate the message since it does not know
// what type of object it is and the duplicate method of lobby messages is reserved for 
// ChatLobby  bouncing objects.
//
// Returns false if the item shouldn't be used (and replaced to NULL)

bool p3ChatService::locked_checkAndRebuildPartialMessage(RsChatMsgItem *& ci)
{
	// Check is the item is ending an incomplete item.
	//
	std::map<RsPeerId,RsChatMsgItem*>::iterator it = _pendingPartialMessages.find(ci->PeerId()) ;

	bool ci_is_incomplete = ci->chatFlags & RS_CHAT_FLAG_PARTIAL_MESSAGE ;

	if(it != _pendingPartialMessages.end())
	{
#ifdef CHAT_DEBUG
		std::cerr << "Pending message found. Appending it." << std::endl;
#endif
		// Yes, there is. Append the item to ci.

		ci->message = it->second->message + ci->message ;
		ci->chatFlags |= it->second->chatFlags ;
        
                // always remove existing partial. The compound message is in ci now.
                
		delete it->second ;
		_pendingPartialMessages.erase(it) ;
	}

        // now decide what to do: if ci is incomplete, store it and replace the pointer with NULL
        // if complete, return it.
        
	if(ci_is_incomplete)
	{
#ifdef CHAT_DEBUG
		std::cerr << "Message is partial, storing for later." << std::endl;
#endif
		// The item is a partial message. Push it, and wait for the rest.
		//
		_pendingPartialMessages[ci->PeerId()] = ci ; 	// cannot use duplicate() here
        	ci = NULL ;					// takes memory ownership over ci
		return false ;
	}
	else
	{
#ifdef CHAT_DEBUG
		std::cerr << "Message is complete, using it now. Size = " << ci->message.size() << ", hash=" << RsDirUtil::sha1sum((uint8_t*)ci->message.c_str(),ci->message.size()) << std::endl;
#endif
		return true ;
	}
}


void p3ChatService::receiveChatQueue()
{
	RsItem *item ;

	while(NULL != (item=recvItem()))
		handleIncomingItem(item) ;
}

class MsgCounter
{
	public:
		MsgCounter() {}

		void clean(rstime_t max_time)
		{
			while(!recv_times.empty() && recv_times.front() < max_time)
				recv_times.pop_front() ;
		}
		std::list<rstime_t> recv_times ;
};

void p3ChatService::handleIncomingItem(RsItem *item)
{
#ifdef CHAT_DEBUG
	std::cerr << "p3ChatService::receiveChatQueue() Item:" << (void*)item
	          << std::endl ;
#endif

	// RsChatMsgItems needs dynamic_cast, since they have derived siblings.
	RsChatMsgItem* ci = dynamic_cast<RsChatMsgItem*>(item);
	if(ci)
	{
		handleRecvChatMsgItem(ci);

		/* +ci+ deletion is handled by handleRecvChatMsgItem ONLY in some
		 * specific cases, in case +ci+ has not been handled deleted it here */
		delete ci ;

		return;
	}

	if(DistributedChatService::handleRecvItem(dynamic_cast<RsChatItem*>(item)))
	{
		delete item;
		return;
	}

	switch(item->PacketSubType())
	{
	case RS_PKT_SUBTYPE_CHAT_STATUS:
		handleRecvChatStatusItem(dynamic_cast<RsChatStatusItem*>(item));
		break;
	case RS_PKT_SUBTYPE_CHAT_AVATAR:
		handleRecvChatAvatarItem(dynamic_cast<RsChatAvatarItem*>(item));
		break;
	case RS_PKT_SUBTYPE_CHAT_AVATAR_INFO:
		handleRecvChatAvatarInfoItem(dynamic_cast<RsChatAvatarInfoItem*>(item));
		break;
	default:
	{
		static int already = false;
		if(!already)
		{
			std::cerr << "Unhandled item subtype "
			          << static_cast<int>(item->PacketSubType())
			          << " in p3ChatService: " << std::endl;
			already = true;
		}
	}
	}
	delete item;
}

void p3ChatService::handleRecvChatAvatarItem(RsChatAvatarItem *ca)
{
	receiveAvatarJpegData(ca) ;

#ifdef CHAT_DEBUG
	std::cerr << "Received avatar data for peer " << ca->PeerId() << ". Notifying." << std::endl ;
#endif
    if(rsEvents)
    {
        auto e = std::make_shared<RsFriendListEvent>();
        e->mSslId = ca->PeerId();
        e->mEventCode = RsFriendListEventCode::NODE_AVATAR_CHANGED;
        rsEvents->postEvent(e);
    }
}

uint32_t p3ChatService::getMaxMessageSecuritySize(int type)
{
	switch (type)
	{
	case RS_CHAT_TYPE_LOBBY:
		return MAX_MESSAGE_SECURITY_SIZE;

	case RS_CHAT_TYPE_PUBLIC:
	case RS_CHAT_TYPE_PRIVATE:
	case RS_CHAT_TYPE_DISTANT:
		return 0; // unlimited
	}

	std::cerr << "p3ChatService::getMaxMessageSecuritySize: Unknown chat type " << type << std::endl;

	return MAX_MESSAGE_SECURITY_SIZE;
}

bool p3ChatService::checkForMessageSecurity(RsChatMsgItem *ci)
{
	// Remove too big messages
	if (ci->chatFlags & RS_CHAT_FLAG_LOBBY)
	{
        uint32_t maxMessageSize = rsChats->getMaxMessageSecuritySize(RS_CHAT_TYPE_LOBBY);
		if (maxMessageSize > 0 && ci->message.length() > maxMessageSize)
		{
			std::ostringstream os;
            os << rsChats->getMaxMessageSecuritySize(RS_CHAT_TYPE_LOBBY);

			ci->message = "**** Security warning: Message bigger than ";
			ci->message += os.str();
			ci->message += " characters, forwarded to you by ";
			ci->message += rsPeers->getPeerName(ci->PeerId());
			ci->message += ", dropped. ****";
			return false;
		}
	}

	// The following code has been suggested, but is kept suspended since it is a bit too much restrictive.
#ifdef SUSPENDED
	// Transform message to lowercase
	std::wstring mes(ci->message);
	std::transform( mes.begin(), mes.end(), mes.begin(), std::towlower);

	// Quick fix for svg attack and other nuisances (inline pictures)
	if (mes.find(L"<img") != std::string::npos)
	{
		ci->message = L"**** Security warning: Message contains an . ****";
		return false;
	}

	// Remove messages with too many line breaks
	size_t pos = 0;
	int count_line_breaks = 0;
	while ((pos = mes.find(L"<br", pos+1)) != std::string::npos)
	{
		count_line_breaks++;
	}
	if (count_line_breaks > 50)
	{
		ci->message = L"**** More than 50 line breaks, dropped. ****";
		return false;
	}
#endif

	// https://en.wikipedia.org/wiki/Billion_laughs
	// This should be done for all incoming HTML messages (also in forums
	// etc.) so this should be a function in some other file.

	if (ci->message.find("<!") != std::string::npos)
	{
		// Drop any message with "<!doctype" or "<!entity"...
		// TODO: check what happens with partial messages
		//
		std::cout << "handleRecvChatMsgItem: " << ci->message << std::endl;
		std::cout << "**********" << std::endl;
		std::cout << "********** entity attack by " << ci->PeerId().toStdString().c_str() << std::endl;
		std::cout << "**********" << std::endl;

		ci->message = "**** This message (from peer id " + rsPeers->getPeerName(ci->PeerId()) + ") has been removed because it contains the string \"<!\".****" ;
		return false;
	}
	// For a future whitelist:
	// things to be kept:
	// <span> <img src="data:image/png;base64,... />
	// <a href="retroshare://…>…</a>
	
	// Also check flags. Lobby msgs should have proper flags, but they can be
	// corrupted by a friend before sending them that can result in e.g. lobby
	// messages ending up in the broadcast channel, etc.
	
	uint32_t fl = ci->chatFlags & (RS_CHAT_FLAG_PRIVATE | RS_CHAT_FLAG_PUBLIC | RS_CHAT_FLAG_LOBBY) ;

#ifdef CHAT_DEBUG
	std::cerr << "Checking msg flags: " << std::hex << fl << std::endl;
#endif

	if(dynamic_cast<RsChatLobbyMsgItem*>(ci) != NULL)
	{
		if(fl != (RS_CHAT_FLAG_PRIVATE | RS_CHAT_FLAG_LOBBY))
			std::cerr << "Warning: received chat lobby message with iconsistent flags " << std::hex << fl << std::dec << " from friend peer " << ci->PeerId() << std::endl;

		ci->chatFlags &= ~RS_CHAT_FLAG_PUBLIC ;
	}
	else if(fl!=0 && !(fl == RS_CHAT_FLAG_PRIVATE || fl == RS_CHAT_FLAG_PUBLIC))	// The !=0 is normally not needed, but we keep it for 
	{																										// a while, for backward compatibility. It's not harmful.
		std::cerr << "Warning: received chat lobby message with iconsistent flags " << std::hex << fl << std::dec << " from friend peer " << ci->PeerId() << std::endl;

		std::cerr << "This message will be dropped."<< std::endl;
		return false ;
	}

	return true ;
}

bool p3ChatService::initiateDistantChatConnexion( const RsGxsId& to_gxs_id,
                                                  const RsGxsId& from_gxs_id,
                                                  DistantChatPeerId& pid,
                                                  uint32_t& error_code,
                                                  bool notify )
{

	if(to_gxs_id.isNull())
	{
		RsErr() << __PRETTY_FUNCTION__ << " Destination RsGxsId is invalid" << std::endl;
		return false;
	}
	if (from_gxs_id.isNull())
	{
		RsErr() << __PRETTY_FUNCTION__ << " Origin RsGxsId is invalid" << std::endl;
		return false;
	}
	if (!rsIdentity->isOwnId(from_gxs_id))
	{
		RsErr() << __PRETTY_FUNCTION__ << " Origin RsGxsId id must be own" << std::endl;
		return false;
	}

	if(DistantChatService::initiateDistantChatConnexion( to_gxs_id,
	                                                     from_gxs_id, pid,
	                                                     error_code, notify ))
	{
		RS_STACK_MUTEX(mDGMutex);
		DistantEndpoints ep; ep.from = from_gxs_id; ep.to = to_gxs_id;
		mDistantGxsMap.insert(DIDEMap::value_type(pid, ep));
		return true;
	}
	return false;
}

bool p3ChatService::receiveGxsTransMail( const RsGxsId& authorId,
                                         const RsGxsId& recipientId,
                                         const uint8_t* data,
                                         uint32_t dataSize )
{
	DistantChatPeerId pid;
	uint32_t error_code;
	if(initiateDistantChatConnexion(
	            authorId, recipientId, pid, error_code, false ))
	{
		RsChatMsgItem* item = static_cast<RsChatMsgItem*>(
		            _serializer->deserialise(
		                const_cast<uint8_t*>(data), &dataSize ));
		RsPeerId rd(p3GxsTunnelService::makeGxsTunnelId(authorId, recipientId));
		item->PeerId(rd);
		handleRecvChatMsgItem(item);
		delete item;
		return true;
	}

	std::cerr << __PRETTY_FUNCTION__ << " (EE) failed initiating"
	          << " distant chat connection error: "<< error_code
	          << std::endl;
	return false;
}

bool p3ChatService::notifyGxsTransSendStatus(RsGxsTransId mailId,
                                             GxsTransSendStatus status )
{
	if ( status != GxsTransSendStatus::RECEIPT_RECEIVED ) return true;

	bool changed = false;

	{
		RS_STACK_MUTEX(mChatMtx);
		auto it = privateOutgoingMap.find(mailId);
		if( it != privateOutgoingMap.end() )
		{
			privateOutgoingMap.erase(it);
			changed = true;
		}
	}

	if(changed)
		IndicateConfigChanged();

	return true;
}

bool p3ChatService::handleRecvChatMsgItem(RsChatMsgItem *& ci)
{
	std::string name;
    //uint32_t popupChatFlag = RS_POPUP_CHAT;

	{
		RS_STACK_MUTEX(mChatMtx);

		// we make sure this call does not take control over the memory
		if(!locked_checkAndRebuildPartialMessage(ci)) return true;
		    /* message is a subpart of an existing message.
			 * So everything ok, but we need to return. */
	}

	// Check for security. This avoids bombing messages, and so on.
	if(!checkForMessageSecurity(ci)) return false;

	/* If it's a lobby item, we need to bounce it and possibly check for timings
	 * etc. */
	if(!DistributedChatService::handleRecvChatLobbyMsgItem(ci)) return false;

#ifdef CHAT_DEBUG
	std::cerr << "p3ChatService::receiveChatQueue() Item:" << std::endl;
	ci->print(std::cerr);
	std::cerr << std::endl << "Got msg. Flags = " << ci->chatFlags << std::endl;
#endif

    // Now treat normal chat stuff such as avatar requests, except for chat lobbies.

    if( !(ci->chatFlags & RS_CHAT_FLAG_LOBBY))
    {
        if(ci->chatFlags & RS_CHAT_FLAG_REQUESTS_AVATAR) 	// no msg here. Just an avatar request.
        {
            sendAvatarJpegData(ci->PeerId()) ;
            return false ;
        }

        // normal msg. Return it normally.
        // Check if new avatar is available at peer's. If so, send a request to get the avatar.

        if((ci->chatFlags & RS_CHAT_FLAG_AVATAR_AVAILABLE) && !(ci->chatFlags & RS_CHAT_FLAG_LOBBY))
        {
#ifdef CHAT_DEBUG
            std::cerr << "New avatar is available for peer " << ci->PeerId() << ", sending request" << std::endl ;
#endif
            sendAvatarRequest(ci->PeerId()) ;
            ci->chatFlags &= ~RS_CHAT_FLAG_AVATAR_AVAILABLE ;
        }

        std::map<RsPeerId,AvatarInfo *>::const_iterator it = _avatars.find(ci->PeerId()) ;

#ifdef CHAT_DEBUG
    // std::cerr << "p3chatservice:: avatar requested from above. " << std::endl ;
#endif
        // has avatar. Return it strait away.
        //
        if(it!=_avatars.end() && it->second->_peer_is_new)
        {
#ifdef CHAT_DEBUG
            std::cerr << "Avatar is new for peer. ending info above" << std::endl ;
#endif
            ci->chatFlags |= RS_CHAT_FLAG_AVATAR_AVAILABLE ;
        }
    }

    std::string message = ci->message;

#ifdef TO_REMOVE
    if(!(ci->chatFlags & RS_CHAT_FLAG_LOBBY))
    {
        if(ci->chatFlags & RS_CHAT_FLAG_PRIVATE)
            RsServer::notify()->AddPopupMessage(popupChatFlag, ci->PeerId().toStdString(), name, message); /* notify private chat message */
        else
        {
#ifdef RS_DIRECT_CHAT
			/* notify public chat message */
			RsServer::notify()->AddPopupMessage( RS_POPUP_GROUPCHAT, ci->PeerId().toStdString(), "", message );
			//RsServer::notify()->AddFeedItem( RS_FEED_ITEM_CHAT_NEW, ci->PeerId().toStdString(), message, "" );
#else // def RS_DIRECT_CHAT
			/* Ignore deprecated direct node broadcast chat messages */
			return false;
#endif
        }
    }
#endif

	ci->recvTime = time(NULL);

    ChatMessage cm;
    initChatMessage(ci, cm);
    cm.incoming = true;
    cm.online = true;
    
    auto ev = std::make_shared<RsChatServiceEvent>();
    ev->mEventCode = RsChatServiceEventCode::CHAT_MESSAGE_RECEIVED;
    ev->mMsg = cm;
    ev->mCid = cm.chat_id;
    rsEvents->postEvent(ev);

    mHistoryMgr->addMessage(cm);

	if(rsEvents)
	{
		auto ev = std::make_shared<RsChatMessageEvent>();
		ev->mChatMessage = cm;
		rsEvents->postEvent(ev);
	}
    
    return true ;
}

void p3ChatService::locked_storeIncomingMsg(RsChatMsgItem */*item*/)
{
#ifdef REMOVE
	privateIncomingList.push_back(item) ;
#endif
}

void p3ChatService::handleRecvChatStatusItem(RsChatStatusItem *cs)
{
#ifdef CHAT_DEBUG
	std::cerr << "Received status string \"" << cs->status_string << "\"" << std::endl ;
#endif

    DistantChatPeerInfo dcpinfo;

	if(cs->flags & RS_CHAT_FLAG_REQUEST_CUSTOM_STATE) 	// no state here just a request.
		sendCustomState(cs->PeerId()) ;
	else if(cs->flags & RS_CHAT_FLAG_CUSTOM_STATE)		// Check if new custom string is available at peer's. 
	{ 																	// If so, send a request to get the custom string.
		receiveStateString(cs->PeerId(),cs->status_string) ;	// store it
    }
	else if(cs->flags & RS_CHAT_FLAG_CUSTOM_STATE_AVAILABLE)
	{
#ifdef CHAT_DEBUG
		std::cerr << "New custom state is available for peer " << cs->PeerId() << ", sending request" << std::endl ;
#endif
		sendCustomStateRequest(cs->PeerId()) ;
	}
    else
    {
        auto ev = std::make_shared<RsChatServiceEvent>();
        ev->mStr = cs->status_string;
        ev->mEventCode = RsChatServiceEventCode::CHAT_STATUS_CHANGED;

        if(DistantChatService::getDistantChatStatus(DistantChatPeerId(cs->PeerId()), dcpinfo))
            ev->mCid = ChatId(DistantChatPeerId(cs->PeerId()));
        else if(cs->flags & RS_CHAT_FLAG_PRIVATE)
            ev->mCid = ChatId(cs->PeerId());
        else if(cs->flags & RS_CHAT_FLAG_PUBLIC)
        {
            ev->mCid = ChatId::makeBroadcastId();
            ev->mCid.broadcast_status_peer_id = cs->PeerId();
        }

        rsEvents->postEvent(ev);
    }

	DistantChatService::handleRecvChatStatusItem(cs) ;
}

void p3ChatService::initChatMessage(RsChatMsgItem *c, ChatMessage &m)
{
    m.chat_id = ChatId(c->PeerId());
    m.chatflags = 0;
    m.sendTime = c->sendTime;
    m.recvTime = c->recvTime;
    m.msg  = c->message;

    RsChatLobbyMsgItem *lobbyItem = dynamic_cast<RsChatLobbyMsgItem*>(c) ;
    if(lobbyItem != NULL)
    {
        m.lobby_peer_gxs_id = lobbyItem->signature.keyId ;
        m.chat_id = ChatId(lobbyItem->lobby_id);
        return;
    }

    DistantChatPeerInfo dcpinfo;
    if(DistantChatService::getDistantChatStatus(DistantChatPeerId(c->PeerId()), dcpinfo))
        m.chat_id = ChatId(DistantChatPeerId(c->PeerId()));

    if (c -> chatFlags & RS_CHAT_FLAG_PRIVATE)
        m.chatflags |= RS_CHAT_PRIVATE;
    else
    {
        m.chat_id = ChatId::makeBroadcastId();
        m.broadcast_peer_id = c->PeerId();
        m.chatflags |= RS_CHAT_PUBLIC;
    }
}

void p3ChatService::setCustomStateString(const std::string& s)
{
	std::set<RsPeerId> onlineList;
	{
		RsStackMutex stack(mChatMtx); /********** STACK LOCKED MTX ******/

#ifdef CHAT_DEBUG
		std::cerr << "p3chatservice: Setting own state string to new value : " << s << std::endl ;
#endif
		_custom_status_string = s ;

		for(std::map<RsPeerId,StateStringInfo>::iterator it(_state_strings.begin());it!=_state_strings.end();++it)
			it->second._own_is_new = true ;

		mServiceCtrl->getPeersConnected(getServiceInfo().mServiceType, onlineList);
	}

    if(rsEvents)
    {
        auto e = std::make_shared<RsFriendListEvent>();
        e->mEventCode = RsFriendListEventCode::OWN_STATUS_CHANGED;
        e->mSslId = mServiceCtrl->getOwnId();
        rsEvents->postEvent(e);
    }

	// alert your online peers to your newly set status
	std::set<RsPeerId>::iterator it(onlineList.begin());
	for(; it != onlineList.end(); ++it){

		RsChatStatusItem *cs = new RsChatStatusItem();
		cs->flags = RS_CHAT_FLAG_CUSTOM_STATE_AVAILABLE;
		cs->status_string = "";
		cs->PeerId(*it);
		sendItem(cs);
	}

	IndicateConfigChanged();
}

void p3ChatService::setOwnNodeAvatarData(const unsigned char *data, int size)
{
	std::set<RsPeerId> onlineList;

	{
		/* We use a scoped block to release the mutex before broadcasting, 
		 * preventing the deadlock you saw in GDB. */
		RsStackMutex stack(mChatMtx); /********** STACK LOCKED MTX ******/

		RsDbg() << "AVATAR setting own node avatar data, size: " << size;

#ifdef CHAT_DEBUG
		std::cerr << "p3chatservice: Setting own avatar to new image." << std::endl ;
#endif

		if(size < 0 || (uint32_t)size > MAX_AVATAR_JPEG_SIZE)
		{
			std::cerr << "Supplied avatar image is too big. Max is " << MAX_AVATAR_JPEG_SIZE << std::endl;
			return ;
		}

		if(_own_avatar != NULL)
			delete _own_avatar ;

		_own_avatar = new AvatarInfo(data,(uint32_t)size) ;

		if(rsEvents)
		{
			auto e = std::make_shared<RsFriendListEvent>();
			e->mEventCode = RsFriendListEventCode::OWN_AVATAR_CHANGED;
			e->mSslId = mServiceCtrl->getOwnId();
			rsEvents->postEvent(e);
		}

		for(std::map<RsPeerId,AvatarInfo *>::iterator it(_avatars.begin());it!=_avatars.end();++it)
			it->second->_own_is_new = true ;

		mServiceCtrl->getPeersConnected(getServiceInfo().mServiceType, onlineList);
	} 

	for(std::set<RsPeerId>::iterator it = onlineList.begin(); it != onlineList.end(); ++it)
	{
		RsDbg() << "AVATAR broadcasting to peer: " << it->toStdString().c_str();
		sendAvatarInfo(*it);
	}

	IndicateConfigChanged();
}

void p3ChatService::receiveStateString(const RsPeerId& id,const std::string& s)
{
	RsStackMutex stack(mChatMtx); /********** STACK LOCKED MTX ******/
#ifdef CHAT_DEBUG
   std::cerr << "p3chatservice: received custom state string for peer " << id << ". Storing it." << std::endl ;
#endif

   bool new_peer = (_state_strings.find(id) == _state_strings.end()) ;

        auto e = std::make_shared<RsFriendListEvent>();
        e->mEventCode = RsFriendListEventCode::NODE_STATE_STRING_CHANGED;
        e->mStateString = s;
        e->mSslId = id;
        rsEvents->postEvent(e);

   _state_strings[id]._custom_status_string = s ;
   _state_strings[id]._peer_is_new = true ;
   _state_strings[id]._own_is_new = new_peer ;
}

void p3ChatService::receiveAvatarJpegData(RsChatAvatarItem *ci)
{
	RsPeerId pid = ci->PeerId();
	RsPeerId ownId = mServiceCtrl->getOwnId();

	/* Safety: Do not let network packets overwrite local 'Self' data */
	if(pid.isNull() || (!ownId.isNull() && pid == ownId)) {
		RsDbg() << "AVATAR: [RECV] Ignored incoming avatar packet identifying as SELF.";
		return;
	}

	RS_STACK_MUTEX(mChatMtx); 
	RsDbg() << "AVATAR: [RECV] Received valid avatar for peer: " << pid.toStdString();
	
	if (_avatars.count(pid)) 
	{
		_avatars[pid]->init(ci->image_data, ci->image_size);
	}
	else
	{
		_avatars[pid] = new AvatarInfo(ci->image_data, ci->image_size);
	}
	_avatars[pid]->_peer_is_new = true;

	IndicateConfigChanged();
}

std::string p3ChatService::getOwnCustomStateString() 
{
	RsStackMutex stack(mChatMtx); /********** STACK LOCKED MTX ******/
	return _custom_status_string ;
}
void p3ChatService::getOwnNodeAvatarData(unsigned char *& data,int& size)
{
	// should be a Mutex here.
	RsStackMutex stack(mChatMtx); /********** STACK LOCKED MTX ******/

	uint32_t s = 0 ;
#ifdef CHAT_DEBUG
	//std::cerr << "p3chatservice:: own avatar requested from above. " << std::endl ;
#endif
	// has avatar. Return it strait away.
	//
	if(_own_avatar != NULL)
	{
	   _own_avatar->toUnsignedChar(data,s) ;
		size = s ;
	}
	else
	{
		data=NULL ;
		size=0 ;
	}
}

std::string p3ChatService::getCustomStateString(const RsPeerId& peer_id) 
{
	{
		// should be a Mutex here.
		RsStackMutex stack(mChatMtx); /********** STACK LOCKED MTX ******/

		std::map<RsPeerId,StateStringInfo>::iterator it = _state_strings.find(peer_id) ; 

		// has it. Return it strait away.
		//
		if(it!=_state_strings.end())
		{
			it->second._peer_is_new = false ;
			return it->second._custom_status_string ;
		}
	}

	sendCustomStateRequest(peer_id);
	return std::string() ;
}

void p3ChatService::getAvatarData(const RsPeerId& peer_id,unsigned char *& data,int& size)
{
	{
		// should be a Mutex here.
		RsStackMutex stack(mChatMtx); /********** STACK LOCKED MTX ******/

		std::map<RsPeerId,AvatarInfo *>::const_iterator it = _avatars.find(peer_id) ; 

#ifdef CHAT_DEBUG
		//std::cerr << "p3chatservice:: avatar for peer " << peer_id << " requested from above. " << std::endl ;
#endif
		// has avatar. Return it straight away.
		//
		if(it!=_avatars.end())
		{
			uint32_t s=0 ;
			it->second->toUnsignedChar(data,s) ;
			size = s ;
			it->second->_peer_is_new = false ;
#ifdef CHAT_DEBUG
			std::cerr << "Already has avatar. Returning it" << std::endl ;
#endif
			return ;
		} else {
            /* Create placeholder to store request time */
            if (it == _avatars.end()) {
                 _avatars[peer_id] = new AvatarInfo();
                 it = _avatars.find(peer_id);
            }

            time_t now = time(NULL);
            if (now - it->second->_last_request_time > 60) {
#ifdef CHAT_DEBUG
			    RsDbg() << "AVATAR p3ChatService::getAvatarData: No avatar for peer " << peer_id << ". Requesting it (throttled).";
#endif
                it->second->_last_request_time = now;
                sendAvatarRequest(peer_id);
            }
		}
	}
}

void p3ChatService::sendAvatarRequest(const RsPeerId& peer_id)
{
	if(!isOnline(peer_id))
		return ;

	// Doesn't have avatar. Request it.
	//
	RsChatMsgItem *ci = new RsChatMsgItem();

	ci->PeerId(peer_id);
	ci->chatFlags = RS_CHAT_FLAG_PRIVATE | RS_CHAT_FLAG_REQUESTS_AVATAR ;
	ci->sendTime = time(NULL);
	ci->message.erase();

#ifdef CHAT_DEBUG
	RsDbg() << "AVATAR p3ChatService::sendAvatarRequest: sending request for avatar to peer " << peer_id;
#endif

	sendChatItem(ci);
}

void p3ChatService::sendCustomStateRequest(const RsPeerId& peer_id){

	if(!isOnline(peer_id))
		return ;

	RsChatStatusItem* cs = new RsChatStatusItem;

	cs->PeerId(peer_id);
	cs->flags = RS_CHAT_FLAG_PRIVATE | RS_CHAT_FLAG_REQUEST_CUSTOM_STATE ;
	cs->status_string.erase();

#ifdef CHAT_DEBUG
	std::cerr << "p3ChatService::sending request for status, to peer " << peer_id << std::endl ;
	std::cerr << std::endl;
#endif

	sendChatItem(cs);
}

RsChatStatusItem *p3ChatService::locked_makeOwnCustomStateStringItem()
{
	/* INTERNAL HELPER: No internal mutex to prevent recursion. 
	 * The caller MUST hold mChatMtx before calling this. */
	RsChatStatusItem *ci = new RsChatStatusItem();

	ci->flags = RS_CHAT_FLAG_CUSTOM_STATE ;
	ci->status_string = _custom_status_string ;

	return ci ;
}

RsChatAvatarItem *p3ChatService::locked_makeOwnAvatarItem()
{
	RsChatAvatarItem *ci = new RsChatAvatarItem();

	if(_own_avatar != nullptr)
	{
		_own_avatar->toUnsignedChar(ci->image_data,ci->image_size) ;
	}

	return ci ;
}

void p3ChatService::sendAvatarJpegData(const RsPeerId& peer_id)
{
#ifdef CHAT_DEBUG
	std::cerr << "p3chatservice: sending requested for peer " << peer_id << ", data=" << (void*)_own_avatar << std::endl ;
#endif

	/* We need to lock here because locked_makeOwnAvatarItem requires it */
	RS_STACK_MUTEX(mChatMtx);

	if(_own_avatar != NULL)
	{
		RsChatAvatarItem *ci = locked_makeOwnAvatarItem();
		ci->PeerId(peer_id);

		// take avatar, and embed it into a std::string.
		//
#ifdef CHAT_DEBUG
		std::cerr << "p3ChatService::sending avatar image to peer" << peer_id << ", image size = " << ci->image_size << std::endl ;
		std::cerr << std::endl;
#endif

		sendChatItem(ci) ;
	}
   else {
#ifdef CHAT_DEBUG
        std::cerr << "We have no avatar yet: Doing nothing" << std::endl ;
#endif
   }
}

void p3ChatService::sendCustomState(const RsPeerId& peer_id){

#ifdef CHAT_DEBUG
std::cerr << "p3chatservice: sending requested status string for peer " << peer_id << std::endl ;
#endif

	/* We lock here, then call the 'locked_' helper */
	RS_STACK_MUTEX(mChatMtx); 
	RsChatStatusItem *cs = locked_makeOwnCustomStateStringItem();
	cs->PeerId(peer_id);

	sendChatItem(cs);
}

RsChatAvatarInfoItem *p3ChatService::locked_makeOwnAvatarInfoItem()
{
    RsChatAvatarInfoItem *ci = new RsChatAvatarInfoItem();
    if(_own_avatar != nullptr)
    {
        ci->timestamp = (uint32_t)_own_avatar->_timestamp;
    }
    return ci;
}

void p3ChatService::sendAvatarInfo(const RsPeerId& peer_id)
{
#ifdef CHAT_DEBUG
    RsDbg() << "AVATAR p3ChatService::sendAvatarInfo: Sending Info to " << peer_id;
#endif
    RS_STACK_MUTEX(mChatMtx); 
    if(_own_avatar != nullptr)
    {
        RsChatAvatarInfoItem *ci = locked_makeOwnAvatarInfoItem();
        ci->PeerId(peer_id);
        sendChatItem(ci);
    }
}

void p3ChatService::handleRecvChatAvatarInfoItem(RsChatAvatarInfoItem *item)
{
    RsPeerId pid = item->PeerId();
    if(pid.isNull()) return;

    RS_STACK_MUTEX(mChatMtx);
    std::map<RsPeerId,AvatarInfo*>::iterator it = _avatars.find(pid);
    
    bool need_update = false;
    if(it == _avatars.end())
    {
        _avatars[pid] = new AvatarInfo();
        _avatars[pid]->_timestamp = (time_t)item->timestamp;
        need_update = true;
    }
    else
    {
        if((uint32_t)it->second->_timestamp < item->timestamp)
        {
            it->second->_timestamp = (time_t)item->timestamp;
            need_update = true;
        }
    }
    
    if(need_update)
    {
#ifdef CHAT_DEBUG
        RsDbg() << "AVATAR p3ChatService::handleRecvChatAvatarInfoItem: Peer " << pid << " has newer avatar (remote TS=" << item->timestamp << "). Requesting.";
#endif
        sendAvatarRequest(pid);
    }
}


bool p3ChatService::loadList(std::list<RsItem*>& load)
{
    for(std::list<RsItem*>::iterator it(load.begin()); it != load.end(); )
    {
        bool item_handled = false;
        RsItem *item = *it;

        /* A. Handle Binary Items (Own Avatar ID 0) */
        RsChatAvatarItem *ai = dynamic_cast<RsChatAvatarItem *>(item);
        if(ai)
        {
            RS_STACK_MUTEX(mChatMtx); 
            RsPeerId pid = ai->PeerId();
            if(pid.isNull() || pid == mServiceCtrl->getOwnId()) {
                if(_own_avatar == NULL) _own_avatar = new AvatarInfo(ai->image_data, ai->image_size);
                item_handled = true;
            }
        }

        /* B. Handle Key-Value Set (Peer Avatars - name/kvs convention) */
        RsConfigKeyValueSet *kv = dynamic_cast<RsConfigKeyValueSet *>(item);

        if(kv)
        {
            /* Check if the KV set contains peer IDs. Since RsConfigKeyValueSet has no name,
             * we iterate and check keys. */
            bool found_avatar = false;
            RS_STACK_MUTEX(mChatMtx);
#ifdef CHAT_DEBUG
            RsDbg() << "AVATAR p3ChatService::loadList: Checking KvSet with " << kv->tlvkvs.pairs.size() << " pairs.";
            if(!kv->tlvkvs.pairs.empty()) {
                RsDbg() << "AVATAR p3ChatService::loadList: First key is: " << kv->tlvkvs.pairs.front().key;
            }
#endif
            for(std::list<RsTlvKeyValue>::const_iterator mit = kv->tlvkvs.pairs.begin(); mit != kv->tlvkvs.pairs.end(); ++mit)
            {
                // If the key is a valid PeerId, we treat it as an avatar entry
                // Only attempt conversion if key format is valid (32 hex chars) to avoid noisy errors
                if (mit->key.length() == 32 && std::all_of(mit->key.begin(), mit->key.end(), ::isxdigit))
                {
                    RsPeerId pid(mit->key);
                    if (!pid.isNull()) {
                        RsDbg() << "AVATAR p3ChatService::loadList: Loading avatar for " << pid << ", encoded_size=" << mit->value.size() << ".";
                        if (_avatars.count(pid)) delete _avatars[pid];
                        _avatars[pid] = new AvatarInfo(mit->value);
                        RsDbg() << "AVATAR p3ChatService::loadList: Loaded avatar for " << pid << ", image_size=" << _avatars[pid]->_image_size << ", timestamp=" << _avatars[pid]->_timestamp << ".";
                        found_avatar = true;
                    }
                }
                else if (mit->key == "OWN_AVATAR_TS")
                {
                    if (_own_avatar) {
                        try {
                            _own_avatar->_timestamp = (time_t)std::stoull(mit->value, nullptr, 10);
                        } catch(...) {}
                    }
                    found_avatar = true;
                }
            }
            if(found_avatar) item_handled = true;
        }

        /* C. Handle Status & Messages */
        if (!item_handled)
        {
            RsChatStatusItem *mitem = dynamic_cast<RsChatStatusItem *>(item);
            if(mitem) {
                RS_STACK_MUTEX(mChatMtx); 
                _custom_status_string = mitem->status_string;
                item_handled = true;
            }
            PrivateOugoingMapItem* om = dynamic_cast<PrivateOugoingMapItem *>(item);
            if(om) {
                RS_STACK_MUTEX(mChatMtx);
                for( auto& pair : om->store )
                    privateOutgoingMap.insert(outMP::value_type(pair.first, new RsChatMsgItem(pair.second)));
                item_handled = true;
            }
        }

        /* D. RELAY to parents */
        if (!item_handled && DistributedChatService::processLoadListItem(item)) item_handled = true;
        if (!item_handled && DistantChatService::processLoadListItem(item)) item_handled = true;

        if(item_handled) { delete item; it = load.erase(it); }
        else { ++it; }
    }
    return true;
}

bool p3ChatService::saveList(bool& cleanup, std::list<RsItem*>& list)
{
    cleanup = true;
    RS_STACK_MUTEX(mChatMtx); 
    RsPeerId ownId = mServiceCtrl->getOwnId();

    /* 1. Save OWN avatar: Binary item with ID 0 */
    if(_own_avatar != NULL && _own_avatar->_image_size > 0)
    {
        RsChatAvatarItem *ai = locked_makeOwnAvatarItem();
        ai->PeerId(RsPeerId()); 
        list.push_back(ai);

        /* Save own TS in a KV set */
        RsConfigKeyValueSet *okv = new RsConfigKeyValueSet();
        RsTlvKeyValue pair;
        pair.key = "OWN_AVATAR_TS";
        pair.value = std::to_string((uint64_t)_own_avatar->_timestamp);
        okv->tlvkvs.pairs.push_back(pair);
        list.push_back(okv);
    }

    /* 2. Save PEER avatars: Key-Value Set (name/kvs convention) */
    if (!_avatars.empty())
    {
#ifdef CHAT_DEBUG
        RsDbg() << "AVATAR p3ChatService::saveList: Total avatars in memory map: " << _avatars.size();
#endif
        RsConfigKeyValueSet *kv = NULL;
        int count_in_chunk = 0;
        const int MAX_AVATARS_PER_CHUNK = 10;

        for(std::map<RsPeerId, AvatarInfo*>::iterator it = _avatars.begin(); it != _avatars.end(); ++it)
        {
            if (it->second != NULL && it->second->_image_size > 0 && !it->first.isNull() && it->first != ownId)
            {
                if (kv == NULL) {
                    kv = new RsConfigKeyValueSet();
                    count_in_chunk = 0;
                }

                RsDbg() << "AVATAR p3ChatService::saveList: Saving avatar for " << it->first << ", image_size=" << it->second->_image_size << ", timestamp=" << it->second->_timestamp << ".";
                RsTlvKeyValue pair;
                pair.key = it->first.toStdString();
                pair.value = it->second->toRadix64();
                RsDbg() << "AVATAR p3ChatService::saveList: Encoded value size=" << pair.value.size() << ", first 32 chars: " << pair.value.substr(0, std::min((size_t)32, pair.value.size()));
                kv->tlvkvs.pairs.push_back(pair);
                count_in_chunk++;

                if (count_in_chunk >= MAX_AVATARS_PER_CHUNK) {
                    list.push_back(kv);
                    kv = NULL;
                }
            }
#ifdef CHAT_DEBUG
            else if (it->first != ownId) {
                 RsDbg() << "AVATAR p3ChatService::saveList: SKIPPING avatar for " << it->first 
                         << " (Size=" << (it->second ? it->second->_image_size : -1) 
                         << ", ValidPtr=" << (it->second != NULL) 
                         << ", ValidId=" << !it->first.isNull() << ")";
            }
#endif
        }
        
        if(kv != NULL) {
            if(!kv->tlvkvs.pairs.empty())
                list.push_back(kv);
            else
                delete kv;
        }
    }

    /* 3. Status and messages */
    list.push_back(locked_makeOwnCustomStateStringItem());
    PrivateOugoingMapItem* om = new PrivateOugoingMapItem;
    for( auto& pair : privateOutgoingMap )
        om->store.insert(std::map<uint64_t, RsChatMsgItem>::value_type(pair.first, *pair.second));
    list.push_back(om);

    /* 4. Parent Relays */
    DistributedChatService::addToSaveList(list);
    DistantChatService::addToSaveList(list);

    return true;
}

void p3ChatService::saveDone()
{
	/* Empty because we now use RsStackMutex in saveList */
}

RsSerialiser *p3ChatService::setupSerialiser()
{
	RsSerialiser *rss = new RsSerialiser ;
	rss->addSerialType(new RsChatSerialiser) ;
	rss->addSerialType(new RsGeneralConfigSerialiser());

	return rss ;
}

/*************** pqiMonitor callback ***********************/

void p3ChatService::statusChange(const std::list<pqiServicePeer> &plist)
{
	for (auto it = plist.cbegin(); it != plist.cend(); ++it)
	{
		if (it->actions & RS_SERVICE_PEER_CONNECTED)
		{
			/* send the saved outgoing messages */
			bool changed = false;
			std::vector<RsChatMsgItem*> to_send;

			{
				RS_STACK_MUTEX(mChatMtx);
				for( auto cit = privateOutgoingMap.begin(); cit != privateOutgoingMap.end(); )
				{
					RsChatMsgItem *c = cit->second;
					if (c->PeerId() == it->id)
					{
						//mHistoryMgr->addMessage(false, c->PeerId(), ownId, c);
						to_send.push_back(c) ;
						changed = true;
						cit = privateOutgoingMap.erase(cit);
						continue;
					}
					++cit;
				}
			}

			for(auto toIt = to_send.begin(); toIt != to_send.end(); ++toIt)
			{
				ChatMessage message;
				initChatMessage(*toIt, message);
				message.incoming = false;
				message.online = true;

                auto ev = std::make_shared<RsChatServiceEvent>();
                ev->mEventCode = RsChatServiceEventCode::CHAT_MESSAGE_RECEIVED;
                ev->mMsg = message;
                rsEvents->postEvent(ev);

                checkSizeAndSendMessage(*toIt); // delete item
			}

			if (changed)
				IndicateConfigChanged();

			/* AVATAR Handshake on connection */
			if(_own_avatar != nullptr && _own_avatar->_image_size > 0)
			{
				sendAvatarInfo(it->id);
			}
			
			/* Request peer's avatar only if we don't have one (backward compatibility with old code) */
			{
				RS_STACK_MUTEX(mChatMtx);
				std::map<RsPeerId,AvatarInfo*>::const_iterator it_avatar = _avatars.find(it->id);
				if(it_avatar == _avatars.end() || it_avatar->second->_image_size == 0)
				{
					sendAvatarRequest(it->id);
				}
			}
		}
		else if (it->actions & RS_SERVICE_PEER_REMOVED)
		{
			/* now handle remove */
			mHistoryMgr->clear(ChatId(it->id));

			RS_STACK_MUTEX(mChatMtx);
			for ( auto cit = privateOutgoingMap.begin(); cit != privateOutgoingMap.end(); )
			{
				RsChatMsgItem *c = cit->second;
				if (c->PeerId() == it->id) cit = privateOutgoingMap.erase(cit);
				else ++cit;
			}
			IndicateConfigChanged();
		}
	}
}
