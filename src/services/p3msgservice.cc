/*******************************************************************************
 * libretroshare/src/services: p3msgservice.cc                                 *
 *                                                                             *
 * libretroshare: retroshare core library                                      *
 *                                                                             *
 * Copyright (C) 2004-2008 Robert Fernie <retroshare@lunamutt.com>             *
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

// Messaging system
// ================
//
//
//
// sendMail()
//     |
//     +---- for each to/cc --- sendDistantMessage(RsMsgItem *,GxsId from)  // sends from GxsId to GxsId
//     |                              |
// messageSend()                      +--- store in msgOutgoing[]
//     |                              |
//     +-----for each to/cc --- sendMessage(RsMsgItem *)					   // sends from node to node
//
// tick()
//   |
//   +----- checkOutgoingMessages()
//   |              |
//   |              +----- sendDistantMsgItem()
//   |                               |
//   |                               +-- p3Grouter::sendData()
//   |                               |
//   |                               +-- p3GxsTrans::sendData()
//   |
//   +----- manageDistantPeers()
//   |              |
//   |              +----- p3GRouter::register/unregisterKey()              // adds remove peers
//   |
//   +----- incomingMsg()
//   |         |
//   |        ...
//   |         |
//   |         +--- processIncomingMsg()
//   |                       |
//   |                       +--- store in mReceivedMessages[]
//   |                       |
//   |                       +--- store in mRecentlyReceivedMessageHashes[]
//   |
//   +----- cleanListOfReceivedMessageHashes()
//

#include "retroshare/rsiface.h"
#include "retroshare/rspeers.h"
#include "retroshare/rsidentity.h"

#include "pqi/pqibin.h"
#include "pqi/p3linkmgr.h"
#include "pqi/authgpg.h"
#include "pqi/p3cfgmgr.h"

#include "gxs/gxssecurity.h"

#include "services/p3idservice.h"
#include "services/p3msgservice.h"

#include "pgp/pgpkeyutil.h"
#include "rsserver/p3face.h"

#include "rsitems/rsconfigitems.h"

#include "grouter/p3grouter.h"
#include "grouter/groutertypes.h"

#include "util/rsdebug.h"
#include "util/rsdir.h"
#include "util/rsstring.h"
#include "util/radix64.h"
#include "util/rsrandom.h"
#include "util/rsmemory.h"
#include "util/rsprint.h"
#include "util/rsthreads.h"

#include <unistd.h>
#include <iomanip>
#include <map>
#include <sstream>

using namespace Rs::Msgs;

//#define DEBUG_DISTANT_MSG

/// keep msg hashes for 2 months to avoid re-sent msgs
static constexpr uint32_t RS_MSG_DISTANT_MESSAGE_HASH_KEEP_TIME = 2*30*86400;

/* Another little hack ..... unique message Ids
 * will be handled in this class.....
 * These are unique within this run of the server, 
 * and are not stored long term....
 *
 * Only 3 entry points:
 * (1) from network....
 * (2) from local send
 * (3) from storage...
 */

p3MsgService::p3MsgService( p3ServiceControl *sc, p3IdService *id_serv,
                            p3GxsTrans& gxsMS )
    : p3Service(), p3Config(),
      gxsOngoingMutex("p3MsgService Gxs Outgoing Mutex"), mIdService(id_serv),
      mServiceCtrl(sc), mMsgMtx("p3MsgService"),
      recentlyReceivedMutex("p3MsgService recently received hash mutex"),
      mGxsTransServ(gxsMS)
{
	/* this serialiser is used for services. It's not the same than the one
	 * returned by setupSerialiser(). We need both!! */
	_serialiser = new RsMsgSerialiser();
	addSerialType(_serialiser);

	/* MsgIds are not transmitted, but only used locally as a storage index.
	 * As such, thay do not need to be different at friends nodes. */

	mShouldEnableDistantMessaging = true;
	mDistantMessagingEnabled = false;
	mDistantMessagePermissions = RS_DISTANT_MESSAGING_CONTACT_PERMISSION_FLAG_FILTER_NONE;

	if(sc) initStandardTagTypes(); // Initialize standard tag types

    mGxsTransServ.registerGxsTransClient( GxsTransSubServices::P3_MSG_SERVICE, this );
}

const std::string MSG_APP_NAME = "msg";
const uint16_t MSG_APP_MAJOR_VERSION	= 	1;
const uint16_t MSG_APP_MINOR_VERSION  = 	0;
const uint16_t MSG_MIN_MAJOR_VERSION  = 	1;
const uint16_t MSG_MIN_MINOR_VERSION	=	0;

RsServiceInfo p3MsgService::getServiceInfo()
{
	return RsServiceInfo(RS_SERVICE_TYPE_MSG, 
		MSG_APP_NAME,
		MSG_APP_MAJOR_VERSION, 
		MSG_APP_MINOR_VERSION, 
		MSG_MIN_MAJOR_VERSION, 
		MSG_MIN_MINOR_VERSION);
}

p3MsgService::~p3MsgService()
{
    RS_STACK_MUTEX(mMsgMtx); /********** STACK LOCKED MTX ******/

    for(auto tag:mTags)             delete tag.second;
    for(auto img:mReceivedMessages) delete img.second;
    for(auto img:mSentMessages)     delete img.second;

    for(auto mpend:_pendingPartialIncomingMessages) delete mpend.second;
}

uint32_t p3MsgService::getNewUniqueMsgId()
{
	RS_STACK_MUTEX(mMsgMtx); /********** STACK LOCKED MTX ******/

    uint32_t res;

    do { res = RsRandom::random_u32(); } while(mAllMessageIds.find(res)!= mAllMessageIds.end());

    mAllMessageIds.insert(res);
    return res;
}

int p3MsgService::tick()
{
	/* don't worry about increasing tick rate! 
	 * (handled by p3service)
	 */

	incomingMsgs(); 

	static rstime_t last_management_time = 0 ;
	rstime_t now = time(NULL) ;

	if(now > last_management_time + 5)
	{
		manageDistantPeers();
		checkOutgoingMessages();
		cleanListOfReceivedMessageHashes();

		last_management_time = now;
#ifdef DEBUG_DISTANT_MSG
        debug_dump();
#endif
	}

	return 0;
}

void p3MsgService::cleanListOfReceivedMessageHashes()
{
	RS_STACK_MUTEX(recentlyReceivedMutex);

	rstime_t now = time(nullptr);

	for( auto it = mRecentlyReceivedMessageHashes.begin();
	     it != mRecentlyReceivedMessageHashes.end(); )
		if( now > RS_MSG_DISTANT_MESSAGE_HASH_KEEP_TIME + it->second )
		{
			std::cerr << "p3MsgService(): cleanListOfReceivedMessageHashes(). "
			          << "Removing old hash " << it->first << ", aged "
			          << now - it->second << " secs ago" << std::endl;

			it = mRecentlyReceivedMessageHashes.erase(it);
		}
		else ++it;
}

void p3MsgService::processIncomingMsg(RsMsgItem *mi,const MsgAddress& from,const MsgAddress& to)
{
	mi -> recvTime = static_cast<uint32_t>(time(nullptr));
	mi -> msgId = getNewUniqueMsgId();

	{
		RS_STACK_MUTEX(mMsgMtx);

		/* from a peer */

		mi->msgFlags &= (RS_MSG_FLAGS_DISTANT | RS_MSG_FLAGS_SYSTEM); // remove flags except those
		mi->msgFlags |= RS_MSG_FLAGS_NEW;

		if (rsEvents)
		{
			auto ev = std::make_shared<RsMailStatusEvent>();
			ev->mMailStatusEventCode = RsMailStatusEventCode::NEW_MESSAGE;
			ev->mChangedMsgIds.insert(std::to_string(mi->msgId));
			rsEvents->postEvent(ev);
		}

        RsMailStorageItem * msi = new RsMailStorageItem;
        msi->msg = *mi;
        msi->from = from;
        msi->to = to;

        mReceivedMessages[mi->msgId] = msi;

		IndicateConfigChanged(RsConfigMgr::CheckPriority::SAVE_NOW); /**** INDICATE MSG CONFIG CHANGED! *****/

		/**** STACK UNLOCKED ***/
	}

    // If the peer is allowed to push files, then auto-download the recommended files.

    RsIdentityDetails id_details;
    if(rsIdentity->getIdDetails(RsGxsId(mi->PeerId()),id_details) && !id_details.mPgpId.isNull() && (rsPeers->servicePermissionFlags(id_details.mPgpId) & RS_NODE_PERM_ALLOW_PUSH))
    {
        std::list<RsPeerId> srcIds;
        srcIds.push_back(mi->PeerId());

        for(std::list<RsTlvFileItem>::const_iterator it(mi->attachment.items.begin());it!=mi->attachment.items.end();++it)
            rsFiles->FileRequest((*it).name,(*it).hash,(*it).filesize,std::string(),RS_FILE_REQ_ANONYMOUS_ROUTING,srcIds) ;
    }
}

bool p3MsgService::checkAndRebuildPartialMessage(RsMsgItem *ci)
{
	// Check is the item is ending an incomplete item.
	//
    std::map<RsPeerId,RsMsgItem*>::iterator it = _pendingPartialIncomingMessages.find(ci->PeerId()) ;

	bool ci_is_partial = ci->msgFlags & RS_MSG_FLAGS_PARTIAL ;

    if(it != _pendingPartialIncomingMessages.end())
	{
#ifdef MSG_DEBUG
		std::cerr << "Pending message found. Appending it." << std::endl;
#endif
		// Yes, there is. Append the item to ci.

		ci->message = it->second->message + ci->message ;
		ci->msgFlags |= it->second->msgFlags ;

		delete it->second ;

		if(!ci_is_partial)
            _pendingPartialIncomingMessages.erase(it) ;
	}

	if(ci_is_partial)
	{
#ifdef MSG_DEBUG
		std::cerr << "Message is partial, storing for later." << std::endl;
#endif
		// The item is a partial message. Push it, and wait for the rest.
		//
        _pendingPartialIncomingMessages[ci->PeerId()] = ci ;
		return false ;
	}
	else
	{
#ifdef MSG_DEBUG
		std::cerr << "Message is complete, using it now." << std::endl;
#endif
		return true ;
	}
}

int p3MsgService::incomingMsgs()	// direct node-to-node messages
{
	RsMsgItem *mi;
	int i = 0;

	while((mi = (RsMsgItem *) recvItem()) != NULL)
	{
        handleIncomingItem(mi,
                           Rs::Msgs::MsgAddress(mi->PeerId(),            Rs::Msgs::MsgAddress::MSG_ADDRESS_MODE_TO),
                           Rs::Msgs::MsgAddress(mServiceCtrl->getOwnId(),Rs::Msgs::MsgAddress::MSG_ADDRESS_MODE_TO));
        ++i ;
	}

	return i;
}

void p3MsgService::handleIncomingItem(RsMsgItem *mi,const Rs::Msgs::MsgAddress& from,const Rs::Msgs::MsgAddress& to)
{
	// only returns true when a msg is complete.
	if(checkAndRebuildPartialMessage(mi))
	{
        processIncomingMsg(mi,from,to);
        delete mi;
    }
}

void    p3MsgService::statusChange(const std::list<pqiServicePeer> &plist)
{
	/* should do it properly! */
	/* only do this when a new peer is connected */
	bool newPeers = false;
	std::list<pqiServicePeer>::const_iterator it;
	for(it = plist.begin(); it != plist.end(); ++it)
	{
		if (it->actions & RS_SERVICE_PEER_CONNECTED)
		{
			newPeers = true;
		}
	}

	if (newPeers)
		checkOutgoingMessages();
}

void p3MsgService::checkSizeAndSendMessage(RsMsgItem *msg,const RsPeerId& destination)
{
	// We check the message item, and possibly split it into multiple messages, if the message is too big.

    msg->PeerId(destination);
	static const uint32_t MAX_STRING_SIZE = 15000 ;

    std::cerr << "Msg is size " << msg->message.size() << std::endl;

	while(msg->message.size() > MAX_STRING_SIZE)
	{
		// chop off the first 15000 wchars

		RsMsgItem *item = new RsMsgItem(*msg) ;

		item->message = item->message.substr(0,MAX_STRING_SIZE) ;
		msg->message = msg->message.substr(MAX_STRING_SIZE,msg->message.size()-MAX_STRING_SIZE) ;

#ifdef DEBUG_DISTANT_MSG
		std::cerr << "  Chopped off msg of size " << item->message.size() << std::endl;
#endif

		// Indicate that the message is to be continued.
		//
		item->msgFlags |= RS_MSG_FLAGS_PARTIAL ;
        sendItem(item) ;
	}
#ifdef DEBUG_DISTANT_MSG
	std::cerr << "  Chopped off msg of size " << msg->message.size() << std::endl;
#endif

    sendItem(msg) ;
}

