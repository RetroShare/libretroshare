/*******************************************************************************
 * libretroshare/src/retroshare: rsmail.h                                      *
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

#include <list>
#include <iostream>
#include <string>
#include <set>
#include <assert.h>

#include "retroshare/rstypes.h"
#include "retroshare/rsgxsifacetypes.h"
#include "retroshare/rsevents.h"
#include "util/rsdeprecate.h"
#include "util/rsmemory.h"

/********************** For Messages and Channels *****************/

#define RS_MSG_BOXMASK   0x000f   /* Mask for determining Box */

#define RS_MSG_OUTGOING        0x0001   /* !Inbox */
#define RS_MSG_PENDING         0x0002   /* OutBox */
#define RS_MSG_DRAFT           0x0004   /* Draft  */

/* ORs of above */
#define RS_MSG_INBOX           0x00     /* Inbox */
#define RS_MSG_SENTBOX         0x01     /* Sentbox  = OUTGOING           */
#define RS_MSG_OUTBOX          0x03     /* Outbox   = OUTGOING + PENDING */
#define RS_MSG_DRAFTBOX        0x05     /* Draftbox = OUTGOING + DRAFT   */
#define RS_MSG_TRASHBOX        0x20     /* Trashbox = RS_MSG_TRASH       */

#define RS_MSG_NEW                   0x000010   /* New */
#define RS_MSG_TRASH                 0x000020   /* Trash */
#define RS_MSG_UNREAD_BY_USER        0x000040   /* Unread by user */
#define RS_MSG_REPLIED               0x000080   /* Message is replied */
#define RS_MSG_FORWARDED             0x000100   /* Message is forwarded */
#define RS_MSG_STAR                  0x000200   /* Message is marked with a star */
// system message
#define RS_MSG_USER_REQUEST          0x000400   /* user request */
#define RS_MSG_FRIEND_RECOMMENDATION 0x000800   /* friend recommendation */
#define RS_MSG_DISTANT               0x001000	/* message is distant */
#define RS_MSG_SIGNATURE_CHECKS      0x002000	/* message was signed, and signature checked */
#define RS_MSG_SIGNED                0x004000	/* message was signed and signature didn't check */
#define RS_MSG_LOAD_EMBEDDED_IMAGES  0x008000   /* load embedded images */
#define RS_MSG_PUBLISH_KEY           0x020000   /* publish key */
#define RS_MSG_SPAM                  0x040000   /* Message is marked as spam */

#define RS_MSG_SYSTEM                (RS_MSG_USER_REQUEST | RS_MSG_FRIEND_RECOMMENDATION | RS_MSG_PUBLISH_KEY)

#define RS_MSGTAGTYPE_IMPORTANT  1
#define RS_MSGTAGTYPE_WORK       2
#define RS_MSGTAGTYPE_PERSONAL   3
#define RS_MSGTAGTYPE_TODO       4
#define RS_MSGTAGTYPE_LATER      5
#define RS_MSGTAGTYPE_USER       100


typedef std::string RsMailMessageId; // TODO: rebase on t_RsGenericIdType

/**
 * Used to return a tracker id so the API user can keep track of sent mail
 * status, it contains mail id, and recipient id
 */
struct RsMailIdRecipientIdPair : RsSerializable
{
	RsMailIdRecipientIdPair(RsMailMessageId mailId, RsGxsId recipientId):
	    mMailId(mailId), mRecipientId(recipientId) {}

	RsMailMessageId mMailId;
	RsGxsId mRecipientId;

	/// @see RsSerializable
	void serial_process(
	        RsGenericSerializer::SerializeJob j,
	        RsGenericSerializer::SerializeContext &ctx ) override;

	bool operator<(const RsMailIdRecipientIdPair& other) const;
	bool operator==(const RsMailIdRecipientIdPair& other) const;

	RsMailIdRecipientIdPair() = default;
	~RsMailIdRecipientIdPair() override = default;
};

namespace Rs
{
namespace Msgs
{

enum class BoxName:uint8_t {
        BOX_NONE   = 0x00,
        BOX_INBOX  = 0x01,
        BOX_OUTBOX = 0x02,
        BOX_DRAFTS = 0x03,
        BOX_SENT   = 0x04,
        BOX_TRASH  = 0x05,
        BOX_ALL    = 0x06
    };

class MsgAddress: public RsSerializable
{
	public:
        MsgAddress() : _type(MSG_ADDRESS_TYPE_UNKNOWN),_mode(MSG_ADDRESS_MODE_UNKNOWN) {}

