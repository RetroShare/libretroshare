/*******************************************************************************
 * libretroshare/src/retroshare: rschats.h                                     *
 *                                                                             *
 * libretroshare: retroshare core library                                      *
 *                                                                             *
 * Copyright (C) 2007-2008  Robert Fernie <retroshare@lunamutt.com>            *
 * Copyright (C) 2016-2019  Gioacchino Mazzurco <gio@eigenlab.org>             *
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

#include "retroshare/rstypes.h"
#include "retroshare/rsevents.h"

#define RS_CHAT_LOBBY_EVENT_PEER_LEFT   			0x01
#define RS_CHAT_LOBBY_EVENT_PEER_STATUS 			0x02
#define RS_CHAT_LOBBY_EVENT_PEER_JOINED 			0x03
#define RS_CHAT_LOBBY_EVENT_PEER_CHANGE_NICKNAME 	0x04
#define RS_CHAT_LOBBY_EVENT_KEEP_ALIVE          	0x05

//#define RS_CHAT_LOBBY_PRIVACY_LEVEL_CHALLENGE  	0	/* Used to accept connection challenges only. */
//#define RS_CHAT_LOBBY_PRIVACY_LEVEL_PUBLIC  		1	/* lobby is visible by friends. Friends can connect.*/
//#define RS_CHAT_LOBBY_PRIVACY_LEVEL_PRIVATE 		2	/* lobby invisible by friends. Peers on invitation only .*/

#define RS_CHAT_TYPE_PUBLIC  1
#define RS_CHAT_TYPE_PRIVATE 2
#define RS_CHAT_TYPE_LOBBY   3
#define RS_CHAT_TYPE_DISTANT 4

const ChatLobbyFlags RS_CHAT_LOBBY_FLAGS_AUTO_SUBSCRIBE( 0x00000001 ) ;
const ChatLobbyFlags RS_CHAT_LOBBY_FLAGS_deprecated    ( 0x00000002 ) ;
const ChatLobbyFlags RS_CHAT_LOBBY_FLAGS_PUBLIC        ( 0x00000004 ) ;
const ChatLobbyFlags RS_CHAT_LOBBY_FLAGS_CHALLENGE     ( 0x00000008 ) ;
const ChatLobbyFlags RS_CHAT_LOBBY_FLAGS_PGP_SIGNED    ( 0x00000010 ) ; // requires the signing ID to be PGP-linked. Avoids anonymous crap.

typedef uint64_t	ChatLobbyId ;
typedef uint64_t	ChatLobbyMsgId ;
typedef std::string ChatLobbyNickName ;

#define RS_CHAT_PUBLIC 			                  0x0001
#define RS_CHAT_PRIVATE 		                  0x0002
#define RS_CHAT_AVATAR_AVAILABLE 	              0x0004

#define RS_DISTANT_CHAT_STATUS_UNKNOWN			  0x0000
#define RS_DISTANT_CHAT_STATUS_TUNNEL_DN   		  0x0001
#define RS_DISTANT_CHAT_STATUS_CAN_TALK		 	  0x0002
#define RS_DISTANT_CHAT_STATUS_REMOTELY_CLOSED 	  0x0003

#define RS_DISTANT_CHAT_ERROR_NO_ERROR            0x0000
#define RS_DISTANT_CHAT_ERROR_DECRYPTION_FAILED   0x0001
#define RS_DISTANT_CHAT_ERROR_SIGNATURE_MISMATCH  0x0002
#define RS_DISTANT_CHAT_ERROR_UNKNOWN_KEY         0x0003
#define RS_DISTANT_CHAT_ERROR_UNKNOWN_HASH        0x0004

#define RS_DISTANT_CHAT_FLAG_SIGNED               0x0001
#define RS_DISTANT_CHAT_FLAG_SIGNATURE_OK         0x0002