int p3MsgService::checkOutgoingMessages()
{
    auto pEvent = std::make_shared<RsMailStatusEvent>();
    pEvent->mMailStatusEventCode = RsMailStatusEventCode::MESSAGE_SENT;

    {
        RS_STACK_MUTEX(mMsgMtx); /********** STACK LOCKED MTX ******/

        const RsPeerId& ownId = mServiceCtrl->getOwnId();

        std::list<uint32_t>::iterator it;
        std::list<uint32_t> toErase;

        for(auto mit = msgOutgoing.begin();mit!= msgOutgoing.end();)
        {
            // 1 - find the original message this entry refers to.

            auto message_data_identifier = mit->first;
            auto sit = mSentMessages.find(message_data_identifier);

            if(sit == mSentMessages.end())
            {
                RsErr() << "Cannot find original copy of message to be sent: id=" << message_data_identifier << ", removing all outgoing messages." ;

                auto tmp = mit;
                ++tmp;
                msgOutgoing.erase(mit);
                mit = tmp;

                continue;
            }

            // 2 - for each copy (i.e. destination), update the status, send, etc.

            for(auto fit=mit->second.begin();fit!=mit->second.end();)
            {
                auto& minfo(fit->second);	// MessageOutgoingInfo

                MsgAddress to(minfo.destination);
                MsgAddress from(minfo.origin);

                if( to.type()==MsgAddress::MSG_ADDRESS_TYPE_RSPEERID )
                {
                    if(to.toRsPeerId() == ownId || mServiceCtrl->isPeerConnected(getServiceInfo().mServiceType, to.toRsPeerId()) )
                    {
                        auto msg_item = createOutgoingMessageItem(*sit->second,to);

                        // Use the msg_id of the outgoing message copy.
                        msg_item->msgId = mit->first;

                        Dbg3() << __PRETTY_FUNCTION__ << " Sending out message" << std::endl;
                        checkSizeAndSendMessage(msg_item,to.toRsPeerId());

                        pEvent->mChangedMsgIds.insert(std::to_string(mit->first));

                        // now remove the entry

                        auto tmp = fit;
                        ++tmp;
                        mit->second.erase(fit);
                        fit = tmp;

                        continue;
                    }
                    else
                    {
#ifdef DEBUG_DISTANT_MSG
                        Dbg3() << __PRETTY_FUNCTION__ << " Delaying until available..." << std::endl;
#endif
                        ++fit;
                        continue;
                    }
                }
                else  if( to.type()==MsgAddress::MSG_ADDRESS_TYPE_RSGXSID && !(minfo.flags & RS_MSG_FLAGS_ROUTED))
                {
                    minfo.flags |= RS_MSG_FLAGS_ROUTED;
                    minfo.flags |= RS_MSG_FLAGS_DISTANT;

#ifdef DEBUG_DISTANT_MSG
                    RsDbg() << "Message id " << mit->first << " is distant: kept in outgoing, and marked as ROUTED" << std::endl;
#endif
                    Dbg3() << __PRETTY_FUNCTION__ << " Sending out message" << std::endl;
                    auto msg_item = createOutgoingMessageItem(*sit->second,to);

                    // Use the msg_id of the outgoing message copy.
                    msg_item->msgId = mit->first;
                    locked_sendDistantMsgItem(msg_item,from.toGxsId(),fit->first);
                    pEvent->mChangedMsgIds.insert(std::to_string(mit->first));

                    // Check if the msg is sent to ourselves. It happens that GRouter/GxsMail do not
                    // acknowledge receipt of these messages. If the msg is not routed, then it's received.

                    if(rsIdentity->isOwnId(to.toGxsId()))
                    {
                         auto tmp = fit;
                        ++tmp;
                        mit->second.erase(fit);
                        fit = tmp;
                        continue;
                    }
                    else
                        ++fit;
                }
                else
                    ++fit;
            }

            // cleanup.

            if(mit->second.empty())
            {
                sit->second->msg.msgFlags &= ~RS_MSG_FLAGS_PENDING;
                auto tmp = mit;
                ++tmp;
                msgOutgoing.erase(mit);
                mit=tmp;
            }
            else
                ++mit;
        }
    }

    if(rsEvents && !pEvent->mChangedMsgIds.empty())
        rsEvents->postEvent(pEvent);

    return 0;
}

bool p3MsgService::saveList(bool& cleanup, std::list<RsItem*>& itemList)
{
	RsMsgGRouterMap* gxsmailmap = new RsMsgGRouterMap;
	{
		RS_STACK_MUTEX(gxsOngoingMutex);
		gxsmailmap->ongoing_msgs = gxsOngoingMessages;
	}
	itemList.push_front(gxsmailmap);

	cleanup = true;

	mMsgMtx.lock();

    for(auto mit:mReceivedMessages) itemList.push_back(new RsMailStorageItem(*mit.second));
    for(auto mit:mSentMessages)     itemList.push_back(new RsMailStorageItem(*mit.second));
    for(auto mit:mTrashMessages)    itemList.push_back(new RsMailStorageItem(*mit.second));
    for(auto mit:mDraftMessages)    itemList.push_back(new RsMailStorageItem(*mit.second));

    RsMsgOutgoingMapStorageItem *out_map_item = new RsMsgOutgoingMapStorageItem ;
    out_map_item->outgoing_map = msgOutgoing;
    itemList.push_back(out_map_item);

    for(auto mit2:mTags)
        itemList.push_back(new RsMsgTagType(*mit2.second));

    RsMsgGRouterMap *grmap = new RsMsgGRouterMap ;
    grmap->ongoing_msgs = _grouter_ongoing_messages ;

    itemList.push_back(grmap) ;

	RsMsgDistantMessagesHashMap *ghm = new RsMsgDistantMessagesHashMap;
	{
		RS_STACK_MUTEX(recentlyReceivedMutex);
		ghm->hash_map = mRecentlyReceivedMessageHashes;
	}
	itemList.push_back(ghm);

	RsConfigKeyValueSet *vitem = new RsConfigKeyValueSet ;
	RsTlvKeyValue kv;
	kv.key = "DISTANT_MESSAGES_ENABLED" ;
    kv.value = mShouldEnableDistantMessaging?"YES":"NO" ;
	vitem->tlvkvs.pairs.push_back(kv) ;
    
    kv.key = "DISTANT_MESSAGE_PERMISSION_FLAGS" ;
    kv.value = RsUtil::NumberToString(mDistantMessagePermissions) ;
	vitem->tlvkvs.pairs.push_back(kv) ;

	itemList.push_back(vitem);

	return true;
}

void p3MsgService::saveDone()
{
	// unlocks mutex which has been locked by savelist
	mMsgMtx.unlock();
}

RsSerialiser* p3MsgService::setupSerialiser()	// this serialiser is used for config. So it adds somemore info in the serialised items
{
	RsSerialiser *rss = new RsSerialiser ;

	rss->addSerialType(new RsMsgSerialiser(RsSerializationFlags::CONFIG));
	rss->addSerialType(new RsGeneralConfigSerialiser());

	return rss;
}

// build list of standard tag types
static void getStandardTagTypes(MsgTagType &tags)
{
	/* create standard tag types, the text must be translated in the GUI */
	tags.types [RS_MSGTAGTYPE_IMPORTANT] = std::pair<std::string, uint32_t> ("Important", 0xFF0000);
	tags.types [RS_MSGTAGTYPE_WORK]      = std::pair<std::string, uint32_t> ("Work",      0xFF9900);
	tags.types [RS_MSGTAGTYPE_PERSONAL]  = std::pair<std::string, uint32_t> ("Personal",  0x009900);
	tags.types [RS_MSGTAGTYPE_TODO]      = std::pair<std::string, uint32_t> ("Todo",      0x3333FF);
	tags.types [RS_MSGTAGTYPE_LATER]     = std::pair<std::string, uint32_t> ("Later",     0x993399);
}

// Initialize the standard tag types after load
void p3MsgService::initStandardTagTypes()
{
	bool bChanged = false;
    const RsPeerId& ownId = mServiceCtrl->getOwnId();

	MsgTagType tags;
	getStandardTagTypes(tags);

	std::map<uint32_t, std::pair<std::string, uint32_t> >::iterator tit;
	for (tit = tags.types.begin(); tit != tags.types.end(); ++tit) {
		std::map<uint32_t, RsMsgTagType*>::iterator mit = mTags.find(tit->first);
		if (mit == mTags.end()) {
			RsMsgTagType* tagType = new RsMsgTagType();
            tagType->PeerId (ownId);
			tagType->tagId = tit->first;
			tagType->text = tit->second.first;
			tagType->rgb_color = tit->second.second;

			mTags.insert(std::pair<uint32_t, RsMsgTagType*>(tit->first, tagType));

			bChanged = true;
		}
	}

	if (bChanged) {
		IndicateConfigChanged(RsConfigMgr::CheckPriority::SAVE_NOW); /**** INDICATE MSG CONFIG CHANGED! *****/
	}
}

bool p3MsgService::parseList_backwardCompatibility(std::list<RsItem*>& load)
{
    if(!load.empty())
        RsInfo() << "p3MsgService: Loading messages with old format. " ;

    // 1 - load all old-format data pieces

    std::map<uint32_t,RsMailStorageItem*> msg_map;
    std::list<RsMsgTags *> msg_tags;
    std::list<RsMsgSrcId *> msg_srcids;
    std::list<RsMsgParentId *> msg_parentids;

    for(auto it:load)
    {
        RsMsgTags* mti;
        RsMsgSrcId* msi;
        RsMsgParentId* msp;
        RsMsgItem *mitem;

        if (nullptr != (mitem = dynamic_cast<RsMsgItem *>(it)))
        {
            auto msi = new RsMailStorageItem();
            msi->msg = *mitem;
            msg_map[mitem->msgId] = msi;
        }
        else if(nullptr != (mti = dynamic_cast<RsMsgTags *>(it)))
            msg_tags.push_back(mti);
        else if(nullptr != (msi = dynamic_cast<RsMsgSrcId *>(it)))
            msg_srcids.push_back(msi);
        else if(nullptr != (msp = dynamic_cast<RsMsgParentId *>(it)))
            msg_parentids.push_back(msp);
    }

    RsInfo() << "  Current Msg map:" ;
    for(auto m:msg_map)
        RsInfo() << "    id=" << m.first << "  pointer=" << m.second ;

    // 2 - process all tags and set them to the proper message

    for(auto ptag:msg_tags)
    {
        auto mit = msg_map.find(ptag->msgId);

        std::string tagstr;
        for(auto t:ptag->tagIds) tagstr += std::to_string(t) + ",";

        if(!tagstr.empty())
            tagstr.pop_back();

        if(mit == msg_map.end())
        {
            RsErr() << "Found message tag (msg=" << ptag->msgId << ", tag=" << tagstr << ") that belongs to no specific message";
            continue;
        }
        RsInfo() << "  Loading msg tag pair (msg=" << ptag->msgId << ", tag=" << tagstr << ")" ;

        mit->second->tagIds = std::set<uint32_t>(ptag->tagIds.begin(),ptag->tagIds.end());
    }

    // 3 - process all parent ids and set them to the proper message

    for(auto pparent:msg_parentids)
    {
        auto mit = msg_map.find(pparent->msgId);

        if(mit == msg_map.end())
        {
            RsErr() << "Found message parent (msg=" << pparent->msgId << ", parent=" << pparent->msgParentId << ") that belongs to no specific message";
            continue;
        }
        auto mit2 = msg_map.find(pparent->msgParentId);

        if(mit2 == msg_map.end())
        {
            RsErr() << "Found message parent (msg=" << pparent->msgId << ", parent=" << pparent->msgParentId << ") that refers to an unknown parent message";
            continue;
        }
        RsInfo() << "  Loading parent id pair (msg=" << pparent->msgId << ", parent=" << pparent->msgParentId << ") ";

        mit->second->parentId = pparent->msgParentId;
    }

    // 3 - process all parent ids and set them to the proper message

    for(auto psrc:msg_srcids)
    {
        auto mit = msg_map.find(psrc->msgId);

        if(mit == msg_map.end())
        {
            RsErr() << "Found message parent (msg=" << psrc->msgId << ", src_id=" << psrc->srcId << ") that belongs to no specific message";
            continue;
        }
        RsErr() << "  Loaded msg source pair (msg=" << psrc->msgId << ", src_id=" << psrc->srcId << ")";

        if(mit->second->msg.msgFlags & RS_MSG_FLAGS_DISTANT)
            mit->second->from = Rs::Msgs::MsgAddress(RsGxsId(psrc->srcId),Rs::Msgs::MsgAddress::MSG_ADDRESS_MODE_TO);
        else
            mit->second->from = Rs::Msgs::MsgAddress(psrc->srcId,Rs::Msgs::MsgAddress::MSG_ADDRESS_MODE_TO);
    }
    // 4 - store each message in the appropriate map.

    std::list<RsMailStorageItem*> pending_msg;

    for(auto mit:msg_map)
    {
        // Early detect "outgoing" list, and keep them for later.

        if (mit.second->msg.msgFlags & RS_MSG_FLAGS_PENDING)
        {
            RsInfo() << "Ignoring pending message " << mit.first << " as the destination of pending msgs is not saved in old format.";
            continue;
        }

        // Fix up destination. Try to guess it, as it wasn't actually stored originally.

        if(mit.second->msg.msgFlags & RS_MSG_FLAGS_DISTANT)
        {
            for(auto d:mit.second->msg.rsgxsid_msgto.ids)
                if(rsIdentity->isOwnId(d))
                {
                    mit.second->to = MsgAddress(d,Rs::Msgs::MsgAddress::MSG_ADDRESS_MODE_TO);
                    break;
                }
            for(auto d:mit.second->msg.rsgxsid_msgcc.ids)
                if(rsIdentity->isOwnId(d))
                {
                    mit.second->to = MsgAddress(d,Rs::Msgs::MsgAddress::MSG_ADDRESS_MODE_CC);
                    break;
                }
            for(auto d:mit.second->msg.rsgxsid_msgbcc.ids)
                if(rsIdentity->isOwnId(d))
                {
                    mit.second->to = MsgAddress(d,Rs::Msgs::MsgAddress::MSG_ADDRESS_MODE_BCC);
                    break;
                }
        }
        else
        {
            if(mit.second->msg.rspeerid_msgto.ids.find(rsPeers->getOwnId()) != mit.second->msg.rspeerid_msgto.ids.end())
                mit.second->to = MsgAddress(rsPeers->getOwnId(),Rs::Msgs::MsgAddress::MSG_ADDRESS_MODE_TO);
            else if(mit.second->msg.rspeerid_msgcc.ids.find(rsPeers->getOwnId()) != mit.second->msg.rspeerid_msgcc.ids.end())
                mit.second->to = MsgAddress(rsPeers->getOwnId(),Rs::Msgs::MsgAddress::MSG_ADDRESS_MODE_CC);
            else
                mit.second->to = MsgAddress(rsPeers->getOwnId(),Rs::Msgs::MsgAddress::MSG_ADDRESS_MODE_BCC);
        }

        RsInfo() << "  Storing message " << mit.first << ", possible destination: " << mit.second->to  << ", MsgFlags: " << std::hex << mit.second->msg.msgFlags << std::dec ;

        if(mit.second->msg.msgFlags & RS_MSG_FLAGS_TRASH)
            mTrashMessages.insert(mit);
        else if (mit.second->msg.msgFlags & RS_MSG_FLAGS_DRAFT)
            mDraftMessages.insert(mit);
        else if (mit.second->msg.msgFlags & RS_MSG_FLAGS_OUTGOING)
            mSentMessages.insert(mit);
        else
            mReceivedMessages.insert(mit);
    }

    return true;
}