		typedef enum { MSG_ADDRESS_TYPE_UNKNOWN  = 0x00,
							MSG_ADDRESS_TYPE_RSPEERID = 0x01, 
							MSG_ADDRESS_TYPE_RSGXSID  = 0x02, 
                            MSG_ADDRESS_TYPE_PLAIN    = 0x03 } AddressType;

		typedef enum { MSG_ADDRESS_MODE_UNKNOWN = 0x00,
		               MSG_ADDRESS_MODE_TO      = 0x01,
		               MSG_ADDRESS_MODE_CC      = 0x02,
		               MSG_ADDRESS_MODE_BCC     = 0x03 } AddressMode;

		explicit MsgAddress(const RsGxsId&  gid, MsgAddress::AddressMode mmode)
			: _type(MSG_ADDRESS_TYPE_RSGXSID),  _mode(mmode), _addr_string(gid.toStdString()){}

		explicit MsgAddress(const RsPeerId& pid, MsgAddress::AddressMode mmode)
			: _type(MSG_ADDRESS_TYPE_RSPEERID), _mode(mmode), _addr_string(pid.toStdString()){}

		explicit MsgAddress(const std::string& email, MsgAddress::AddressMode mmode)
            : _type(MSG_ADDRESS_TYPE_PLAIN), _mode(mmode), _addr_string(email){}

        void serial_process( RsGenericSerializer::SerializeJob j, RsGenericSerializer::SerializeContext &ctx ) override
        {
            RS_SERIAL_PROCESS(_type);
            RS_SERIAL_PROCESS(_mode);
            RS_SERIAL_PROCESS(_addr_string);
        }

        MsgAddress::AddressType type() const { return _type ;}
        MsgAddress::AddressMode mode() const { return _mode ;}

        RsGxsId toGxsId()     const { checkType(MSG_ADDRESS_TYPE_RSGXSID);return RsGxsId (_addr_string);}
        RsPeerId toRsPeerId() const { checkType(MSG_ADDRESS_TYPE_RSPEERID);return RsPeerId(_addr_string);}
        std::string toEmail() const { checkType(MSG_ADDRESS_TYPE_PLAIN   );return          _addr_string ;}
        std::string toStdString() const { return _addr_string ;}

        void clear() { _addr_string=""; _type=MSG_ADDRESS_TYPE_UNKNOWN; _mode=MSG_ADDRESS_MODE_UNKNOWN;}
        void checkType(MsgAddress::AddressType t) const
        {
            if(t != _type)
                RsErr() << "WRONG TYPE in MsgAddress. This is not a good sign. Something's going wrong." ;
        }
        bool operator<(const MsgAddress& m) const { return _addr_string < m._addr_string; }
	private:
		AddressType _type ;
		AddressMode _mode ;
		std::string _addr_string ;
};

struct MessageInfo : RsSerializable
{
	MessageInfo(): msgflags(0), size(0), count(0), ts(0) {}

	std::string msgId;

    MsgAddress from;
    MsgAddress to;

    unsigned int msgflags;

    std::set<MsgAddress> destinations;

    std::string title;
    std::string msg;

    std::string attach_title;
    std::string attach_comment;
    std::list<FileInfo> files;

    int size;  /* total of files */
    int count; /* file count     */

    int ts;

	// RsSerializable interface
    void serial_process( RsGenericSerializer::SerializeJob j, RsGenericSerializer::SerializeContext &ctx ) override
	{
		RS_SERIAL_PROCESS(msgId);

        RS_SERIAL_PROCESS(from);
        RS_SERIAL_PROCESS(to);
        RS_SERIAL_PROCESS(destinations);
        RS_SERIAL_PROCESS(msgflags);

		RS_SERIAL_PROCESS(title);
		RS_SERIAL_PROCESS(msg);

		RS_SERIAL_PROCESS(attach_title);
		RS_SERIAL_PROCESS(attach_comment);
		RS_SERIAL_PROCESS(files);

		RS_SERIAL_PROCESS(size);
		RS_SERIAL_PROCESS(count);

		RS_SERIAL_PROCESS(ts);
	}