#define RS_DISTANT_CHAT_CONTACT_PERMISSION_FLAG_FILTER_NONE           0x0000
#define RS_DISTANT_CHAT_CONTACT_PERMISSION_FLAG_FILTER_NON_CONTACTS   0x0001
#define RS_DISTANT_CHAT_CONTACT_PERMISSION_FLAG_FILTER_EVERYBODY      0x0002

struct DistantChatPeerInfo : RsSerializable
{
    DistantChatPeerInfo() : status(0),pending_items(0) {}

    RsGxsId to_id ;
    RsGxsId own_id ;
    DistantChatPeerId peer_id ;	// this is the tunnel id actually
    uint32_t status ;			// see the values in rschats.h
    uint32_t pending_items;		// items not sent, waiting for a tunnel

    ///* @see RsEvent @see RsSerializable
    void serial_process( RsGenericSerializer::SerializeJob j, RsGenericSerializer::SerializeContext& ctx ) override
    {
        RS_SERIAL_PROCESS(to_id);
        RS_SERIAL_PROCESS(own_id);
        RS_SERIAL_PROCESS(peer_id);
        RS_SERIAL_PROCESS(status);
        RS_SERIAL_PROCESS(pending_items);
    }
};

enum class RsChatHistoryChangeFlags: uint8_t
{
    SAME   = 0x00,
    MOD    = 0x01, /* general purpose, check all */
    ADD    = 0x02, /* flagged additions */
    DEL    = 0x04, /* flagged deletions */
};
RS_REGISTER_ENUM_FLAGS_TYPE(RsChatHistoryChangeFlags);

// Identifier for an chat endpoint like
// neighbour peer, distant peer, chatlobby, broadcast
class ChatId : RsSerializable
{
public:
    ChatId();
    virtual ~ChatId() = default;

    explicit ChatId(RsPeerId     id);
    explicit ChatId(ChatLobbyId  id);
    explicit ChatId(DistantChatPeerId id);
    explicit ChatId(std::string str);
    static ChatId makeBroadcastId();

    std::string toStdString() const;
    bool operator<(const ChatId& other) const;
    bool isSameEndpoint(const ChatId& other) const;

    bool operator==(const ChatId& other) const { return isSameEndpoint(other) ; }

    bool isNotSet() const;
    bool isPeerId() const;
    bool isDistantChatId() const;
    bool isLobbyId() const;
    bool isBroadcast() const;

    RsPeerId    toPeerId()  const;
    ChatLobbyId toLobbyId() const;
    DistantChatPeerId toDistantChatId() const;

    // for the very specific case of transfering a status string
    // from the chatservice to the gui,
    // this defines from which peer the status string came from
    RsPeerId broadcast_status_peer_id;
private:
    enum Type : uint8_t
    {	TYPE_NOT_SET,
        TYPE_PRIVATE,            // private chat with directly connected friend, peer_id is valid
        TYPE_PRIVATE_DISTANT,    // private chat with distant peer, gxs_id is valid
        TYPE_LOBBY,              // chat lobby id, lobby_id is valid
        TYPE_BROADCAST           // message to/from all connected peers
    };

    Type type;
    RsPeerId peer_id;
    DistantChatPeerId distant_chat_id;
    ChatLobbyId lobby_id;

    // RsSerializable interface
public:
    void serial_process(RsGenericSerializer::SerializeJob j, RsGenericSerializer::SerializeContext &ctx) {
        RS_SERIAL_PROCESS(broadcast_status_peer_id);
        RS_SERIAL_PROCESS(type);
        RS_SERIAL_PROCESS(peer_id);
        RS_SERIAL_PROCESS(distant_chat_id);
        RS_SERIAL_PROCESS(lobby_id);
    }
};

struct ChatMessage : RsSerializable
{
    ChatId chat_id; // id of chat endpoint
    RsPeerId broadcast_peer_id; // only used for broadcast chat: source peer id
    RsGxsId lobby_peer_gxs_id; // only used for lobbys: nickname of message author
    std::string peer_alternate_nickname; // only used when key is unknown.

    unsigned int chatflags;
    uint32_t sendTime;
    uint32_t recvTime;
    std::string msg;
    bool incoming;
    bool online; // for outgoing messages: was this message send?
    //bool system_message;