bool p3MsgService::loadList(std::list<RsItem*>& load)
{
    RS_STACK_MUTEX(mMsgMtx);	// lock ere, because we need to load, then check for duplicates, and this needs to be done in the same lock.

    auto gxsmIt = load.begin();
	RsMsgGRouterMap* gxsmailmap = dynamic_cast<RsMsgGRouterMap*>(*gxsmIt);
	if(gxsmailmap)
	{
		{
			RS_STACK_MUTEX(gxsOngoingMutex);
			gxsOngoingMessages = gxsmailmap->ongoing_msgs;
		}
		delete *gxsmIt; load.erase(gxsmIt);
	}

    std::list<RsItem*> unhandled_items;
    
    // load items and calculate next unique msgId
    for(auto it = load.begin(); it != load.end(); ++it)
    {
        RsConfigKeyValueSet *vitem = nullptr ;

        RsMsgTagType* mtt;
        RsMsgGRouterMap* grm;
        RsMsgDistantMessagesHashMap *ghm;
        RsMailStorageItem *msi;
        RsMsgOutgoingMapStorageItem *mom;

        if (NULL != (grm = dynamic_cast<RsMsgGRouterMap *>(*it)))
        {
            typedef std::map<GRouterMsgPropagationId,uint32_t> tT;
            for( tT::const_iterator bit = grm->ongoing_msgs.begin(); bit != grm->ongoing_msgs.end(); ++bit )
                _grouter_ongoing_messages.insert(*bit);

            delete *it;
        }
        else if(NULL != (ghm = dynamic_cast<RsMsgDistantMessagesHashMap*>(*it)))
        {
            {
                RS_STACK_MUTEX(recentlyReceivedMutex);
                mRecentlyReceivedMessageHashes = ghm->hash_map;
            }
#ifdef DEBUG_DISTANT_MSG
            std::cerr << "  loaded recently received message map: " << std::endl;

            for(std::map<Sha1CheckSum,uint32_t>::const_iterator it(mRecentlyReceivedMessageHashes.begin());it!=mRecentlyReceivedMessageHashes.end();++it)
                std::cerr << "    " << it->first << " received " << time(NULL)-it->second << " secs ago." << std::endl;
#endif
            delete *it;
        }
        else if(NULL != (mtt = dynamic_cast<RsMsgTagType *>(*it)))
        {
            // delete standard tags as they are now save in config
            std::map<uint32_t,RsMsgTagType*>::const_iterator tagIt;

            if(mTags.end() == (tagIt = mTags.find(mtt->tagId)))
                mTags.insert(std::pair<uint32_t, RsMsgTagType* >(mtt->tagId, mtt));
            else
            {
                delete mTags[mtt->tagId];
                mTags.erase(tagIt);
                mTags.insert(std::pair<uint32_t, RsMsgTagType* >(mtt->tagId, mtt));
            }
            // no delete here because the item is stored.
        }
        else if(NULL != (vitem = dynamic_cast<RsConfigKeyValueSet*>(*it)))
        {
            for(std::list<RsTlvKeyValue>::const_iterator kit = vitem->tlvkvs.pairs.begin(); kit != vitem->tlvkvs.pairs.end(); ++kit)
            {
                if(kit->key == "DISTANT_MESSAGES_ENABLED")
                {
#ifdef MSG_DEBUG
                    std::cerr << "Loaded config default nick name for distant chat: " << kit->value << std::endl ;
#endif
                    mShouldEnableDistantMessaging = (kit->value == "YES") ;
                }
                if(kit->key == "DISTANT_MESSAGE_PERMISSION_FLAGS")
                {
#ifdef MSG_DEBUG
                    std::cerr << "Loaded distant message permission flags: " << kit->value << std::endl ;
#endif
                    if (!kit->value.empty())
                    {
                        std::istringstream is(kit->value) ;

                        uint32_t tmp ;
                        is >> tmp ;

                        if(tmp < 3)
                            mDistantMessagePermissions = tmp ;
                        else
                            std::cerr << "(EE) Invalid value read for DistantMessagePermission flags in config: " << tmp << std::endl;
                    }
                }
            }
            delete *it;
        }
        else if(nullptr != (msi = dynamic_cast<RsMailStorageItem*>(*it)))
        {
            RsErr() << "Loaded msg with msg.to=" << msi->to ;

            /* STORE MsgID */
            if (msi->msg.msgId != 0)
            {

                /* switch depending on the PENDING
                 * flags
                 */
                if (msi->msg.msgFlags & RS_MSG_FLAGS_TRASH)
                    mTrashMessages[msi->msg.msgId] = msi;
                else if (msi->msg.msgFlags & RS_MSG_FLAGS_OUTGOING)
                    mSentMessages[msi->msg.msgId] = msi;
                else if (msi->msg.msgFlags & RS_MSG_FLAGS_DRAFT)
                    mDraftMessages[msi->msg.msgId] = msi;
                else
                    mReceivedMessages[msi->msg.msgId] = msi;
            }
            else
            {
                RsErr() << "Found Message item without an ID. This is an error. Item will be dropped." ;
                delete *it;
            }

            // no delete here because the item is stored.
        }
        else if(nullptr != (mom = dynamic_cast<RsMsgOutgoingMapStorageItem*>(*it)))
        {
            msgOutgoing = mom->outgoing_map;
            delete *it;
        }
        else
            unhandled_items.push_back(*it);
    }

    parseList_backwardCompatibility(unhandled_items);

    // clean up

    for(auto m:unhandled_items)
        delete m;

    load.clear();

#ifdef MSG_DEBUG
    // list all the msg Ids
    auto print_msgids = [](const std::map<uint32_t,RsMailStorageItem*>& mp,const std::string& name) {
        std::cerr << "Message ids in box " << name << " : " << std::endl;
        for(auto it:mp)
            std::cerr << "  " << it.first << "  " << it.second->msg.msgId << std::endl;
    };
    print_msgids(mSentMessages,"Sent");
    print_msgids(mTrashMessages,"Trash");
    print_msgids(mDraftMessages,"Drafts");
    print_msgids(mReceivedMessages,"Received");

    std::cerr << "Outgoing messages: " << std::endl;
    for(auto m:msgOutgoing)
    {
        std::cerr << "  parent " << m.first << " : " << std::endl;
        for(auto p:m.second)
            std::cerr << "    " << p.first << std::endl;
    }
#endif

    // This was added on Sept 20, 2024. It is here to fix errors following a bug that caused duplication of
    // some message ids. This should be kept because it also creates the list that is stored in mAllMessageIds,
    // that is further used by getNewUniqueId() to create unique message Ids in a more robust way than before.

    locked_checkForDuplicates();
    return true;
}

// Two generic methods to replace elements in a map, when the first (resp. second) element matches an id to substitute.

template<class T> void replace_first(std::map<uint32_t,T>& mp,uint32_t old_id,uint32_t new_id)
{
    auto tt = mp.find(old_id);
    if(tt == mp.end())
        return;

    auto sec = tt->second;
    mp.erase(tt);
    mp[new_id] = sec;
}

template<class T> void replace_second(std::map<T,uint32_t>& mp,uint32_t old_id,uint32_t new_id)
{
    for(auto& it:mp)
        if(it.second == old_id)
            it.second = new_id;
}
void p3MsgService::locked_checkForDuplicates()
{
    std::set<uint32_t> already_known_ids;
    std::set<RsMailMessageId> changed_msg_ids;

    auto replace_parent = [](std::map<uint32_t,RsMailStorageItem*>& mp,uint32_t old_id,uint32_t new_id)
    {
        for(auto& it:mp)
            if(it.second->parentId == old_id)
            {
                RsWarn() << "Replacing parent ID " << old_id << " of message " << it.first << " with new parent " << new_id << std::endl;
                it.second->parentId = new_id;
            }
    };

    auto check = [&already_known_ids,&changed_msg_ids,this,replace_parent](std::map<uint32_t,RsMailStorageItem*>& mp,const std::string& name)
    {
        std::map<uint32_t,RsMailStorageItem*> new_mp;

        for(std::map<uint32_t,RsMailStorageItem*>::iterator it(mp.begin());it!=mp.end();)
        {
            if(already_known_ids.find(it->first)!=already_known_ids.end())
            {
                // generate a new ID
                uint32_t old_id = it->first;
                uint32_t new_id;
                do { new_id = RsRandom::random_u32() ; } while(already_known_ids.find(new_id)!=already_known_ids.end());

                already_known_ids.insert(new_id);
                changed_msg_ids.insert(std::to_string(new_id));

                RsWarn() << "Duplicate ID " << it->first << " found in message box " << name << ". Will be replaced by new ID " << new_id << std::endl;

                // replace the old ID by the new, everywhere

                // 1 - in the map itself

                it->second->msg.msgId = new_id;
                new_mp[new_id] = it->second;	// put the modified item in a new map, so as not to have the same item visited twice in this loop.

                auto tmp = it;	// remove the item from the map
                tmp++;
                mp.erase(it);
                it = tmp;

                // Next, we replace the old id by the new, everywhere it is mentionned. Of course this may not be correct, since
                // the actual old id may be mentionned on purpose. Still, there is absolutely no way to know which is the right one.

                // 2 - everywhere it is designated as parent

                replace_parent(mTrashMessages,   old_id,new_id);
                replace_parent(mSentMessages,    old_id,new_id);
                replace_parent(mDraftMessages,   old_id,new_id);
                replace_parent(mReceivedMessages,old_id,new_id);

                // 3 - mMsgOutgoing refers to original msg in Sent, so the substitution must happen there too

                replace_first(msgOutgoing,old_id,new_id);

                // 4 - in GRouter and GxsTrans correspondance maps, and recently received messages

                replace_second(_grouter_ongoing_messages     ,old_id,new_id);
                replace_second(gxsOngoingMessages            ,old_id,new_id);
                replace_second(mRecentlyReceivedMessageHashes,old_id,new_id);

                // 6 - in mParentId correspondance map

                replace_first(mParentId,old_id,new_id);
            }
            else
                ++it;

            already_known_ids.insert(it->first);
        }
        mp.insert(new_mp.begin(),new_mp.end());	// merge back the new list in the modified one
    };

    check(mTrashMessages,"mTrashMessages");
    check(mSentMessages,"mSentMessages");
    check(mDraftMessages,"mDraftMessages");
    check(mReceivedMessages,"mReceivedMessages");

    // now check msgOutgoing. The first element refers to an element in mSentMessages, so it's already been treated

    for(auto& it:msgOutgoing)
    {
        std::map<uint32_t,uint32_t> to_switch;

        for(auto sit:it.second)
            if(already_known_ids.find(sit.first) != already_known_ids.end())
            {
                uint32_t new_id;
                do { new_id = RsRandom::random_u32() ; } while(already_known_ids.find(new_id)!=already_known_ids.end());

                RsWarn() << "Duplicate ID " << sit.first << " found in msgOutgoing. Will be replaced by new ID " << new_id << std::endl;

                to_switch[sit.first] = new_id;
                changed_msg_ids.insert(std::to_string(new_id));
                already_known_ids.insert(new_id);
            }
            else
                already_known_ids.insert(sit.first);

        for(auto sit:to_switch)
            replace_first(it.second,sit.first,sit.second);
    }

    mAllMessageIds = already_known_ids;

    if(!changed_msg_ids.empty())
    {
        IndicateConfigChanged(RsConfigMgr::CheckPriority::SAVE_NOW);

        auto pEvent = std::make_shared<RsMailStatusEvent>();
        pEvent->mMailStatusEventCode = RsMailStatusEventCode::MESSAGE_CHANGED;
        pEvent->mChangedMsgIds = changed_msg_ids;
        rsEvents->postEvent(pEvent);
    }
}
void p3MsgService::loadWelcomeMsg()
{
	/* Load Welcome Message */
    RsMsgItem msg;

	//msg -> PeerId(mServiceCtrl->getOwnId());

    msg . sendTime = time(NULL);
    msg . recvTime = time(NULL);
    msg . msgFlags = RS_MSG_FLAGS_NEW;
    msg . subject = "Welcome to Retroshare";
    msg . message  = "Send and receive messages with your friends...\n";
    msg . message += "These can hold recommendations from your local shared files.\n\n";
    msg . message += "Add recommendations through the Local Files Dialog.\n\n";
    msg . message += "Enjoy.";
    msg . msgId = getNewUniqueMsgId();

    RsMailStorageItem *msi = new RsMailStorageItem;

    msi->msg = msg;
    msi->from = MsgAddress(RsPeerId(),MsgAddress::MSG_ADDRESS_MODE_TO); // means "system message"
    msi->to = MsgAddress(mServiceCtrl->getOwnId(),MsgAddress::MSG_ADDRESS_MODE_TO); // means "system message"
    msi->parentId = 0;

	RsStackMutex stack(mMsgMtx); /********** STACK LOCKED MTX ******/

    mReceivedMessages[msg.msgId] = msi;

	IndicateConfigChanged(RsConfigMgr::CheckPriority::SAVE_NOW);
}