    ~MessageInfo() override = default;
};

typedef std::set<uint32_t> MsgTagInfo  ;

struct MsgInfoSummary : RsSerializable
{
	MsgInfoSummary() : msgflags(0), count(0), ts(0) {}

	RsMailMessageId msgId;
    MsgAddress from;
    MsgAddress to;						// specific address the message has been sent to (may be used for e.g. reply)

    uint32_t msgflags;		// combination of flags from rsmsgs.h (RS_MSG_OUTGOING, etc)
    MsgTagInfo msgtags;

	std::string title;
	int count; /** file count     */
	rstime_t ts;

    std::set<MsgAddress> destinations;  // all destinations of the message

	/// @see RsSerializable
	void serial_process(
	        RsGenericSerializer::SerializeJob j,
	        RsGenericSerializer::SerializeContext &ctx) override
	{
		RS_SERIAL_PROCESS(msgId);
        RS_SERIAL_PROCESS(from);
        RS_SERIAL_PROCESS(to);

		RS_SERIAL_PROCESS(msgflags);
        RS_SERIAL_PROCESS(msgtags);

		RS_SERIAL_PROCESS(title);
		RS_SERIAL_PROCESS(count);
		RS_SERIAL_PROCESS(ts);

        RS_SERIAL_PROCESS(destinations);
    }

    ~MsgInfoSummary() override = default;
};

//struct MsgTagInfo : RsSerializable
//{
//	virtual ~MsgTagInfo() = default;
//
//	std::string msgId;
//	std::set<uint32_t> tagIds;
//
//	// RsSerializable interface
//	void serial_process(RsGenericSerializer::SerializeJob j, RsGenericSerializer::SerializeContext &ctx) {
//		RS_SERIAL_PROCESS(msgId);
//		RS_SERIAL_PROCESS(tagIds);
//	}
//};

struct MsgTagType : RsSerializable
{
	virtual ~MsgTagType() = default;
	/* map containing tagId -> pair (text, rgb color) */
	std::map<uint32_t, std::pair<std::string, uint32_t> > types;

	// RsSerializable interface
	void serial_process(RsGenericSerializer::SerializeJob j, RsGenericSerializer::SerializeContext &ctx) {
		RS_SERIAL_PROCESS(types);
	}
};

} //namespace Rs
} //namespace Msgs

enum class RsMailStatusEventCode: uint8_t
{
    UNKNOWN                         = 0x00,
    NEW_MESSAGE                     = 0x01,
    MESSAGE_REMOVED                 = 0x02,
    MESSAGE_SENT                    = 0x03,

	/// means the peer received the message
    MESSAGE_RECEIVED_ACK            = 0x04,

	/// An error occurred attempting to sign the message
    SIGNATURE_FAILED                = 0x05,

    MESSAGE_CHANGED                 = 0x06,
    TAG_CHANGED                     = 0x07,
};

struct RsMailStatusEvent : RsEvent
{
	RsMailStatusEvent() : RsEvent(RsEventType::MAIL_STATUS) {}

    RsMailStatusEventCode mMailStatusEventCode;
	std::set<RsMailMessageId> mChangedMsgIds;

	/// @see RsEvent
	void serial_process( RsGenericSerializer::SerializeJob j,
	                     RsGenericSerializer::SerializeContext& ctx) override
	{
		RsEvent::serial_process(j, ctx);
		RS_SERIAL_PROCESS(mChangedMsgIds);
		RS_SERIAL_PROCESS(mMailStatusEventCode);
	}

	~RsMailStatusEvent() override = default;
};

enum class RsMailTagEventCode: uint8_t
{
	TAG_ADDED   = 0x00,
	TAG_CHANGED = 0x01,
	TAG_REMOVED = 0x02,
};

struct RsMailTagEvent : RsEvent
{
	RsMailTagEvent() : RsEvent(RsEventType::MAIL_TAG) {}

	RsMailTagEventCode mMailTagEventCode;
	std::set<std::string> mChangedMsgTagIds;

	/// @see RsEvent
	void serial_process( RsGenericSerializer::SerializeJob j,
	                     RsGenericSerializer::SerializeContext& ctx) override
	{
		RsEvent::serial_process(j, ctx);
		RS_SERIAL_PROCESS(mChangedMsgTagIds);
		RS_SERIAL_PROCESS(mMailTagEventCode);
	}

	~RsMailTagEvent() override = default;
};