    ///* @see RsEvent @see RsSerializable
    void serial_process( RsGenericSerializer::SerializeJob j, RsGenericSerializer::SerializeContext& ctx ) override
    {
        RS_SERIAL_PROCESS(chat_id);
        RS_SERIAL_PROCESS(broadcast_peer_id);
        RS_SERIAL_PROCESS(lobby_peer_gxs_id);
        RS_SERIAL_PROCESS(peer_alternate_nickname);

        RS_SERIAL_PROCESS(chatflags);
        RS_SERIAL_PROCESS(sendTime);
        RS_SERIAL_PROCESS(recvTime);
        RS_SERIAL_PROCESS(msg);
        RS_SERIAL_PROCESS(incoming);
        RS_SERIAL_PROCESS(online);
    }
};

class ChatLobbyInvite : RsSerializable
{
public:
    virtual ~ChatLobbyInvite() = default;

    ChatLobbyId lobby_id ;
    RsPeerId peer_id ;
    std::string lobby_name ;
    std::string lobby_topic ;
    ChatLobbyFlags lobby_flags ;

    // RsSerializable interface
public:
    void serial_process(RsGenericSerializer::SerializeJob j, RsGenericSerializer::SerializeContext &ctx) {
        RS_SERIAL_PROCESS(lobby_id);
        RS_SERIAL_PROCESS(peer_id);
        RS_SERIAL_PROCESS(lobby_name);
        RS_SERIAL_PROCESS(lobby_topic);
        RS_SERIAL_PROCESS(lobby_flags);
    }
};

/****************************************/
/*        Chat Events Management        */
/****************************************/

enum class RsChatServiceEventCode: uint8_t
{
    UNKNOWN                               = 0x00,

    CHAT_MESSAGE_RECEIVED 			      = 0x01,    // new private incoming chat, NOTIFY_LIST_PRIVATE_INCOMING_CHAT
    CHAT_STATUS_CHANGED   			      = 0x02,    //
    CHAT_HISTORY_CHANGED  			      = 0x03,    //
};

enum class RsChatLobbyEventCode: uint8_t
{
    UNKNOWN                               = 0x00,

    CHAT_LOBBY_LIST_CHANGED               = 0x03,    // NOTIFY_LIST_CHAT_LOBBY_LIST	,	    ADD/REMOVE , // new/removed chat lobby
    CHAT_LOBBY_INVITE_RECEIVED            = 0x04,    // NOTIFY_LIST_CHAT_LOBBY_INVITE, received chat lobby invite
    CHAT_LOBBY_EVENT_PEER_LEFT   	 	  = 0x05,	 // RS_CHAT_LOBBY_EVENT_PEER_LEFT
    CHAT_LOBBY_EVENT_PEER_STATUS 	      = 0x06,	 // RS_CHAT_LOBBY_EVENT_PEER_STATUS
    CHAT_LOBBY_EVENT_PEER_JOINED          = 0x07,	 // RS_CHAT_LOBBY_EVENT_PEER_JOINED
    CHAT_LOBBY_EVENT_PEER_CHANGE_NICKNAME = 0x08,	 // RS_CHAT_LOBBY_EVENT_PEER_CHANGE_NICKNAME
    CHAT_LOBBY_EVENT_KEEP_ALIVE           = 0x09,	 // RS_CHAT_LOBBY_EVENT_KEEP_ALIVE
};

enum class RsDistantChatEventCode: uint8_t
{
    TUNNEL_STATUS_UNKNOWN                 = 0x00,
    TUNNEL_STATUS_CAN_TALK                = 0x01,
    TUNNEL_STATUS_TUNNEL_DN               = 0x02,
    TUNNEL_STATUS_REMOTELY_CLOSED         = 0x03,
    TUNNEL_STATUS_CONNECTION_REFUSED      = 0x04,
};