/***********************************************************************/
/***********************************************************************/
/***********************************************************************/
/***********************************************************************/


/****************************************/
/****************************************/

bool p3MsgService::getMessageSummaries(BoxName box,std::list<MsgInfoSummary>& msgList)
{
    /* do stuff */
    msgList.clear();

    RsStackMutex stack(mMsgMtx); /********** STACK LOCKED MTX ******/

    if(box==BoxName::BOX_ALL || box == BoxName::BOX_SENT)
        for(const auto& mit : mSentMessages)
        {
            MsgInfoSummary mis;
            initRsMIS(*mit.second, mit.second->from,mit.second->to,mit.first,mis);
            msgList.push_back(mis);
        }

    if(box==BoxName::BOX_ALL || box == BoxName::BOX_INBOX)
        for(const auto& mit : mReceivedMessages)
        {
            MsgInfoSummary mis;
            initRsMIS(*mit.second, mit.second->from,mit.second->to,mit.first,mis);
            msgList.push_back(mis);
        }

    if(box==BoxName::BOX_ALL || box == BoxName::BOX_DRAFTS)
        for(const auto& mit : mDraftMessages)
        {
            MsgInfoSummary mis;
            initRsMIS(*mit.second, mit.second->from,mit.second->to,mit.first,mis);
            msgList.push_back(mis);
        }

    if(box==BoxName::BOX_ALL || box == BoxName::BOX_TRASH)
        for(const auto& mit : mTrashMessages)
        {
            MsgInfoSummary mis;
            initRsMIS(*mit.second, mit.second->from,mit.second->to,mit.first,mis);
            msgList.push_back(mis);
        }

    if(box==BoxName::BOX_ALL || box == BoxName::BOX_OUTBOX)
        for(const auto& mit:msgOutgoing) // Now special process for outgoing, since it's references with their own Ids
        {
            auto mref = mSentMessages.find(mit.first);

            if(mref == mSentMessages.end())
            {
                RsErr() << "Cannot find original source message with ID=" << mit.first << " for outgoing msg" ;
                continue;
            }

            for(auto sit:mit.second)
            {
                MsgInfoSummary mis;
                initRsMIS(*mref->second,sit.second.origin,sit.second.destination,sit.first,mis);

                // correct the flags
                //mis.msgflags = sit.second.flags;
                msgList.push_back(mis);
            }
        }

    return true;
}

bool p3MsgService::getMessage(const std::string& mId, MessageInfo& msg)
{
    uint32_t msgId = strtoul(mId.c_str(), NULL, 10);

    RsStackMutex stack(mMsgMtx); /********** STACK LOCKED MTX ******/

    auto mit = mReceivedMessages.find(msgId);

    if (mit != mReceivedMessages.end())
    {
        initRsMI(*mit->second, mit->second->from,mit->second->to,mit->second->msg.msgFlags,msg);
        return true;
    }

    mit = mDraftMessages.find(msgId);

    if (mit != mDraftMessages.end())
    {
        initRsMI(*mit->second, mit->second->from,mit->second->to,mit->second->msg.msgFlags,msg);
        return true;
    }

    mit = mSentMessages.find(msgId);

    if (mit != mSentMessages.end())
    {
        initRsMI(*mit->second, mit->second->from,mit->second->to,mit->second->msg.msgFlags,msg);
        return true;
    }

    mit = mTrashMessages.find(msgId);

    if (mit != mTrashMessages.end())
    {
        initRsMI(*mit->second, mit->second->from,mit->second->to,mit->second->msg.msgFlags,msg);
        return true;
    }

    for(auto mit:msgOutgoing)
    {
        auto sit = mit.second.find(msgId);

        if(sit != mit.second.end())
        {
            auto bit = mSentMessages.find(mit.first); // look for data of original message

            if(bit == mSentMessages.end())
            {
                RsErr() << "Cannot find original message of id=" << mit.first << " for outbox element with id=" << msgId ;
                return false;
            }
            // We supply our own flags because the outging msg has specific flags.
            initRsMI(*bit->second,sit->second.origin,sit->second.destination,sit->second.flags,msg);

            return true;
        }
    }
    return false;
}

void p3MsgService::getMessageCount(uint32_t &nInbox, uint32_t &nInboxNew, uint32_t &nOutbox, uint32_t &nDraftbox, uint32_t &nSentbox, uint32_t &nTrashbox)
{
    RsStackMutex stack(mMsgMtx); /********** STACK LOCKED MTX ******/

	nInbox = 0;
	nInboxNew = 0;
	nOutbox = 0;
	nDraftbox = 0;
	nSentbox = 0;
	nTrashbox = 0;

    // Inbox and InboxNew

    for (auto mit:mReceivedMessages)
    {
        if(mit.second->msg.msgFlags & RS_MSG_FLAGS_NEW)
            nInboxNew++;

        nInbox++;
    }

    // Sent box

         nSentbox = mSentMessages.size();

    // Outbox: Count 1 for each reference to a sent email.

    for(auto m:msgOutgoing)
        nOutbox += m.second.size();
}

/* remove based on the unique mid (stored in sid) */
bool    p3MsgService::deleteMessage(const std::string& mid)
{
    uint32_t msgId = strtoul(mid.c_str(), 0, 10);

    if (msgId == 0) {
        std::cerr << "p3MsgService::removeMsgId: Unknown msgId " << msgId << std::endl;
        return false;
    }

    bool changed = false;

    auto pEvent = std::make_shared<RsMailStatusEvent>();
    pEvent->mMailStatusEventCode = RsMailStatusEventCode::MESSAGE_REMOVED;

    {
        RsStackMutex stack(mMsgMtx); /********** STACK LOCKED MTX ******/

        auto mit = mReceivedMessages.find(msgId);

        if (mit != mReceivedMessages.end())
        {
            changed = true;
            delete mit->second;
            mReceivedMessages.erase(mit);
            pEvent->mChangedMsgIds.insert(mid);

            goto end_deleteMessage;
        }
        mit = mSentMessages.find(msgId);

        if (mit != mSentMessages.end())
        {
            changed = true;
            delete mit->second;
            mSentMessages.erase(mit);
            pEvent->mChangedMsgIds.insert(mid);

            goto end_deleteMessage;
        }
        mit = mTrashMessages.find(msgId);

        if (mit != mTrashMessages.end())
        {
            changed = true;
            delete mit->second;
            mTrashMessages.erase(mit);
            pEvent->mChangedMsgIds.insert(mid);

            goto end_deleteMessage;
        }

        for(auto& m:msgOutgoing)
        {
            auto msgcopyit = m.second.find(msgId);

            if(msgcopyit != m.second.end())
            {
                m.second.erase(msgcopyit);				// /!\ this works because only one msg is deleted!
                pEvent->mChangedMsgIds.insert(mid);
                changed = true;

                goto end_deleteMessage;
            }
        }

        RsErr() << "Message with ID = " << mid << " could not be found.";
        return false;
    }

end_deleteMessage:

    if(changed)
        IndicateConfigChanged(RsConfigMgr::CheckPriority::SAVE_NOW); /**** INDICATE MSG CONFIG CHANGED! *****/

    if(rsEvents && !pEvent->mChangedMsgIds.empty())
        rsEvents->postEvent(pEvent);

    return changed;
}

bool    p3MsgService::markMsgIdRead(const std::string &mid, bool unreadByUser)
{
    uint32_t msgId = strtoul(mid.c_str(), NULL, 10);

    {
        RsStackMutex stack(mMsgMtx); /********** STACK LOCKED MTX ******/

        auto mit = mReceivedMessages.find(msgId);

        if (mit != mReceivedMessages.end())
        {
            RsMsgItem *mi = &mit->second->msg;

            uint32_t msgFlags = mi->msgFlags;

            /* remove new state */
            mi->msgFlags &= ~(RS_MSG_FLAGS_NEW);

            /* set state from user */
            if (unreadByUser) {
                mi->msgFlags |= RS_MSG_FLAGS_UNREAD_BY_USER;
            } else {
                mi->msgFlags &= ~RS_MSG_FLAGS_UNREAD_BY_USER;
            }

            if (mi->msgFlags != msgFlags)
            {
                IndicateConfigChanged(RsConfigMgr::CheckPriority::SAVE_NOW); /**** INDICATE MSG CONFIG CHANGED! *****/

                auto pEvent = std::make_shared<RsMailStatusEvent>();
                pEvent->mMailStatusEventCode = RsMailStatusEventCode::MESSAGE_CHANGED;
                pEvent->mChangedMsgIds.insert(mid);
                rsEvents->postEvent(pEvent);
            }
        }
        else
            return false;

    } /* UNLOCKED */

    return true;
}

bool    p3MsgService::setMsgFlag(const std::string &mid, uint32_t flag, uint32_t mask)
{
    uint32_t msgId = strtoul(mid.c_str(), NULL, 10);

    {
        RsStackMutex stack(mMsgMtx); /********** STACK LOCKED MTX ******/

        auto sit = mReceivedMessages.find(msgId);

        if (sit == mReceivedMessages.end())
        {
            RsErr() << " Requested setMsgFlag on unknown message Id=" << msgId;
            return false;
        }
        auto msg = &sit->second->msg;

        uint32_t oldFlag = msg->msgFlags;

        msg->msgFlags &= ~mask;
        msg->msgFlags |= flag;

        if (msg->msgFlags != oldFlag)
        {
            IndicateConfigChanged(RsConfigMgr::CheckPriority::SAVE_NOW); /**** INDICATE MSG CONFIG CHANGED! *****/

            auto pEvent = std::make_shared<RsMailStatusEvent>();
            pEvent->mMailStatusEventCode = RsMailStatusEventCode::MESSAGE_CHANGED;
            pEvent->mChangedMsgIds.insert(mid);
            rsEvents->postEvent(pEvent);
        }
    } /* UNLOCKED */

    return true;
}

bool    p3MsgService::getMsgParentId(const std::string& msgId, std::string& msgParentId)
{
    uint32_t mId = strtoul(msgId.c_str(), NULL, 10);
    msgParentId.clear();

	RsStackMutex stack(mMsgMtx); /********** STACK LOCKED MTX ******/

    auto mit = mReceivedMessages.find(mId);

    if(mit != mReceivedMessages.end())
    {
        msgParentId = std::to_string(mit->second->parentId);
        return true;
    }

    mit = mSentMessages.find(mId);

    if(mit != mSentMessages.end())
    {
        msgParentId = std::to_string(mit->second->parentId);
        return true;
    }

    return false;
}

bool    p3MsgService::setMsgParentId(uint32_t msgId, uint32_t msgParentId)
{
    {
        RsStackMutex stack(mMsgMtx); /********** STACK LOCKED MTX ******/

        auto mit = mReceivedMessages.find(msgId);

        if(mit != mReceivedMessages.end())
        {
            mit->second->parentId = msgParentId;
            IndicateConfigChanged(RsConfigMgr::CheckPriority::SAVE_NOW); /**** INDICATE MSG CONFIG CHANGED! *****/
            return true;
        }

        mit = mSentMessages.find(msgId);

        if(mit != mSentMessages.end())
        {
            mit->second->parentId = msgParentId;
            IndicateConfigChanged(RsConfigMgr::CheckPriority::SAVE_NOW); /**** INDICATE MSG CONFIG CHANGED! *****/
            return true;
        }
    }
    return false;
}

/****************************************/
/****************************************/
	/* Message Items */
// no from field because it's implicitly our own PeerId
MessageIdentifier p3MsgService::internal_sendMessage(MessageIdentifier id,const MsgAddress& from,const MsgAddress& to,uint32_t flags)
{
    auto msgId     = getNewUniqueMsgId(); /* grabs Mtx as well */
    {
	    RS_STACK_MUTEX(mMsgMtx) ;

	    /* STORE MsgID */

        auto& mos(msgOutgoing[id]);	    // get to the outgoing list for message "id"
        auto& info(mos[msgId]);	        // create a new reference to that message in the list

        // then add the new msg id with the correct from/to

        info.flags = flags;
        info.destination = to;

        info.flags |= RS_MSG_FLAGS_OUTGOING ;

        if(to.type() == MsgAddress::MSG_ADDRESS_TYPE_RSGXSID)
        {
            info.flags |= RS_MSG_FLAGS_DISTANT;
            info.origin = from;
        }
        else
        {
            info.flags |= RS_MSG_FLAGS_LOAD_EMBEDDED_IMAGES; /* load embedded images only for node-to-node messages?? */  // (cyril: ?!?!)
            info.origin = Rs::Msgs::MsgAddress(mServiceCtrl->getOwnId(),Rs::Msgs::MsgAddress::MSG_ADDRESS_MODE_TO);
        }
    }

    IndicateConfigChanged(RsConfigMgr::CheckPriority::SAVE_NOW); /**** INDICATE MSG CONFIG CHANGED! *****/

    // Return the message id of the created reference
    return msgId;
}