// flags to define who we accept to talk to. Each flag *removes* some people.

#define RS_DISTANT_MESSAGING_CONTACT_PERMISSION_FLAG_FILTER_NONE           0x0000 
#define RS_DISTANT_MESSAGING_CONTACT_PERMISSION_FLAG_FILTER_NON_CONTACTS   0x0001 
#define RS_DISTANT_MESSAGING_CONTACT_PERMISSION_FLAG_FILTER_EVERYBODY      0x0002 

class RsMail;
/**
 * @brief Pointer to retroshare's message service
 * @jsonapi{development}
 */
extern RsMail *rsMail;

class RsMail
{
public:

	/**
	 * @brief getMessageSummaries
	 * @jsonapi{development}
     * @param[in]  box
     * @param[out] msgList
	 * @return always true
	 */
    virtual bool getMessageSummaries(Rs::Msgs::BoxName box,std::list<Rs::Msgs::MsgInfoSummary> &msgList) = 0;

	/**
	 * @brief getMessage
	 * @jsonapi{development}
	 * @param[in] msgId message ID to lookup
	 * @param[out] msg
	 * @return true on success
	 */
	virtual bool getMessage(const std::string &msgId, Rs::Msgs::MessageInfo &msg)  = 0;

	/**
	 * @brief sendMail
	 * @jsonapi{development}
	 * @param[in] from GXS id of the author
	 * @param[in] subject Mail subject
	 * @param[in] mailBody Mail body
	 * @param[in] to list of To: recipients
	 * @param[in] cc list of CC: recipients
	 * @param[in] bcc list of BCC: recipients
	 * @param[in] attachments list of suggested files
	 * @param[out] trackingIds storage for tracking ids for each sent mail
	 * @param[out] errorMsg error message if errors occurred, empty otherwise
	 * @return number of successfully sent mails
	 */
	virtual uint32_t sendMail(
	        const RsGxsId from,
	        const std::string& subject,
	        const std::string& mailBody,
	        const std::set<RsGxsId>& to = std::set<RsGxsId>(),
	        const std::set<RsGxsId>& cc = std::set<RsGxsId>(),
	        const std::set<RsGxsId>& bcc = std::set<RsGxsId>(),
	        const std::vector<FileInfo>& attachments = std::vector<FileInfo>(),
	        std::set<RsMailIdRecipientIdPair>& trackingIds =
	            RS_DEFAULT_STORAGE_PARAM(std::set<RsMailIdRecipientIdPair>),
	        std::string& errorMsg =
	            RS_DEFAULT_STORAGE_PARAM(std::string) ) = 0;

	/**
	 * @brief getMessageCount
	 * @jsonapi{development}
	 * @param[out] nInbox
	 * @param[out] nInboxNew
	 * @param[out] nOutbox
	 * @param[out] nDraftbox
	 * @param[out] nSentbox
	 * @param[out] nTrashbox
	 */
	virtual void getMessageCount(uint32_t &nInbox, uint32_t &nInboxNew, uint32_t &nOutbox, uint32_t &nDraftbox, uint32_t &nSentbox, uint32_t &nTrashbox) = 0;

	/**
	 * @brief SystemMessage
	 * @jsonapi{development}
	 * @param[in] title
	 * @param[in] message
	 * @param[in] systemFlag
	 * @return true on success
	 */
	virtual bool SystemMessage(const std::string &title, const std::string &message, uint32_t systemFlag) = 0;

	/**
	 * @brief MessageToDraft
	 * @jsonapi{development}
	 * @param[in] info
	 * @param[in] msgParentId
	 * @return true on success
	 */
	virtual bool MessageToDraft(Rs::Msgs::MessageInfo &info, const std::string &msgParentId) = 0;

	/**
	 * @brief MessageToTrash
	 * @jsonapi{development}
	 * @param[in] msgId        Id of the message to mode to trash box
	 * @param[in] bTrash       Move to trash if true, otherwise remove from trash
	 * @return true on success
	 */
	virtual bool MessageToTrash(const std::string &msgId, bool bTrash)   = 0;

	/**
	 * @brief getMsgParentId
	 * @jsonapi{development}
	 * @param[in] msgId
	 * @param[out] msgParentId
	 * @return true on success
	 */
	virtual bool getMsgParentId(const std::string &msgId, std::string &msgParentId) = 0;