struct RsChatLobbyEvent : RsEvent // This event handles events internal to the distributed chat system
{
    RsChatLobbyEvent() : RsEvent(RsEventType::CHAT_SERVICE),mEventCode(RsChatLobbyEventCode::UNKNOWN),mLobbyId(0),mTimeShift(0) {}
    virtual ~RsChatLobbyEvent() override = default;

    RsChatLobbyEventCode mEventCode;

    uint64_t mLobbyId;
    RsGxsId mGxsId;
    std::string mStr;
    ChatMessage mMsg;
    int mTimeShift;

    void serial_process(RsGenericSerializer::SerializeJob j, RsGenericSerializer::SerializeContext &ctx) override {

        RsEvent::serial_process(j,ctx);

        RS_SERIAL_PROCESS(mEventCode);
        RS_SERIAL_PROCESS(mLobbyId);
        RS_SERIAL_PROCESS(mGxsId);
        RS_SERIAL_PROCESS(mStr);
        RS_SERIAL_PROCESS(mMsg);
        RS_SERIAL_PROCESS(mTimeShift);
    }
};

struct RsDistantChatEvent : RsEvent // This event handles events internal to the distant chat system
{
    RsDistantChatEvent() : RsEvent(RsEventType::CHAT_SERVICE),mEventCode(RsDistantChatEventCode::TUNNEL_STATUS_UNKNOWN) {}
    virtual ~RsDistantChatEvent() override = default;

    RsDistantChatEventCode mEventCode;
    DistantChatPeerId mId;

    void serial_process(RsGenericSerializer::SerializeJob j, RsGenericSerializer::SerializeContext &ctx) override {

        RsEvent::serial_process(j,ctx);

        RS_SERIAL_PROCESS(mEventCode);
        RS_SERIAL_PROCESS(mId);
    }
};


struct RsChatServiceEvent : RsEvent // This event handles chat in general: status strings, new messages, etc.
{
    RsChatServiceEvent() : RsEvent(RsEventType::CHAT_SERVICE), mEventCode(RsChatServiceEventCode::UNKNOWN),
        mMsgHistoryId(0),mHistoryChangeType(RsChatHistoryChangeFlags::SAME) {}
    virtual ~RsChatServiceEvent() override = default;

    RsChatServiceEventCode mEventCode;

    std::string mStr;
    ChatId mCid;
    ChatMessage mMsg;
    uint32_t mMsgHistoryId;
    RsChatHistoryChangeFlags mHistoryChangeType;          // ChatHistoryChangeFlags::ADD/DEL/MOD

    void serial_process(RsGenericSerializer::SerializeJob j, RsGenericSerializer::SerializeContext &ctx) override {

        RsEvent::serial_process(j,ctx);

        RS_SERIAL_PROCESS(mEventCode);
        RS_SERIAL_PROCESS(mStr);
        RS_SERIAL_PROCESS(mCid);
        RS_SERIAL_PROCESS(mMsg);
        RS_SERIAL_PROCESS(mMsgHistoryId);
        RS_SERIAL_PROCESS(mHistoryChangeType);
    }
};

/****************************************/
/*           Chat Rooms Classes         */
/****************************************/

struct VisibleChatLobbyRecord : RsSerializable
{
    VisibleChatLobbyRecord():
        lobby_id(0), total_number_of_peers(0), last_report_time(0) {}
    virtual ~VisibleChatLobbyRecord() override = default;

    ChatLobbyId lobby_id ;						// unique id of the lobby
    std::string lobby_name ;					// name to use for this lobby
    std::string lobby_topic ;					// topic to use for this lobby
    std::set<RsPeerId> participating_friends ;	// list of direct friend who participate.

    uint32_t total_number_of_peers ;			// total number of particpating peers. Might not be
    rstime_t last_report_time ; 					// last time the lobby was reported.
    ChatLobbyFlags lobby_flags ;				// see RS_CHAT_LOBBY_PRIVACY_LEVEL_PUBLIC / RS_CHAT_LOBBY_PRIVACY_LEVEL_PRIVATE