// uint32_t p3MsgService::sendDistantMessage(RsMsgItem *item, const RsGxsId& from)
// {
// 	if(!item)
// 	{
// 		RsErr() << __PRETTY_FUNCTION__ << " item can't be null" << std::endl;
// 		print_stacktrace();
// 		return 0;
// 	}
//
// 	item->msgId  = getNewUniqueMsgId(); /* grabs Mtx as well */
// 	item->msgFlags |= ( RS_MSG_FLAGS_DISTANT | RS_MSG_FLAGS_OUTGOING |
// 	                    RS_MSG_FLAGS_PENDING ); /* add pending flag */
//
// 	{
// 		RS_STACK_MUTEX(mMsgMtx);
//
// 		/* STORE MsgID */
// 		msgOutgoing[item->msgId] = item;
// 		mDistantOutgoingMsgSigners[item->msgId] = from ;
//
// 		if (item->PeerId() != mServiceCtrl->getOwnId())
// 		{
// 			/* not to the loopback device */
//
// 			RsMsgSrcId* msi = new RsMsgSrcId();
// 			msi->msgId = item->msgId;
// 			msi->srcId = RsPeerId(from);
// 			mSrcIds.insert(std::pair<uint32_t, RsMsgSrcId*>(msi->msgId, msi));
// 		}
// 	}
//
// 	IndicateConfigChanged(RsConfigMgr::CheckPriority::SAVE_NOW); /**** INDICATE MSG CONFIG CHANGED! *****/
//
// 	auto pEvent = std::make_shared<RsMailStatusEvent>();
// 	pEvent->mMailStatusEventCode = RsMailStatusEventCode::MESSAGE_SENT;
// 	pEvent->mChangedMsgIds.insert(std::to_string(item->msgId));
// 	rsEvents->postEvent(pEvent);
//
// 	return item->msgId;
// }

bool 	p3MsgService::MessageSend(MessageInfo &info)
{
    // First store message in Sent list. In order to appear as sent the message needs to have the OUTGOING flg, but no PENDING flag.
    // This makes absolutely no sense. A message that is sent shouldn't be stored into the list of incoming messages.

    RsMailStorageItem *msi = initMIRsMsg(info);

    msi->to.clear();	    // Most of the time, messages have multiple destinations. There is no need to chose one in particular.
    msi->from = info.from;	// We should probably do the same here since distant and local msgs have different from fields. However, this
                            // is bound to not be the case when local messages are made obsolete.
    if(!msi)
        return false;

    auto msg(&msi->msg);

    if (msg->msgFlags & RS_MSG_FLAGS_SIGNED)
        msg->msgFlags |= RS_MSG_FLAGS_SIGNATURE_CHECKS;	// this is always true, since we are sending the message

    /* use processMsg to get the new msgId */

    msg->recvTime = time(NULL);
    msg->msgId = getNewUniqueMsgId();

    msg->msgFlags |= RS_MSG_FLAGS_OUTGOING;

    mSentMessages[msg->msgId] = msi;

    // Update info for caller (is that necessary?)
    info.msgId = std::to_string(msg->msgId);
    info.msgflags = msg->msgFlags;

    // Then stores outgoing message references for each destination in the msgOutgoing list

    for(auto pit: info.destinations)
        internal_sendMessage(msg->msgId,info.from, pit,info.msgflags);

    msi->msg.msgFlags |= RS_MSG_FLAGS_PENDING;

    auto pEvent = std::make_shared<RsMailStatusEvent>();
    pEvent->mMailStatusEventCode = RsMailStatusEventCode::MESSAGE_SENT;
    pEvent->mChangedMsgIds.insert(std::to_string(msg->msgId));
    rsEvents->postEvent(pEvent);

    return true;
}

uint32_t p3MsgService::sendMail(
        const RsGxsId from,
        const std::string& subject,
        const std::string& body,
        const std::set<RsGxsId>& to,
        const std::set<RsGxsId>& cc,
        const std::set<RsGxsId>& bcc,
        const std::vector<FileInfo>& attachments,
        std::set<RsMailIdRecipientIdPair>& trackingIds,
        std::string& errorMsg )
{
	errorMsg.clear();
	const std::string fname = __PRETTY_FUNCTION__;
	auto pCheck = [&](bool test, const std::string& errMsg)
	{
		if(!test)
		{
			errorMsg = errMsg;
			RsErr() << fname << " " << errMsg << std::endl;
		}
		return test;
	};

	if(!pCheck(!from.isNull(), "from can't be null")) return false;
    if(!pCheck( rsIdentity->isOwnId(from), "from must be own identity") ) return false;
    if(!pCheck(!(to.empty() && cc.empty() && bcc.empty()), "You must specify at least one recipient" )) return false;

    auto dstCheck = [&](const std::set<RsGxsId>& dstSet, const std::string& setName)
	{
		for(const RsGxsId& dst: dstSet)
		{
			if(dst.isNull())
			{
				errorMsg = setName + " contains a null recipient";
				RsErr() << fname << " " << errorMsg << std::endl;
				return false;
			}

			if(!rsIdentity->isKnownId(dst))
			{
				rsIdentity->requestIdentity(dst);
				errorMsg = setName + " contains an unknown recipient: " +
				        dst.toStdString();
				RsErr() << fname << " " << errorMsg << std::endl;
				return false;
			}
		}
		return true;
	};

	if(!dstCheck(to, "to"))   return false;
	if(!dstCheck(cc, "cc"))   return false;
	if(!dstCheck(bcc, "bcc")) return false;

	MessageInfo msgInfo;

    msgInfo.from = MsgAddress(from,MsgAddress::MSG_ADDRESS_MODE_TO);
	msgInfo.title = subject;
	msgInfo.msg = body;

    for(auto t:to)  msgInfo.destinations.insert(MsgAddress(t,MsgAddress::MSG_ADDRESS_MODE_TO));
    for(auto t:cc)  msgInfo.destinations.insert(MsgAddress(t,MsgAddress::MSG_ADDRESS_MODE_CC));
    for(auto t:bcc) msgInfo.destinations.insert(MsgAddress(t,MsgAddress::MSG_ADDRESS_MODE_BCC));

    std::copy( attachments.begin(), attachments.end(), std::back_inserter(msgInfo.files) );

    auto msi = initMIRsMsg(msgInfo);

    msi->msg.msgId = getNewUniqueMsgId();
    msi->msg.msgFlags = RS_MSG_FLAGS_DISTANT | RS_MSG_FLAGS_PENDING;

    mSentMessages[msi->msg.msgId] = msi;

	uint32_t ret = 0;

	auto pEvent = std::make_shared<RsMailStatusEvent>();
	pEvent->mMailStatusEventCode = RsMailStatusEventCode::MESSAGE_SENT;

    for(auto dst:msgInfo.destinations)
    {
        auto msg_copy_id = internal_sendMessage(msi->msg.msgId,MsgAddress(from,MsgAddress::MSG_ADDRESS_MODE_TO),dst,msi->msg.msgFlags);

        const RsMailMessageId mailId = std::to_string(msg_copy_id);
        pEvent->mChangedMsgIds.insert(mailId);

        if(dst.type() == MsgAddress::MSG_ADDRESS_TYPE_RSGXSID)
            trackingIds.insert(RsMailIdRecipientIdPair(mailId, dst.toGxsId()));	// (cyril: I don't understand why we should keep track of these. Only msi->msg.msgId is important)

        ++ret;
    }

	if(rsEvents) rsEvents->postEvent(pEvent);
	return ret;
}

bool p3MsgService::SystemMessage(const std::string &title, const std::string &message, uint32_t systemFlag)
{
	if ((systemFlag & RS_MSG_SYSTEM) == 0) {
		/* no flag specified */
		return false;
	}


	RsMsgItem *msg = new RsMsgItem();

	msg->PeerId();// Notification == null

	msg->msgFlags = 0;

	if (systemFlag & RS_MSG_USER_REQUEST) {
		msg->msgFlags |= RS_MSG_FLAGS_USER_REQUEST;
	}
	if (systemFlag & RS_MSG_FRIEND_RECOMMENDATION) {
		msg->msgFlags |= RS_MSG_FLAGS_FRIEND_RECOMMENDATION;
	}
	if (systemFlag & RS_MSG_PUBLISH_KEY) {
		msg->msgFlags |= RS_MSG_FLAGS_PUBLISH_KEY;
	}

	msg->msgId = 0;
	msg->sendTime = time(NULL);
	msg->recvTime = 0;

	msg->subject = title;
	msg->message = message;

	msg->rspeerid_msgto.ids.insert(mServiceCtrl->getOwnId());

    processIncomingMsg(msg,
                           Rs::Msgs::MsgAddress(RsPeerId(),              Rs::Msgs::MsgAddress::MSG_ADDRESS_MODE_TO),
                           Rs::Msgs::MsgAddress(mServiceCtrl->getOwnId(),Rs::Msgs::MsgAddress::MSG_ADDRESS_MODE_TO));

	return true;
}

bool p3MsgService::MessageToDraft(MessageInfo& info, const std::string& msgParentId)
{
    RsMailStorageItem *msg = initMIRsMsg(info);

    if (!msg)
        return false;

    msg->parentId = strtoul(msgParentId.c_str(), NULL, 10);

    uint32_t msgId = getNewUniqueMsgId(); /* grabs Mtx as well */
    msg->msg.msgId = msgId;

    {
        RsStackMutex stack(mMsgMtx); /********** STACK LOCKED MTX ******/

        /* add pending flag */
        msg->msg.msgFlags |= RS_MSG_FLAGS_DRAFT;

        /* STORE MsgID */

        if(mDraftMessages.find(msgId) != mDraftMessages.end())
            delete mDraftMessages[msgId];

        mDraftMessages[msgId] = msg;

        // return new message id
       info.msgId = std::to_string(msgId);
    }

    IndicateConfigChanged(RsConfigMgr::CheckPriority::SAVE_NOW); /**** INDICATE MSG CONFIG CHANGED! *****/

    auto pEvent = std::make_shared<RsMailStatusEvent>();
    pEvent->mMailStatusEventCode = RsMailStatusEventCode::MESSAGE_SENT;
    pEvent->mChangedMsgIds.insert(std::to_string(msgId));
    rsEvents->postEvent(pEvent);

    return true;
}

bool 	p3MsgService::getMessageTag(const std::string &msgId, Rs::Msgs::MsgTagInfo& info)
{
	RsStackMutex stack(mMsgMtx); /********** STACK LOCKED MTX ******/
    return locked_getMessageTag(msgId,info);
}

bool 	p3MsgService::getMessageTagTypes(MsgTagType& tags)
{
	RsStackMutex stack(mMsgMtx); /********** STACK LOCKED MTX ******/

	std::map<uint32_t, RsMsgTagType*>::iterator mit;

	for(mit = mTags.begin(); mit != mTags.end(); ++mit) {
		std::pair<std::string, uint32_t> p(mit->second->text, mit->second->rgb_color);
		tags.types.insert(std::pair<uint32_t, std::pair<std::string, uint32_t> >(mit->first, p));
	}

	return true;
}

bool  	p3MsgService::setMessageTagType(uint32_t tagId, std::string& text, uint32_t rgb_color)
{
	auto ev = std::make_shared<RsMailTagEvent>();

	{
		RsStackMutex stack(mMsgMtx); /********** STACK LOCKED MTX ******/

		std::map<uint32_t, RsMsgTagType*>::iterator mit;
		mit = mTags.find(tagId);

		if (mit == mTags.end()) {
			if (tagId < RS_MSGTAGTYPE_USER) {
				std::cerr << "p3MsgService::MessageSetTagType: Standard tag type " <<  tagId << " cannot be inserted" << std::endl;
				return false;
			}

			/* new tag */
			RsMsgTagType* tagType = new RsMsgTagType();
			tagType->PeerId (mServiceCtrl->getOwnId());
			tagType->rgb_color = rgb_color;
			tagType->tagId = tagId;
			tagType->text = text;

			mTags.insert(std::pair<uint32_t, RsMsgTagType*>(tagId, tagType));

			ev->mMailTagEventCode = RsMailTagEventCode::TAG_ADDED;
			ev->mChangedMsgTagIds.insert(std::to_string(tagId));
		} else {
			if (mit->second->text != text || mit->second->rgb_color != rgb_color) {
				/* modify existing tag */
				if (tagId >= RS_MSGTAGTYPE_USER) {
					mit->second->text = text;
				} else {
					/* don't change text for standard tag types */
					if (mit->second->text != text) {
						std::cerr << "p3MsgService::MessageSetTagType: Text " << text << " for standard tag type " <<  tagId << " cannot be changed" << std::endl;
					}
				}
				mit->second->rgb_color = rgb_color;

				ev->mMailTagEventCode = RsMailTagEventCode::TAG_CHANGED;
				ev->mChangedMsgTagIds.insert(std::to_string(tagId));
			}
		}

	} /* UNLOCKED */

	if (!ev->mChangedMsgTagIds.empty()) {
		IndicateConfigChanged(RsConfigMgr::CheckPriority::SAVE_NOW); /**** INDICATE MSG CONFIG CHANGED! *****/

		rsEvents->postEvent(ev);

		return true;
	}

	return false;
}