	/**
	 * @brief MessageDelete
	 * @jsonapi{development}
	 * @param[in] msgId
	 * @return true on success
	 */
	virtual bool MessageDelete(const std::string &msgId)                 = 0;

	/**
	 * @brief MessageRead
	 * @jsonapi{development}
	 * @param[in] msgId
	 * @param[in] unreadByUser
	 * @return true on success
	 */
	virtual bool MessageRead(const std::string &msgId, bool unreadByUser) = 0;

	/**
	 * @brief MessageReplied
	 * @jsonapi{development}
	 * @param[in] msgId
	 * @param[in] replied
	 * @return true on success
	 */
	virtual bool MessageReplied(const std::string &msgId, bool replied) = 0;

	/**
	 * @brief MessageForwarded
	 * @jsonapi{development}
	 * @param[in] msgId
	 * @param[in] forwarded
	 * @return true on success
	 */
	virtual bool MessageForwarded(const std::string &msgId, bool forwarded) = 0;

	/**
	 * @brief MessageStar
	 * @jsonapi{development}
	 * @param[in] msgId
	 * @param[in] mark
	 * @return true on success
	 */
	virtual bool MessageStar(const std::string &msgId, bool mark) = 0;

	/**
	 * @brief MessageJunk
	 * @jsonapi{development}
	 * @param[in] msgId
	 * @param[in] mark
	 * @return true on success
	 */
	virtual bool MessageJunk(const std::string &msgId, bool mark) = 0;

	/**
	 * @brief MessageLoadEmbeddedImages
	 * @jsonapi{development}
	 * @param[in] msgId
	 * @param[in] load
	 * @return true on success
	 */
	virtual bool MessageLoadEmbeddedImages(const std::string &msgId, bool load) = 0;

	/* message tagging */
	/**
	 * @brief getMessageTagTypes
	 * @jsonapi{development}
	 * @param[out] tags
	 * @return always true
	 */
	virtual bool getMessageTagTypes(Rs::Msgs::MsgTagType& tags) = 0;

	/**
	 * @brief setMessageTagType
	 * @jsonapi{development}
	 * @param[in] tagId
	 * @param[in] text
	 * @param[in] rgb_color
	 * @return true on success
	 */
	virtual bool setMessageTagType(uint32_t tagId, std::string& text, uint32_t rgb_color) = 0;

	/**
	 * @brief removeMessageTagType
	 * @jsonapi{development}
	 * @param[in] tagId
	 * @return true on success
	 */
	virtual bool removeMessageTagType(uint32_t tagId) = 0;

	/**
	 * @brief getMessageTag
	 * @jsonapi{development}
	 * @param[in] msgId
	 * @param[out] info
	 * @return true on success
	 */
	virtual bool getMessageTag(const std::string &msgId, Rs::Msgs::MsgTagInfo& info) = 0;

	/**
	 * @brief setMessageTag
	 * @jsonapi{development}
	 * @param[in] msgId
     * @param[in] tagId	 TagID to set/unset. Use set == false && tagId == 0 remove all tags
     * @param[in] set
	 * @return true on success
	 */
	virtual bool setMessageTag(const std::string &msgId, uint32_t tagId, bool set) = 0;

	/**
	 * @brief resetMessageStandardTagTypes
	 * @jsonapi{development}
	 * @param[out] tags
	 * @return always true
	 */
	virtual bool resetMessageStandardTagTypes(Rs::Msgs::MsgTagType& tags) = 0;

	/****************************************/
	/*        Private distant messages      */
	/****************************************/

	/**
	 * @brief getDistantMessagingPermissionFlags get the distant messaging permission flags
	 * @jsonapi{development}
	 * @return distant messaging permission flags as a uint32_t.
	 */
  virtual uint32_t getDistantMessagingPermissionFlags() = 0 ;
	/**
	 * @brief setDistantMessagingPermissionFlags set the distant messaging permission flags
	 * @jsonapi{development}
	 * @param[in] flags
	 */
  virtual void setDistantMessagingPermissionFlags(uint32_t flags) = 0 ;

	/**
	 * @brief MessageSend
	 * @jsonapi{development}
	 * @param[in] info
	 * @return always true
	 */
	RS_DEPRECATED_FOR(sendMail)
	virtual bool MessageSend(Rs::Msgs::MessageInfo &info) = 0;

    virtual ~RsMail() = default;
};