    /// @see RsSerializable
    void serial_process( RsGenericSerializer::SerializeJob j, RsGenericSerializer::SerializeContext &ctx) override {
        RS_SERIAL_PROCESS(lobby_id);
        RS_SERIAL_PROCESS(lobby_name);
        RS_SERIAL_PROCESS(lobby_topic);
        RS_SERIAL_PROCESS(participating_friends);

        RS_SERIAL_PROCESS(total_number_of_peers);
        RS_SERIAL_PROCESS(last_report_time);
        RS_SERIAL_PROCESS(lobby_flags);
    }
};

class ChatLobbyInfo : RsSerializable
{
public:
    virtual ~ChatLobbyInfo() = default;

    ChatLobbyId lobby_id ;						// unique id of the lobby
    std::string lobby_name ;					// name to use for this lobby
    std::string lobby_topic ;					// topic to use for this lobby
    std::set<RsPeerId> participating_friends ;	// list of direct friend who participate. Used to broadcast sent messages.
    RsGxsId gxs_id ;							// ID to sign messages

    ChatLobbyFlags lobby_flags ;				// see RS_CHAT_LOBBY_PRIVACY_LEVEL_PUBLIC / RS_CHAT_LOBBY_PRIVACY_LEVEL_PRIVATE
    std::map<RsGxsId, rstime_t> gxs_ids ;			// list of non direct friend who participate. Used to display only.
    rstime_t last_activity ;						// last recorded activity. Useful for removing dead lobbies.

    virtual void clear() { gxs_ids.clear(); lobby_id = 0; lobby_name.clear(); lobby_topic.clear(); participating_friends.clear(); }

    // RsSerializable interface
public:
    void serial_process(RsGenericSerializer::SerializeJob j, RsGenericSerializer::SerializeContext &ctx) {
        RS_SERIAL_PROCESS(lobby_id);
        RS_SERIAL_PROCESS(lobby_name);
        RS_SERIAL_PROCESS(lobby_topic);
        RS_SERIAL_PROCESS(participating_friends);
        RS_SERIAL_PROCESS(gxs_id);

        RS_SERIAL_PROCESS(lobby_flags);
        RS_SERIAL_PROCESS(gxs_ids);
        RS_SERIAL_PROCESS(last_activity);
    }
};

/****************************************/
/* Main entry point class for all chats */
/****************************************/

class RsChats;
/**
 * @brief Pointer to retroshare's message service
 * @jsonapi{development}
 */
extern RsChats* rsChats;

class RsChats
{
public:
    // sendChat for broadcast, private, lobby and private distant chat
    // note: for lobby chat, you first have to subscribe to a lobby
    //       for private distant chat, it is reqired to have an active distant chat session

    /**
     * @brief sendChat send a chat message to a given id
     * @jsonapi{development}
     * @param[in] id id to send the message
     * @param[in] msg message to send
     * @return true on success
     */
    virtual bool sendChat(ChatId id, std::string msg) = 0;

    /**
     * @brief getMaxMessageSecuritySize get the maximum size of a chat message
     * @jsonapi{development}
     * @param[in] type chat type
     * @return maximum size or zero for infinite
     */
    virtual uint32_t getMaxMessageSecuritySize(int type) = 0;

    /**
     * @brief sendStatusString send a status string
     * @jsonapi{development}
     * @param[in] id chat id to send the status string to
     * @param[in] status_string status string
     */
    virtual void sendStatusString(const ChatId &id, const std::string &status_string) = 0;

    /**
     * @brief clearChatLobby clear a chat lobby
     * @jsonapi{development}
     * @param[in] id chat lobby id to clear
     */
    virtual void clearChatLobby(const ChatId &id) = 0;

    /**
     * @brief setCustomStateString set your custom status message
     * @jsonapi{development}
     * @param[in] status_string status message
     */
    virtual void setCustomStateString(const std::string &status_string) = 0;

    /**
     * @brief getCustomStateString get your custom status message
     * @return status message
     */
    virtual std::string getOwnCustomStateString() = 0;