bool    p3MsgService::removeMessageTagType(uint32_t tagId)
{
    if (tagId < RS_MSGTAGTYPE_USER) {
        std::cerr << "p3MsgService::MessageRemoveTagType: Can't delete standard tag type " << tagId << std::endl;
        return false;
    }

    auto msgEvent = std::make_shared<RsMailStatusEvent>();
    msgEvent->mMailStatusEventCode = RsMailStatusEventCode::TAG_CHANGED;

    {
        RsStackMutex stack(mMsgMtx); /********** STACK LOCKED MTX ******/

        std::map<uint32_t, RsMsgTagType*>::iterator mit;
        mit = mTags.find(tagId);

        if (mit == mTags.end()) {
            /* tag id not found */
            std::cerr << "p3MsgService::MessageRemoveTagType: Tag Id not found " << tagId << std::endl;
            return false;
        }

        /* search for messages with this tag type */

        std::list<std::map<uint32_t,RsMailStorageItem*> > lst( { mReceivedMessages, mSentMessages, mTrashMessages, mDraftMessages });

        for(auto mit:lst)
            for(auto msi:mit)
            {
                auto tag_it = msi.second->tagIds.find(tagId);

                if(tag_it != msi.second->tagIds.end())
                {
                    msi.second->tagIds.erase(tag_it);
                    msgEvent->mChangedMsgIds.insert(std::to_string(msi.first));
                }
            }

        /* remove tag type */
        delete(mit->second);
        mTags.erase(mit);

    } /* UNLOCKED */

    IndicateConfigChanged(RsConfigMgr::CheckPriority::SAVE_NOW); /**** INDICATE MSG CONFIG CHANGED! *****/

    auto ev = std::make_shared<RsMailTagEvent>();
    ev->mMailTagEventCode = RsMailTagEventCode::TAG_REMOVED;
    ev->mChangedMsgTagIds.insert(std::to_string(tagId));
    rsEvents->postEvent(ev);

    if (!msgEvent->mChangedMsgIds.empty()) {
        rsEvents->postEvent(msgEvent);
    }

    return true;
}

RsMailStorageItem *p3MsgService::locked_getMessageData(uint32_t mid) const
{
    std::map<uint32_t,RsMailStorageItem*>::const_iterator it;

    if( (it = mReceivedMessages.find(mid)) != mReceivedMessages.end())
        return it->second;

    if( (it = mSentMessages.find(mid)) != mSentMessages.end())
        return it->second;

    if( (it = mDraftMessages.find(mid)) != mDraftMessages.end())
        return it->second;

    if( (it = mTrashMessages.find(mid)) != mTrashMessages.end())
        return it->second;

    return nullptr;
}

bool 	p3MsgService::locked_getMessageTag(const std::string &msgId, MsgTagInfo& info)
{
    uint32_t mid = strtoul(msgId.c_str(), NULL, 10);

    if(!mid)
    {
        RsErr() << "Wrong message id string received \"" << msgId << "\"" ;
        return false;
    }

    auto mis = locked_getMessageData(mid);

    if(!mis)
        return false;

    info = mis->tagIds;

    return true;
}

/* set == false && tagId == 0 --> remove all */
bool p3MsgService::setMessageTag(const std::string& msgId, uint32_t tagId, bool set)
{
	uint32_t mid = strtoul(msgId.c_str(), NULL, 10);
    if (mid == 0)
    {
        RsErr() << "p3MsgService::MessageSetMsgTag: Unknown msgId " << msgId ;
		return false;
	}

    if (tagId == 0 && set)
        {
            RsErr() << "p3MsgService::MessageSetMsgTag: No valid tagId given " << tagId ;
			return false;
		}
	
	auto ev = std::make_shared<RsMailStatusEvent>();
	ev->mMailStatusEventCode = RsMailStatusEventCode::TAG_CHANGED;

    {
        RsStackMutex stack(mMsgMtx); /********** STACK LOCKED MTX ******/

        auto msi = locked_getMessageData(mid);

        if(!msi)
            return false;

        if(set)
        {
            msi->tagIds.insert(tagId);
            ev->mChangedMsgIds.insert(msgId); // normally we should check whether the tag already exists or not.
        }
        else if(tagId==0)		// See rsmsgs.h. tagId=0 => erase all tags.
        {
            msi->tagIds.clear();
            ev->mChangedMsgIds.insert(msgId);
        }
        else if(0 < msi->tagIds.erase(tagId))
            ev->mChangedMsgIds.insert(msgId);

    } /* UNLOCKED */

    if (!ev->mChangedMsgIds.empty())
    {
		IndicateConfigChanged(RsConfigMgr::CheckPriority::SAVE_NOW); /**** INDICATE MSG CONFIG CHANGED! *****/
		rsEvents->postEvent(ev);

		return true;
	}

	return false;
}

bool    p3MsgService::resetMessageStandardTagTypes(MsgTagType& tags)
{
	MsgTagType standardTags;
        getStandardTagTypes(standardTags);

	std::map<uint32_t, std::pair<std::string, uint32_t> >::iterator mit;
	for (mit = standardTags.types.begin(); mit != standardTags.types.end(); ++mit) {
		tags.types[mit->first] = mit->second;
	}

	return true;
}

/* move message to trash based on the unique mid */
bool p3MsgService::MessageToTrash(const std::string& mid, bool bTrash)
{
    uint32_t msgId = strtoul(mid.c_str(), NULL, 10);

    bool bFound = false;
    auto pEvent = std::make_shared<RsMailStatusEvent>();
    pEvent->mMailStatusEventCode = RsMailStatusEventCode::MESSAGE_CHANGED;

    if(bTrash)
    {
        RsStackMutex stack(mMsgMtx); /********** STACK LOCKED MTX ******/

        auto mit = mReceivedMessages.find(msgId);

        if(mit != mReceivedMessages.end())
        {
            bFound = true;
            mTrashMessages[mit->first] = mit->second;
            mit->second->msg.msgFlags |= RS_MSG_FLAGS_TRASH;
            pEvent->mChangedMsgIds.insert(mid);
            mReceivedMessages.erase(mit);
        }

        mit = mSentMessages.find(msgId);

        if(mit != mSentMessages.end())
        {
            bFound = true;
            mTrashMessages[mit->first] = mit->second;
            mit->second->msg.msgFlags |= RS_MSG_FLAGS_TRASH;
            pEvent->mChangedMsgIds.insert(mid);
            mSentMessages.erase(mit);
        }

        mit = mDraftMessages.find(msgId);

        if(mit != mDraftMessages.end())
        {
            bFound = true;
            mTrashMessages[mit->first] = mit->second;
            mit->second->msg.msgFlags |= RS_MSG_FLAGS_TRASH;
            pEvent->mChangedMsgIds.insert(mid);
            mDraftMessages.erase(mit);
        }

    }
    else
    {
        auto mit = mTrashMessages.find(msgId);

        if(mit != mTrashMessages.end())
        {
            bFound = true;

            if(mit->second->msg.msgFlags & RS_MSG_FLAGS_OUTGOING)
                mSentMessages[mit->first] = mit->second;
            else
                mReceivedMessages[mit->first] = mit->second;

            mit->second->msg.msgFlags &= ~RS_MSG_FLAGS_TRASH;
            pEvent->mChangedMsgIds.insert(mid);
            mTrashMessages.erase(mit);
        }

    }

    if(!bFound)
        RsErr() << "Could not find message in appropriate lists!" ;

    if (!pEvent->mChangedMsgIds.empty()) {
        IndicateConfigChanged(RsConfigMgr::CheckPriority::SAVE_NOW); /**** INDICATE MSG CONFIG CHANGED! *****/

        checkOutgoingMessages();

        if(rsEvents) {
            rsEvents->postEvent(pEvent);
        }
    }

    return bFound;
}

/****************************************/
/****************************************/


/****************************************/

/**** HELPER FNS For Chat/Msg/Channel Lists ************
 * These aren't required to be locked, unless
 * the data used is from internal stores -> then they should be.
 */

void p3MsgService::initRsMI(const RsMailStorageItem& msi, const MsgAddress& from, const MsgAddress& to, uint32_t flags,MessageInfo &mi)
{
    auto msg(&msi.msg);
	mi.msgflags = 0;

	/* translate flags, if we sent it... outgoing */

    if (flags & RS_MSG_FLAGS_OUTGOING)        mi.msgflags |= RS_MSG_OUTGOING;
    if (flags & RS_MSG_FLAGS_PENDING)         mi.msgflags |= RS_MSG_PENDING;    /* if it has a pending flag, then its in the outbox */
    if (flags & RS_MSG_FLAGS_DRAFT)           mi.msgflags |= RS_MSG_DRAFT;
    if (flags & RS_MSG_FLAGS_NEW)             mi.msgflags |= RS_MSG_NEW;

    if (flags & RS_MSG_FLAGS_SIGNED)                  mi.msgflags |= RS_MSG_SIGNED ;
    if (flags & RS_MSG_FLAGS_SIGNATURE_CHECKS)        mi.msgflags |= RS_MSG_SIGNATURE_CHECKS ;
    if (flags & RS_MSG_FLAGS_DISTANT)                 mi.msgflags |= RS_MSG_DISTANT ;
    if (flags & RS_MSG_FLAGS_TRASH)                   mi.msgflags |= RS_MSG_TRASH;
    if (flags & RS_MSG_FLAGS_UNREAD_BY_USER)          mi.msgflags |= RS_MSG_UNREAD_BY_USER;
    if (flags & RS_MSG_FLAGS_REPLIED)                 mi.msgflags |= RS_MSG_REPLIED;
    if (flags & RS_MSG_FLAGS_FORWARDED)               mi.msgflags |= RS_MSG_FORWARDED;
    if (flags & RS_MSG_FLAGS_STAR)                    mi.msgflags |= RS_MSG_STAR;
    if (flags & RS_MSG_FLAGS_SPAM)                    mi.msgflags |= RS_MSG_SPAM;
    if (flags & RS_MSG_FLAGS_USER_REQUEST)            mi.msgflags |= RS_MSG_USER_REQUEST;
    if (flags & RS_MSG_FLAGS_FRIEND_RECOMMENDATION)   mi.msgflags |= RS_MSG_FRIEND_RECOMMENDATION;
    if (flags & RS_MSG_FLAGS_PUBLISH_KEY)             mi.msgflags |= RS_MSG_PUBLISH_KEY;
    if (flags & RS_MSG_FLAGS_LOAD_EMBEDDED_IMAGES)    mi.msgflags |= RS_MSG_LOAD_EMBEDDED_IMAGES;

	mi.ts = msg->sendTime;

    mi.from = from;
    mi.to = to;

    for(auto m:msg->rspeerid_msgto .ids) mi.destinations.insert(MsgAddress(m,MsgAddress::MSG_ADDRESS_MODE_TO ));
    for(auto m:msg->rspeerid_msgcc .ids) mi.destinations.insert(MsgAddress(m,MsgAddress::MSG_ADDRESS_MODE_CC ));
    for(auto m:msg->rspeerid_msgbcc.ids) mi.destinations.insert(MsgAddress(m,MsgAddress::MSG_ADDRESS_MODE_BCC));

    for(auto m:msg->rsgxsid_msgto .ids) mi.destinations.insert(MsgAddress(m,MsgAddress::MSG_ADDRESS_MODE_TO ));
    for(auto m:msg->rsgxsid_msgcc .ids) mi.destinations.insert(MsgAddress(m,MsgAddress::MSG_ADDRESS_MODE_CC ));
    for(auto m:msg->rsgxsid_msgbcc.ids) mi.destinations.insert(MsgAddress(m,MsgAddress::MSG_ADDRESS_MODE_BCC));

	mi.title = msg->subject;
	mi.msg   = msg->message;
    mi.msgId = std::to_string(msg->msgId);

	mi.attach_title = msg->attachment.title;
	mi.attach_comment = msg->attachment.comment;

	mi.count = 0;
	mi.size = 0;

    for(std::list<RsTlvFileItem>::const_iterator it = msg->attachment.items.begin(); it != msg->attachment.items.end(); ++it)
	{
		FileInfo fi;
		fi.fname = RsDirUtil::getTopDir(it->name);
		fi.size  = it->filesize;
		fi.hash  = it->hash;
		fi.path  = it->path;
		mi.files.push_back(fi);
		mi.count++;
		mi.size += fi.size;
	}
}