    /**
     * @brief getCustomStateString get the custom status message from a peer
     * @jsonapi{development}
     * @param[in] peer_id peer id to the peer you want to get the status message from
     * @return status message
     */
    virtual std::string getCustomStateString(const RsPeerId &peer_id) = 0;

    // get avatar data for peer pid
    virtual void getAvatarData(const RsPeerId& pid,unsigned char *& data,int& size) = 0 ;
    // set own avatar data
    virtual void setOwnNodeAvatarData(const unsigned char *data,int size) = 0 ;
    virtual void getOwnNodeAvatarData(unsigned char *& data,int& size) = 0 ;

    /****************************************/
    /*            Chat lobbies              */
    /****************************************/
    /**
     * @brief joinVisibleChatLobby join a lobby that is visible
     * @jsonapi{development}
     * @param[in] lobby_id lobby to join to
     * @param[in] own_id chat id to use
     * @return true on success
     */
    virtual bool joinVisibleChatLobby(const ChatLobbyId &lobby_id, const RsGxsId &own_id) = 0 ;

    /**
     * @brief getChatLobbyList get ids of subscribed lobbies
     * @jsonapi{development}
     * @param[out] cl_list lobby list
     */
    virtual void getChatLobbyList(std::list<ChatLobbyId> &cl_list) = 0;

    /**
     * @brief getChatLobbyInfo get lobby info of a subscribed chat lobby. Returns true if lobby id is valid.
     * @jsonapi{development}
     * @param[in] id id to get infos from
     * @param[out] info lobby infos
     * @return true on success
     */
    virtual bool getChatLobbyInfo(const ChatLobbyId &id, ChatLobbyInfo &info) = 0 ;

    /**
     * @brief getListOfNearbyChatLobbies get info about all lobbies, subscribed and unsubscribed
     * @jsonapi{development}
     * @param[out] public_lobbies list of all visible lobbies
     */
    virtual void getListOfNearbyChatLobbies(std::vector<VisibleChatLobbyRecord> &public_lobbies) = 0 ;

    /**
     * @brief invitePeerToLobby invite a peer to join a lobby
     * @jsonapi{development}
     * @param[in] lobby_id lobby it to invite into
     * @param[in] peer_id peer to invite
     */
    virtual void invitePeerToLobby(const ChatLobbyId &lobby_id, const RsPeerId &peer_id) = 0;

    /**
     * @brief acceptLobbyInvite accept a chat invite
     * @jsonapi{development}
     * @param[in] id chat lobby id you were invited into and you want to join
     * @param[in] identity chat identity to use
     * @return true on success
     */
    virtual bool acceptLobbyInvite(const ChatLobbyId &id, const RsGxsId &identity) = 0 ;

    /**
     * @brief denyLobbyInvite deny a chat lobby invite
     * @jsonapi{development}
     * @param[in] id chat lobby id you were invited into
     * @return true on success
     */
    virtual bool denyLobbyInvite(const ChatLobbyId &id) = 0 ;

    /**
     * @brief getPendingChatLobbyInvites get a list of all pending chat lobby invites
     * @jsonapi{development}
     * @param[out] invites list of all pending chat lobby invites
     */
    virtual void getPendingChatLobbyInvites(std::list<ChatLobbyInvite> &invites) = 0;

    /**
     * @brief unsubscribeChatLobby leave a chat lobby
     * @jsonapi{development}
     * @param[in] lobby_id lobby to leave
     */
    virtual void unsubscribeChatLobby(const ChatLobbyId &lobby_id) = 0;

    /**
     * @brief sendLobbyStatusPeerLeaving notify friend nodes that we're leaving a subscribed lobby
     * @jsonapi{development}
     * @param[in] lobby_id lobby to leave
     */
    virtual void sendLobbyStatusPeerLeaving(const ChatLobbyId& lobby_id) = 0;