void p3MsgService::initRsMIS(const RsMailStorageItem& msi, const MsgAddress& from, const MsgAddress& to, MessageIdentifier mid,MsgInfoSummary& mis)
{
    mis.msgId = std::to_string(mid);
    mis.msgflags = 0;

    const RsMsgItem *msg = &msi.msg;	// trick to keep the old code

    mis.to = to;
    mis.from = from;

#ifdef DEBUG_DISTANT_MSG
    std::cerr << "msg (peerId=" << msg->PeerId() << ", distant=" << bool(msg->msgFlags & RS_MSG_FLAGS_DISTANT)<< ": " << msg << std::endl;
#endif

    mis.from = msi.from;

    if(msg->msgFlags & RS_MSG_FLAGS_DISTANT)
        mis.msgflags |= RS_MSG_DISTANT ;

    if (msg->msgFlags & RS_MSG_FLAGS_SIGNED)
		mis.msgflags |= RS_MSG_SIGNED ;

	if (msg->msgFlags & RS_MSG_FLAGS_SIGNATURE_CHECKS)
		mis.msgflags |= RS_MSG_SIGNATURE_CHECKS ;

	/* translate flags, if we sent it... outgoing */
	if ((msg->msgFlags & RS_MSG_FLAGS_OUTGOING)
	   /*|| (msg->PeerId() == mServiceCtrl->getOwnId())*/)
	{
		mis.msgflags |= RS_MSG_OUTGOING;
	}
	/* if it has a pending flag, then its in the outbox */
	if (msg->msgFlags & RS_MSG_FLAGS_PENDING)
	{
		mis.msgflags |= RS_MSG_PENDING;
	}
	if (msg->msgFlags & RS_MSG_FLAGS_DRAFT)
	{
		mis.msgflags |= RS_MSG_DRAFT;
	}
	if (msg->msgFlags & RS_MSG_FLAGS_NEW)
	{
		mis.msgflags |= RS_MSG_NEW;
	}
	if (msg->msgFlags & RS_MSG_FLAGS_TRASH)
	{
		mis.msgflags |= RS_MSG_TRASH;
	}
	if (msg->msgFlags & RS_MSG_FLAGS_UNREAD_BY_USER)
	{
		mis.msgflags |= RS_MSG_UNREAD_BY_USER;
	}
	if (msg->msgFlags & RS_MSG_FLAGS_REPLIED)
	{
		mis.msgflags |= RS_MSG_REPLIED;
	}
	if (msg->msgFlags & RS_MSG_FLAGS_FORWARDED)
	{
		mis.msgflags |= RS_MSG_FORWARDED;
	}
	if (msg->msgFlags & RS_MSG_FLAGS_STAR)
	{
		mis.msgflags |= RS_MSG_STAR;
	}
	if (msg->msgFlags & RS_MSG_FLAGS_SPAM)
	{
		mis.msgflags |= RS_MSG_SPAM;
	}
	if (msg->msgFlags & RS_MSG_FLAGS_USER_REQUEST)
	{
		mis.msgflags |= RS_MSG_USER_REQUEST;
	}
	if (msg->msgFlags & RS_MSG_FLAGS_FRIEND_RECOMMENDATION)
	{
		mis.msgflags |= RS_MSG_FRIEND_RECOMMENDATION;
	}
	if (msg->msgFlags & RS_MSG_FLAGS_PUBLISH_KEY)
	{
		mis.msgflags |= RS_MSG_PUBLISH_KEY;
	}
	if (msg->msgFlags & RS_MSG_FLAGS_LOAD_EMBEDDED_IMAGES)
	{
		mis.msgflags |= RS_MSG_LOAD_EMBEDDED_IMAGES;
	}

    mis.title = msg->subject;
	mis.count = msg->attachment.items.size();
	mis.ts = msg->sendTime;

    MsgTagInfo taginfo;
    locked_getMessageTag(mis.msgId,taginfo);
    mis.msgtags = taginfo ;

    auto addToDestination_gxsid  = [&mis](const RsTlvGxsIdSet & s,MsgAddress::AddressMode mode) { for(auto m:s.ids) mis.destinations.insert(MsgAddress(m,mode)); };
    auto addToDestination_peerid = [&mis](const RsTlvPeerIdSet& s,MsgAddress::AddressMode mode) { for(auto m:s.ids) mis.destinations.insert(MsgAddress(m,mode)); };

    addToDestination_gxsid(msg->rsgxsid_msgto,MsgAddress::MSG_ADDRESS_MODE_TO);
    addToDestination_gxsid(msg->rsgxsid_msgcc,MsgAddress::MSG_ADDRESS_MODE_CC);
    addToDestination_gxsid(msg->rsgxsid_msgbcc,MsgAddress::MSG_ADDRESS_MODE_BCC);
    addToDestination_peerid(msg->rspeerid_msgto,MsgAddress::MSG_ADDRESS_MODE_TO);
    addToDestination_peerid(msg->rspeerid_msgcc,MsgAddress::MSG_ADDRESS_MODE_CC);
    addToDestination_peerid(msg->rspeerid_msgbcc,MsgAddress::MSG_ADDRESS_MODE_BCC);
}

bool p3MsgService::initMIRsMsg(RsMailStorageItem *msi,const MessageInfo& info)
{
    auto msg(&msi->msg);	// trick to keep the previous code.

	msg -> msgFlags = 0;
	msg -> msgId = 0;
	msg -> sendTime = time(NULL);
	msg -> recvTime = 0;
	msg -> subject = info.title;
	msg -> message = info.msg;

    // We need to use the RsItem format. It's bad, but needed for backward compatibility at the network layer.

    for(auto m:info.destinations)
        switch(m.mode())
        {
        case MsgAddress::MSG_ADDRESS_MODE_TO:
            if(m.type()==MsgAddress::MSG_ADDRESS_TYPE_RSGXSID)
                msg->rsgxsid_msgto.ids.insert(m.toGxsId());
            else
                msg->rspeerid_msgto.ids.insert(m.toRsPeerId());
            break;
        case MsgAddress::MSG_ADDRESS_MODE_CC:
            if(m.type()==MsgAddress::MSG_ADDRESS_TYPE_RSGXSID)
                msg->rsgxsid_msgcc.ids.insert(m.toGxsId());
            else
                msg->rspeerid_msgcc.ids.insert(m.toRsPeerId());
            break;
        case MsgAddress::MSG_ADDRESS_MODE_BCC:							// BCC destinations will be filtered out just before sending the message.
            if(m.type()==MsgAddress::MSG_ADDRESS_TYPE_RSGXSID)
                msg->rsgxsid_msgbcc.ids.insert(m.toGxsId());
            else if(m.type()==MsgAddress::MSG_ADDRESS_TYPE_RSPEERID)
                msg->rspeerid_msgbcc.ids.insert(m.toRsPeerId());
            break;
        default:
            RsErr() << "Address with unknown mode when creating a MailStorageItem: \"" << m.toStdString() << "\"" << std::endl;
        }

	msg -> attachment.title   = info.attach_title;
	msg -> attachment.comment = info.attach_comment;

	for(std::list<FileInfo>::const_iterator it = info.files.begin(); it != info.files.end(); ++it)
	{
		RsTlvFileItem mfi;
		mfi.hash = it -> hash;
		mfi.name = it -> fname;
		mfi.filesize = it -> size;
		msg -> attachment.items.push_back(mfi);
	}
	/* translate flags from outside */
	if (info.msgflags & RS_MSG_USER_REQUEST)
        msg->msgFlags |= RS_MSG_FLAGS_USER_REQUEST;

	if (info.msgflags & RS_MSG_FRIEND_RECOMMENDATION)
		msg->msgFlags |= RS_MSG_FLAGS_FRIEND_RECOMMENDATION;

    if (info.msgflags & RS_MSG_SIGNED)
        msg->msgFlags |= RS_MSG_FLAGS_SIGNED;

    return true;
}

RsMailStorageItem *p3MsgService::initMIRsMsg(const MessageInfo &info)
{
    RsMailStorageItem *msg = new RsMailStorageItem();

    if(initMIRsMsg(msg,info))
        return msg;
    else
    {
        delete msg;
        return nullptr;
    }
}

void p3MsgService::connectToGlobalRouter(p3GRouter *gr)
{
	mGRouter = gr ;
	gr->registerClientService(GROUTER_CLIENT_ID_MESSAGES,this) ;
}

void p3MsgService::enableDistantMessaging(bool b)
{
    // We use a temporary variable because the call to OwnIds() might fail.

    mShouldEnableDistantMessaging = b ;
    IndicateConfigChanged(RsConfigMgr::CheckPriority::SAVE_NOW) ;
}

bool p3MsgService::distantMessagingEnabled()
{
	RsStackMutex stack(mMsgMtx); /********** STACK LOCKED MTX ******/
    return mShouldEnableDistantMessaging ;
}

void p3MsgService::manageDistantPeers()
{
	// now possibly flush pending messages

    if(mShouldEnableDistantMessaging == mDistantMessagingEnabled)
        return ;

#ifdef DEBUG_DISTANT_MSG
	std::cerr << "p3MsgService::manageDistantPeers()" << std::endl;
#endif
    std::list<RsGxsId> own_id_list ;

    if(mIdService->getOwnIds(own_id_list))
    {
#ifdef DEBUG_DISTANT_MSG
        for(std::list<RsGxsId>::const_iterator it(own_id_list.begin());it!=own_id_list.end();++it)
            std::cerr << (mShouldEnableDistantMessaging?"Enabling":"Disabling") << " distant messaging, with peer id = " << *it << std::endl;
#endif

        for(std::list<RsGxsId>::const_iterator it(own_id_list.begin());it!=own_id_list.end();++it)
        {
            if(mShouldEnableDistantMessaging)
                mGRouter->registerKey(*it,GROUTER_CLIENT_ID_MESSAGES,"Messaging contact") ;
            else
                mGRouter->unregisterKey(*it,GROUTER_CLIENT_ID_MESSAGES) ;
        }

        RsStackMutex stack(mMsgMtx); /********** STACK LOCKED MTX ******/
        mDistantMessagingEnabled = mShouldEnableDistantMessaging ;
    }
}

void p3MsgService::notifyDataStatus( const GRouterMsgPropagationId& id,
                                     const RsGxsId& signer_id,
                                     uint32_t data_status )
{
    if(data_status == GROUTER_CLIENT_SERVICE_DATA_STATUS_FAILED)
    {
        RS_STACK_MUTEX(mMsgMtx);

        auto it = _grouter_ongoing_messages.find(id);
        if(it == _grouter_ongoing_messages.end())
        {
            RsErr() << __PRETTY_FUNCTION__
                    << " cannot find pending message to acknowledge. "
                    << "Weird. grouter id: " << id << std::endl;
            return;
        }

        uint32_t msg_id = it->second;

        RsWarn() << __PRETTY_FUNCTION__ << " Global router tells "
                 << "us that item ID " << id
                 << " could not be delivered on time to " << signer_id << ". Message id: "
                 << msg_id << std::endl;

        for(auto it=msgOutgoing.begin();it!=msgOutgoing.end();++it)
        {
            auto mit = it->second.find(msg_id);

            if(mit != it->second.end())
            {
                std::cerr << "  reseting the ROUTED flag so that the message is requested again" << std::endl;
                mit->second.flags &= ~RS_MSG_FLAGS_ROUTED;
                break;
            }
            else
            {
                std::cerr << "(ii) message has been notified as delivered, but it's"
                          << " not in outgoing list. probably it has been delivered"
                          << " successfully by other means." << std::endl;
                return;
            }
        }
    }
    else if(data_status == GROUTER_CLIENT_SERVICE_DATA_STATUS_RECEIVED)
    {
        RS_STACK_MUTEX(mMsgMtx);
#ifdef DEBUG_DISTANT_MSG
        std::cerr << "p3MsgService::acknowledgeDataReceived(): acknowledging data received for msg propagation id  " << id << std::endl;
#endif
        auto it = _grouter_ongoing_messages.find(id);
        if(it == _grouter_ongoing_messages.end())
        {
            std::cerr << "  (EE) cannot find pending message to acknowledge. "
                      << "Weird. grouter id = " << id << std::endl;
            return;
        }

        uint32_t msg_id = it->second;

        // We should now remove the item from the msgOutgoing list. msgOutgoing is indexed by the original msg, not its copy, so we need
        // a linear search. It's bad, but really doesn't happen very often.

        bool found = false;

        for(auto it=msgOutgoing.begin();it!=msgOutgoing.end();++it)
        {
            auto mit = it->second.find(msg_id);

            if(mit != it->second.end())
            {
                it->second.erase(mit);
                found = true;
                break;
            }

        }
        if(!found)
        {
            std::cerr << "(ii) message has been notified as delivered, but it's"
                      << " not in outgoing list. probably it has been delivered"
                      << " successfully by other means." << std::endl;
            return;
        }

        IndicateConfigChanged(RsConfigMgr::CheckPriority::SAVE_NOW);

        if(rsEvents)
        {
            auto pEvent = std::make_shared<RsMailStatusEvent>();
            pEvent->mMailStatusEventCode = RsMailStatusEventCode::MESSAGE_CHANGED;
            pEvent->mChangedMsgIds.insert(std::to_string(msg_id));
            rsEvents->postEvent(pEvent);
        }
    }
    else
        RsErr() << __PRETTY_FUNCTION__ << " unhandled data status info from global router"
            << " for msg ID " << id << ": this is a bug." << std::endl;
}

bool p3MsgService::acceptDataFromPeer(const RsGxsId& to_gxs_id)
{
    if(mDistantMessagePermissions & RS_DISTANT_MESSAGING_CONTACT_PERMISSION_FLAG_FILTER_NON_CONTACTS)
        return (rsIdentity!=NULL) && rsIdentity->isARegularContact(to_gxs_id) ;
    
    if(mDistantMessagePermissions & RS_DISTANT_MESSAGING_CONTACT_PERMISSION_FLAG_FILTER_EVERYBODY)
        return false ;
    
    return true ;
}