    /**
     * @brief setIdentityForChatLobby set the chat identit
     * @jsonapi{development}
     * @param[in] lobby_id lobby to change the chat idnetity for
     * @param[in] nick new chat identity
     * @return true on success
     */
    virtual bool setIdentityForChatLobby(const ChatLobbyId &lobby_id, const RsGxsId &nick) = 0;

    /**
     * @brief getIdentityForChatLobby
     * @jsonapi{development}
     * @param[in] lobby_id	lobby to get the chat id from
     * @param[out] nick	chat identity
     * @return true on success
     */
    virtual bool getIdentityForChatLobby(const ChatLobbyId &lobby_id, RsGxsId &nick) = 0 ;

    /**
     * @brief setDefaultIdentityForChatLobby set the default identity used for chat lobbies
     * @jsonapi{development}
     * @param[in] nick chat identitiy to use
     * @return true on success
     */
    virtual bool setDefaultIdentityForChatLobby(const RsGxsId &nick) = 0;

    /**
     * @brief getDefaultIdentityForChatLobby get the default identity used for chat lobbies
     * @jsonapi{development}
     * @param[out] id chat identitiy to use
     */
    virtual void getDefaultIdentityForChatLobby(RsGxsId &id) = 0 ;

    /**
     * @brief setLobbyAutoSubscribe enable or disable auto subscribe for a chat lobby
     * @jsonapi{development}
     * @param[in] lobby_id lobby to auto (un)subscribe
     * @param[in] autoSubscribe set value for auto subscribe
     */
    virtual void setLobbyAutoSubscribe(const ChatLobbyId &lobby_id, const bool autoSubscribe) = 0 ;

    /**
     * @brief getLobbyAutoSubscribe get current value of auto subscribe
     * @jsonapi{development}
     * @param[in] lobby_id lobby to get value from
     * @return wether lobby has auto subscribe enabled or disabled
     */
    virtual bool getLobbyAutoSubscribe(const ChatLobbyId &lobby_id) = 0 ;

    /**
     * @brief createChatLobby create a new chat lobby
     * @jsonapi{development}
     * @param[in] lobby_name lobby name
     * @param[in] lobby_identity chat id to use for new lobby
     * @param[in] lobby_topic lobby toppic
     * @param[in] invited_friends list of friends to invite
     * @param[in] lobby_privacy_type flag for new chat lobby
     * @return chat id of new lobby
     */
    virtual ChatLobbyId createChatLobby(const std::string &lobby_name, const RsGxsId &lobby_identity, const std::string &lobby_topic, const std::set<RsPeerId> &invited_friends, ChatLobbyFlags lobby_privacy_type) = 0 ;

    /****************************************/
    /*            Distant chat              */
    /****************************************/

    virtual uint32_t getDistantChatPermissionFlags()=0 ;
    virtual bool setDistantChatPermissionFlags(uint32_t flags)=0 ;

        /**
     * @brief initiateDistantChatConnexion initiate a connexion for a distant chat
     * @jsonapi{development}
     * @param[in] to_pid RsGxsId to start the connection
     * @param[in] from_pid owned RsGxsId who start the connection
     * @param[out] pid distant chat id
     * @param[out] error_code if the connection can't be stablished
     * @param[in] notify notify remote that the connection is stablished
     * @return true on success. If you try to initate a connection already started it will return the pid of it.
     */
    virtual bool initiateDistantChatConnexion(
            const RsGxsId& to_pid, const RsGxsId& from_pid,
            DistantChatPeerId& pid, uint32_t& error_code,
            bool notify = true ) = 0;

    /**
     * @brief getDistantChatStatus receives distant chat info to a given distant chat id
     * @jsonapi{development}
     * @param[in] pid distant chat id
     * @param[out] info distant chat info
     * @return true on success
     */
    virtual bool getDistantChatStatus(const DistantChatPeerId& pid, DistantChatPeerInfo& info)=0;

    /**
     * @brief closeDistantChatConnexion
     * @jsonapi{development}
     * @param[in] pid distant chat id to close the connection
     * @return true on success
     */
    virtual bool closeDistantChatConnexion(const DistantChatPeerId& pid)=0;
};