void p3MsgService::setDistantMessagingPermissionFlags(uint32_t flags) 
{
    if(flags != mDistantMessagePermissions)
    {
	    mDistantMessagePermissions = flags ;

	    IndicateConfigChanged(RsConfigMgr::CheckPriority::SAVE_NOW) ;
    }
}
            
uint32_t p3MsgService::getDistantMessagingPermissionFlags() 
{
    return mDistantMessagePermissions ;
}

bool p3MsgService::receiveGxsTransMail( const RsGxsId& authorId,
                                const RsGxsId& recipientId,
                                const uint8_t* data, uint32_t dataSize )
{
	Dbg2() << __PRETTY_FUNCTION__ << " " << authorId << ", " << recipientId
	       << ",, " << dataSize << std::endl;

	Sha1CheckSum hash = RsDirUtil::sha1sum(data, dataSize);

	{
		RS_STACK_MUTEX(recentlyReceivedMutex);
		if( mRecentlyReceivedMessageHashes.find(hash) != mRecentlyReceivedMessageHashes.end() )
		{
			RsInfo() << __PRETTY_FUNCTION__ << " (II) receiving "
			          << "message of hash " << hash << " more than once. "
			          << "Probably it has arrived  before by other means."
			          << std::endl;
			return true;
		}
		mRecentlyReceivedMessageHashes[hash] = static_cast<uint32_t>(time(nullptr));
	}

	IndicateConfigChanged(RsConfigMgr::CheckPriority::SAVE_NOW);

	RsItem *item = _serialiser->deserialise( const_cast<uint8_t*>(data), &dataSize );
	RsMsgItem *msg_item = dynamic_cast<RsMsgItem*>(item);

	if(msg_item)
	{
		Dbg3() << __PRETTY_FUNCTION__ << " Encrypted item correctly "
		       << "deserialised. Passing on to incoming list."
		       << std::endl;

		msg_item->msgFlags |= RS_MSG_FLAGS_DISTANT;
		/* we expect complete msgs - remove partial flag just in case
		 * someone has funny ideas */
		msg_item->msgFlags &= ~RS_MSG_FLAGS_PARTIAL;

		// hack to pass on GXS id.
		msg_item->PeerId(RsPeerId(authorId));

        handleIncomingItem(msg_item,
                           Rs::Msgs::MsgAddress(authorId,Rs::Msgs::MsgAddress::MSG_ADDRESS_MODE_TO),
                           Rs::Msgs::MsgAddress(recipientId,Rs::Msgs::MsgAddress::MSG_ADDRESS_MODE_TO));
    }
	else
	{
		RsWarn() << __PRETTY_FUNCTION__ << " Item could not be "
		         << "deserialised. Format error?" << std::endl;
		return false;
	}

	return true;
}

bool p3MsgService::notifyGxsTransSendStatus( RsGxsTransId mailId,
                                             GxsTransSendStatus status )
{
    Dbg2() << __PRETTY_FUNCTION__ << " " << mailId << ", "
           << static_cast<uint32_t>(status) << std::endl;

    auto pEvent = std::make_shared<RsMailStatusEvent>();

    RsErr() << __PRETTY_FUNCTION__ << " GXS Trans mail notification "
            << "mailId: " << mailId
            << " status: " << static_cast<uint32_t>(status);

    uint32_t msg_id;

    {
        RS_STACK_MUTEX(gxsOngoingMutex);

        auto it = gxsOngoingMessages.find(mailId);
        if(it == gxsOngoingMessages.end())
        {
            RsErr() << __PRETTY_FUNCTION__
                    << " cannot find pending message to notify"
                    << std::endl;
            return false;
        }

        msg_id = it->second;
    }
    std::cerr << " message id = " << msg_id << std::endl;


    if( status == GxsTransSendStatus::RECEIPT_RECEIVED )
    {
        pEvent->mMailStatusEventCode = RsMailStatusEventCode::MESSAGE_RECEIVED_ACK;

        // We should now remove the item from the msgOutgoing list. msgOutgoing is indexed by the original msg, not its copy, so we need
        // a linear search. It's bad, but really doesn't happen very often.

        RS_STACK_MUTEX(mMsgMtx);
        bool found = false;

        for(auto it=msgOutgoing.begin();it!=msgOutgoing.end();++it)
        {
            auto mit = it->second.find(msg_id);

            if(mit != it->second.end())
            {
                it->second.erase(mit);

                pEvent->mChangedMsgIds.insert(std::to_string(msg_id));
                found = true;
            }

            break;
        }

        if(!found)
            RsInfo() << __PRETTY_FUNCTION__ << " " << mailId
                     << ", " << static_cast<uint32_t>(status)
                     << " received receipt for message that is not in "
                     << "outgoing list, probably it has been acknoweldged "
                     << "before by other means." << std::endl;
        else
            IndicateConfigChanged(RsConfigMgr::CheckPriority::SAVE_NOW);
    }
    else if( status >= GxsTransSendStatus::FAILED_RECEIPT_SIGNATURE )
    {
        pEvent->mMailStatusEventCode = RsMailStatusEventCode::SIGNATURE_FAILED;

        RS_STACK_MUTEX(mMsgMtx);
        bool found = false;

        for(auto it=msgOutgoing.begin();it!=msgOutgoing.end();++it)
        {
            auto mit = it->second.find(msg_id);

            if(mit != it->second.end())
            {
                mit->second.flags &= ~RS_MSG_FLAGS_ROUTED; // forces re-send.

                pEvent->mChangedMsgIds.insert(std::to_string(msg_id));
                found = true;
            }
            break;
        }

        if(!found)
            RsWarn() << __PRETTY_FUNCTION__ << " " << mailId
                     << ", " << static_cast<uint32_t>(status)
                     << " received delivery error for message that is not in "
                     << "outgoing list. " << std::endl;
        else
            IndicateConfigChanged(RsConfigMgr::CheckPriority::SAVE_NOW);

    }

    if(rsEvents && !pEvent->mChangedMsgIds.empty())
        rsEvents->postEvent(pEvent);

    return true;
}

void p3MsgService::receiveGRouterData( const RsGxsId &destination_key,
                                       const RsGxsId &signing_key,
                                       GRouterServiceId &/*client_id*/,
                                       uint8_t *data, uint32_t data_size )
{
	std::cerr << "p3MsgService::receiveGRouterData(): received message item of"
	          << " size " << data_size << ", for key " << destination_key
	          << std::endl;

	/* first make sure that we havn't already received the data. Since we allow
	 * to re-send messages, it's necessary to check. */

	Sha1CheckSum hash = RsDirUtil::sha1sum(data, data_size);

	{
		RS_STACK_MUTEX(recentlyReceivedMutex);
		if( mRecentlyReceivedMessageHashes.find(hash) !=
		        mRecentlyReceivedMessageHashes.end() )
		{
			std::cerr << "p3MsgService::receiveGRouterData(...) (II) receiving"
			          << "distant message of hash " << hash << " more than once"
			          << ". Probably it has arrived  before by other means."
			          << std::endl;
			free(data);
			return;
		}
		mRecentlyReceivedMessageHashes[hash] = time(NULL);
	}

	IndicateConfigChanged(RsConfigMgr::CheckPriority::SAVE_NOW) ;

	RsItem *item = _serialiser->deserialise(data,&data_size) ;
	free(data) ;

	RsMsgItem *msg_item = dynamic_cast<RsMsgItem*>(item) ;

	if(msg_item != NULL)
	{
		std::cerr << "  Encrypted item correctly deserialised. Passing on to incoming list." << std::endl;

		msg_item->msgFlags |= RS_MSG_FLAGS_DISTANT ;
		/* we expect complete msgs - remove partial flag just in case someone has funny ideas */
		msg_item->msgFlags &= ~RS_MSG_FLAGS_PARTIAL;

		msg_item->PeerId(RsPeerId(signing_key)) ;	// hack to pass on GXS id.

        handleIncomingItem(msg_item,
                           Rs::Msgs::MsgAddress(signing_key,    Rs::Msgs::MsgAddress::MSG_ADDRESS_MODE_TO),
                           Rs::Msgs::MsgAddress(destination_key,Rs::Msgs::MsgAddress::MSG_ADDRESS_MODE_TO));
    }
	else
		std::cerr << "  Item could not be deserialised. Format error??" << std::endl;
}

void p3MsgService::locked_sendDistantMsgItem(RsMsgItem *msgitem,const RsGxsId& signing_key_id,uint32_t msgId)
{
	RsGxsId destination_key_id(msgitem->PeerId());

    if(signing_key_id.isNull())
    {
        std::cerr << "ERROR: cannot find signing key id for msg id " << msgitem->msgId << " available keys are:" << std::endl;
        return;
    }
#ifdef DEBUG_DISTANT_MSG
	std::cerr << "p3MsgService::sendDistanteMsgItem(): sending distant msg item"
	          << " msg ID: " << msgitem->msgId << " to peer:"
	          << destination_key_id << " signing: " << signing_key_id
	          << std::endl;
#endif

	/* The item is serialized and turned into a generic turtle item. Use use the
	 * explicit serialiser to make sure that the msgId is not included */

    uint32_t msg_serialized_rssize = RsMsgSerialiser().size(msgitem);
    RsTemporaryMemory msg_serialized_data(msg_serialized_rssize) ;

	if( !RsMsgSerialiser().
	        serialise(msgitem,msg_serialized_data,&msg_serialized_rssize) )
    {
        std::cerr << "(EE) p3MsgService::sendTurtleData(): Serialization error." << std::endl;
        return ;
    }
#ifdef DEBUG_DISTANT_MSG
	std::cerr << "  serialised size : " << msg_serialized_rssize << std::endl;
#endif

	GRouterMsgPropagationId grouter_message_id;
	mGRouter->sendData( destination_key_id, GROUTER_CLIENT_ID_MESSAGES,
	                    msg_serialized_data, msg_serialized_rssize,
	                    signing_key_id, grouter_message_id );
	RsGxsTransId gxsMailId;
	mGxsTransServ.sendData( gxsMailId, GxsTransSubServices::P3_MSG_SERVICE,
	                         signing_key_id, destination_key_id,
	                         msg_serialized_data, msg_serialized_rssize );

	/* now store the grouter id along with the message id, so that we can keep
	 * track of received messages */

    _grouter_ongoing_messages[grouter_message_id] = msgId;
    gxsOngoingMessages[gxsMailId] = msgId;

	IndicateConfigChanged(RsConfigMgr::CheckPriority::SAVE_NOW); // save _ongoing_messages
}

RsMsgItem *p3MsgService::createOutgoingMessageItem(const RsMailStorageItem& msi,const Rs::Msgs::MsgAddress& to)
{
    RsMsgItem *item = new RsMsgItem;

    *item = msi.msg;

    // Clear bcc except for own ids

    std::set<RsPeerId> remaining_peers;

    for(auto d:item->rspeerid_msgbcc.ids)
        if(d == rsPeers->getOwnId())
            remaining_peers.insert(d);

    item->rspeerid_msgbcc.ids = remaining_peers;

    std::set<RsGxsId> remaining_gpeers;

    for(auto d:item->rsgxsid_msgbcc.ids)
        if(rsIdentity->isOwnId(d))
            remaining_gpeers.insert(d);

    item->rsgxsid_msgbcc.ids = remaining_gpeers;

    if(to.type()==MsgAddress::MSG_ADDRESS_TYPE_RSGXSID)
        item->PeerId(RsPeerId(to.toGxsId()));
    else if(to.type()==MsgAddress::MSG_ADDRESS_TYPE_RSPEERID)
        item->PeerId(to.toRsPeerId());
    else
    {
        RsErr() << "Error: address for message is not a GxsId nor a PeerId: \"" << to.toStdString() << "\"";
        return nullptr;
    }
    return item;
}

void p3MsgService::debug_dump()
{
    std::cerr << "Dump of p3MsgService data:" << std::endl;
    auto display_box = [=](const std::map<uint32_t,RsMailStorageItem*>& msgs,const std::string& box_name) {
    std::cerr << "  " + box_name + ":" << std::endl;
    for(auto msg:msgs)
        std::cerr << "    " << msg.first << ": from " << msg.second->from.toStdString() << " to " << msg.second->to.toStdString() << " flags: " << msg.second->msg.msgFlags << " destinations: "
                  << msg.second->msg.rsgxsid_msgto.ids.size()
                    +msg.second->msg.rsgxsid_msgcc.ids.size()
                    +msg.second->msg.rsgxsid_msgbcc.ids.size()
                    +msg.second->msg.rspeerid_msgto.ids.size()
                    +msg.second->msg.rspeerid_msgcc.ids.size()
                    +msg.second->msg.rspeerid_msgbcc.ids.size() << " subject:\"" << msg.second->msg.subject << "\"" << std::endl;
    };

    display_box(mReceivedMessages,"Received");
    display_box(mSentMessages,"Sent");
    display_box(mTrashMessages,"Trash");
    display_box(mDraftMessages,"Draft");

    std::cerr << "  Outgoing:" << std::endl;

    for(auto msg:msgOutgoing)
    {
        std::cerr << "    Original message: " << msg.first << ":" << std::endl;

        for(auto msg2:msg.second)
            std::cerr << "      " << msg2.first << ": from " << msg2.second.origin.toStdString() << " to " << msg2.second.destination.toStdString() << " flags:" << msg2.second.flags << std::endl;
    }
}


