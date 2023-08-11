/*******************************************************************************
 * libretroshare/src/services: p3gxsforums.cc                                  *
 *                                                                             *
 * libretroshare: retroshare core library                                      *
 *                                                                             *
 * Copyright (C) 2012-2014  Robert Fernie <retroshare@lunamutt.com>            *
 * Copyright (C) 2018-2021  Gioacchino Mazzurco <gio@eigenlab.org>             *
 * Copyright (C) 2019-2021  Asociación Civil Altermundi <info@altermundi.net>  *
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

#include <cstdio>
#include <memory>

#include "services/p3gxsforums.h"
#include "rsitems/rsgxsforumitems.h"
#include "retroshare/rspeers.h"
#include "retroshare/rsidentity.h"
#include "util/rsdebug.h"
#include "rsserver/p3face.h"
#include "retroshare/rsnotify.h"
#include "util/rsdebuglevel2.h"
#include "retroshare/rsgxsflags.h"


// For Dummy Msgs.
#include "util/rsrandom.h"
#include "util/rsstring.h"

/****
 * #define GXSFORUM_DEBUG 1
 ****/

RsGxsForums *rsGxsForums = NULL;

#define FORUM_TESTEVENT_DUMMYDATA	0x0001
#define DUMMYDATA_PERIOD		60	// long enough for some RsIdentities to be generated.
#define FORUM_UNUSED_BY_FRIENDS_DELAY (2*30*86400) 		// unused forums are deleted after 2 months

/********************************************************************************/
/******************* Startup / Tick    ******************************************/
/********************************************************************************/

p3GxsForums::p3GxsForums( RsGeneralDataService *gds,
                          RsNetworkExchangeService *nes, RsGixs* gixs ) :
    RsGenExchange( gds, nes, new RsGxsForumSerialiser(),
                   RS_SERVICE_GXS_TYPE_FORUMS, gixs, forumsAuthenPolicy()),
    RsGxsForums(static_cast<RsGxsIface&>(*this)), mGenToken(0),
    mGenActive(false), mGenCount(0),
    mKnownForumsMutex("GXS forums known forums timestamp cache")
#ifdef RS_DEEP_FORUMS_INDEX
    , mDeepIndex(DeepForumsIndex::dbDefaultPath())
#endif
{
	// Test Data disabled in Repo.
	//RsTickEvent::schedule_in(FORUM_TESTEVENT_DUMMYDATA, DUMMYDATA_PERIOD);
}


const std::string GXS_FORUMS_APP_NAME = "gxsforums";
const uint16_t GXS_FORUMS_APP_MAJOR_VERSION  =       1;
const uint16_t GXS_FORUMS_APP_MINOR_VERSION  =       0;
const uint16_t GXS_FORUMS_MIN_MAJOR_VERSION  =       1;
const uint16_t GXS_FORUMS_MIN_MINOR_VERSION  =       0;

RsServiceInfo p3GxsForums::getServiceInfo()
{
        return RsServiceInfo(RS_SERVICE_GXS_TYPE_FORUMS,
                GXS_FORUMS_APP_NAME,
                GXS_FORUMS_APP_MAJOR_VERSION,
                GXS_FORUMS_APP_MINOR_VERSION,
                GXS_FORUMS_MIN_MAJOR_VERSION,
                GXS_FORUMS_MIN_MINOR_VERSION);
}


uint32_t p3GxsForums::forumsAuthenPolicy()
{
	uint32_t policy = 0;
	uint32_t flag = GXS_SERV::MSG_AUTHEN_ROOT_AUTHOR_SIGN | GXS_SERV::MSG_AUTHEN_CHILD_AUTHOR_SIGN;
	RsGenExchange::setAuthenPolicyFlag(flag, policy, RsGenExchange::PUBLIC_GRP_BITS);

	flag |= GXS_SERV::MSG_AUTHEN_ROOT_PUBLISH_SIGN | GXS_SERV::MSG_AUTHEN_CHILD_PUBLISH_SIGN;
	RsGenExchange::setAuthenPolicyFlag(flag, policy, RsGenExchange::RESTRICTED_GRP_BITS);
	RsGenExchange::setAuthenPolicyFlag(flag, policy, RsGenExchange::PRIVATE_GRP_BITS);

	flag = 0;
	RsGenExchange::setAuthenPolicyFlag(flag, policy, RsGenExchange::GRP_OPTION_BITS);

	return policy;
}

static const uint32_t GXS_FORUMS_CONFIG_MAX_TIME_NOTIFY_STORAGE = 86400*30*2 ; // ignore notifications for 2 months
static const uint8_t  GXS_FORUMS_CONFIG_SUBTYPE_NOTIFY_RECORD   = 0x01 ;

struct RsGxsForumNotifyRecordsItem: public RsItem
{

	RsGxsForumNotifyRecordsItem()
	    : RsItem(RS_PKT_VERSION_SERVICE,RS_SERVICE_GXS_TYPE_FORUMS_CONFIG,GXS_FORUMS_CONFIG_SUBTYPE_NOTIFY_RECORD)
	{}

    virtual ~RsGxsForumNotifyRecordsItem() {}

	void serial_process( RsGenericSerializer::SerializeJob j,
	                     RsGenericSerializer::SerializeContext& ctx )
	{ RS_SERIAL_PROCESS(records); }

	void clear() {}

	std::map<RsGxsGroupId,rstime_t> records;
};

class GxsForumsConfigSerializer : public RsServiceSerializer
{
public:
	GxsForumsConfigSerializer() : RsServiceSerializer(RS_SERVICE_GXS_TYPE_FORUMS_CONFIG) {}
	virtual ~GxsForumsConfigSerializer() {}

	RsItem* create_item(uint16_t service_id, uint8_t item_sub_id) const
	{
		if(service_id != RS_SERVICE_GXS_TYPE_FORUMS_CONFIG)
			return NULL;

		switch(item_sub_id)
		{
		case GXS_FORUMS_CONFIG_SUBTYPE_NOTIFY_RECORD: return new RsGxsForumNotifyRecordsItem();
		default:
			return NULL;
		}
	}
};

bool p3GxsForums::saveList(bool &cleanup, std::list<RsItem *>&saveList)
{
	cleanup = true ;

	RsGxsForumNotifyRecordsItem *item = new RsGxsForumNotifyRecordsItem ;

    {
        RS_STACK_MUTEX(mKnownForumsMutex);
        item->records = mKnownForums ;
    }

	saveList.push_back(item) ;
	return true;
}

bool p3GxsForums::loadList(std::list<RsItem *>& loadList)
{
	while(!loadList.empty())
	{
		RsItem *item = loadList.front();
		loadList.pop_front();

		rstime_t now = time(NULL);

		RsGxsForumNotifyRecordsItem *fnr = dynamic_cast<RsGxsForumNotifyRecordsItem*>(item) ;

		if(fnr != NULL)
		{
            RS_STACK_MUTEX(mKnownForumsMutex);

			mKnownForums.clear();

			for(auto it(fnr->records.begin());it!=fnr->records.end();++it)
				if( now < it->second + GXS_FORUMS_CONFIG_MAX_TIME_NOTIFY_STORAGE)
					mKnownForums.insert(*it) ;
		}

		delete item ;
	}
	return true;
}

RsSerialiser* p3GxsForums::setupSerialiser()
{
	RsSerialiser* rss = new RsSerialiser;
	rss->addSerialType(new GxsForumsConfigSerializer());

	return rss;
}

void p3GxsForums::notifyChanges(std::vector<RsGxsNotify*>& changes)
{
	RS_DBG2(changes.size(), " changes to notify");

	for(RsGxsNotify* gxsChange: changes)
	{
		// Let the compiler delete the change for us
		std::unique_ptr<RsGxsNotify> gxsChangeDeleter(gxsChange);

		switch(gxsChange->getType())
		{
		case RsGxsNotify::TYPE_RECEIVED_NEW: // [[fallthrough]]
		case RsGxsNotify::TYPE_PUBLISHED:
		{
			auto msgChange = dynamic_cast<RsGxsMsgChange*>(gxsChange);

			if(msgChange) /* Message received */
			{
				uint8_t msgSubtype = msgChange->mNewMsgItem->PacketSubType();
				switch(static_cast<RsGxsForumsItems>(msgSubtype))
				{
				case RsGxsForumsItems::MESSAGE_ITEM:
				{
					auto newForumMessageItem =
					        dynamic_cast<RsGxsForumMsgItem*>(
					            msgChange->mNewMsgItem );

					if(!newForumMessageItem)
					{
						RS_ERR("Received message change with mNewMsgItem type "
						       "mismatching or null");
						print_stacktrace();
						return;
					}

#ifdef RS_DEEP_FORUMS_INDEX
					RsGxsForumMsg tmpPost = newForumMessageItem->mMsg;
					tmpPost.mMeta = newForumMessageItem->meta;
					mDeepIndex.indexForumPost(tmpPost);
#endif
					auto ev = std::make_shared<RsGxsForumEvent>();
					ev->mForumMsgId = msgChange->mMsgId;
					ev->mForumGroupId = msgChange->mGroupId;
					ev->mForumEventCode = RsForumEventCode::NEW_MESSAGE;
					rsEvents->postEvent(ev);
					break;
				}
				default:
					RS_WARN("Got unknown gxs message subtype: ", msgSubtype);
					break;
				}
			}

			auto groupChange = dynamic_cast<RsGxsGroupChange*>(gxsChange);
			if(groupChange) /* Group received */
			{
				bool unknown;
				{
					RS_STACK_MUTEX(mKnownForumsMutex);
					unknown = ( mKnownForums.find(gxsChange->mGroupId)
					            == mKnownForums.end() );
					mKnownForums[gxsChange->mGroupId] = time(nullptr);
					IndicateConfigChanged();
				}

				if(unknown)
				{
					auto ev = std::make_shared<RsGxsForumEvent>();
					ev->mForumGroupId = gxsChange->mGroupId;
					ev->mForumEventCode = RsForumEventCode::NEW_FORUM;
					rsEvents->postEvent(ev);
				}

#ifdef RS_DEEP_FORUMS_INDEX
				uint8_t itemType = groupChange->mNewGroupItem->PacketSubType();
				switch(static_cast<RsGxsForumsItems>(itemType))
				{
				case RsGxsForumsItems::GROUP_ITEM:
				{
					auto newForumGroupItem =
					        static_cast<RsGxsForumGroupItem*>(
					            groupChange->mNewGroupItem );
					mDeepIndex.indexForumGroup(newForumGroupItem->mGroup);
					break;
				}
				default:
					RS_WARN("Got unknown gxs group subtype: ", itemType);
					break;
				}
#endif // def RS_DEEP_FORUMS_INDEX

			}
			break;
		}
		case RsGxsNotify::TYPE_PROCESSED: // happens when the group is subscribed
		{
			auto ev = std::make_shared<RsGxsForumEvent>();
			ev->mForumGroupId = gxsChange->mGroupId;
			ev->mForumEventCode = RsForumEventCode::SUBSCRIBE_STATUS_CHANGED;
			rsEvents->postEvent(ev);
			break;
		}
		case RsGxsNotify::TYPE_GROUP_SYNC_PARAMETERS_UPDATED:
		{
			auto ev = std::make_shared<RsGxsForumEvent>();
			ev->mForumGroupId = gxsChange->mGroupId;
			ev->mForumEventCode = RsForumEventCode::SYNC_PARAMETERS_UPDATED;
			rsEvents->postEvent(ev);
			break;
		}
		case RsGxsNotify::TYPE_MESSAGE_DELETED:
		{
			auto delChange = dynamic_cast<RsGxsMsgDeletedChange*>(gxsChange);
			if(!delChange)
			{
				RS_ERR( "Got mismatching notification type: ",
				        gxsChange->getType() );
				print_stacktrace();
				break;
			}

#ifdef RS_DEEP_FORUMS_INDEX
			mDeepIndex.removeForumPostFromIndex(
			            delChange->mGroupId, delChange->messageId);
#endif

			auto ev = std::make_shared<RsGxsForumEvent>();
			ev->mForumEventCode = RsForumEventCode::DELETED_POST;
			ev->mForumGroupId = delChange->mGroupId;
			ev->mForumMsgId = delChange->messageId;
			break;
		}
		case RsGxsNotify::TYPE_GROUP_DELETED:
		{
#ifdef RS_DEEP_FORUMS_INDEX
			mDeepIndex.removeForumFromIndex(gxsChange->mGroupId);
#endif
			auto ev = std::make_shared<RsGxsForumEvent>();
			ev->mForumGroupId = gxsChange->mGroupId;
			ev->mForumEventCode = RsForumEventCode::DELETED_FORUM;
			rsEvents->postEvent(ev);
			break;
		}
		case RsGxsNotify::TYPE_STATISTICS_CHANGED:
		{
			auto ev = std::make_shared<RsGxsForumEvent>();
			ev->mForumGroupId = gxsChange->mGroupId;
			ev->mForumEventCode = RsForumEventCode::STATISTICS_CHANGED;
			rsEvents->postEvent(ev);

			RS_STACK_MUTEX(mKnownForumsMutex);
			mKnownForums[gxsChange->mGroupId] = time(nullptr);
			IndicateConfigChanged();
			break;
		}
		case RsGxsNotify::TYPE_UPDATED:
		{
			/* Happens when the group data has changed. In this case we need to
			 * analyse the old and new group in order to detect possible
			 * notifications for clients */

			auto grpChange = dynamic_cast<RsGxsGroupChange*>(gxsChange);

			RsGxsForumGroupItem* old_forum_grp_item =
			        dynamic_cast<RsGxsForumGroupItem*>(grpChange->mOldGroupItem);
			RsGxsForumGroupItem* new_forum_grp_item =
			        dynamic_cast<RsGxsForumGroupItem*>(grpChange->mNewGroupItem);

			if( old_forum_grp_item == nullptr || new_forum_grp_item == nullptr)
			{
				RS_ERR( "received GxsGroupUpdate item with mOldGroup and "
				        "mNewGroup not of type RsGxsForumGroupItem or NULL. "
				        "This is inconsistent!");
				print_stacktrace();
				break;
			}

#ifdef RS_DEEP_FORUMS_INDEX
			mDeepIndex.indexForumGroup(new_forum_grp_item->mGroup);
#endif

			/* First of all, we check if there is a difference between the old
			 * and new list of moderators */

			std::list<RsGxsId> added_mods, removed_mods;
			for(auto& gxs_id: new_forum_grp_item->mGroup.mAdminList.ids)
				if( old_forum_grp_item->mGroup.mAdminList.ids.find(gxs_id)
				        == old_forum_grp_item->mGroup.mAdminList.ids.end() )
					added_mods.push_back(gxs_id);

			for(auto& gxs_id: old_forum_grp_item->mGroup.mAdminList.ids)
				if( new_forum_grp_item->mGroup.mAdminList.ids.find(gxs_id)
				        == new_forum_grp_item->mGroup.mAdminList.ids.end() )
					removed_mods.push_back(gxs_id);

			if(!added_mods.empty() || !removed_mods.empty())
			{
				auto ev = std::make_shared<RsGxsForumEvent>();

				ev->mForumGroupId = new_forum_grp_item->meta.mGroupId;
				ev->mModeratorsAdded = added_mods;
				ev->mModeratorsRemoved = removed_mods;
				ev->mForumEventCode = RsForumEventCode::MODERATOR_LIST_CHANGED;

				rsEvents->postEvent(ev);
			}

			// check the list of pinned posts
			std::list<RsGxsMessageId> added_pins, removed_pins;

			for(auto& msg_id: new_forum_grp_item->mGroup.mPinnedPosts.ids)
				if( old_forum_grp_item->mGroup.mPinnedPosts.ids.find(msg_id)
				        == old_forum_grp_item->mGroup.mPinnedPosts.ids.end() )
					added_pins.push_back(msg_id);

			for(auto& msg_id: old_forum_grp_item->mGroup.mPinnedPosts.ids)
				if( new_forum_grp_item->mGroup.mPinnedPosts.ids.find(msg_id)
				        == new_forum_grp_item->mGroup.mPinnedPosts.ids.end() )
					removed_pins.push_back(msg_id);

			if(!added_pins.empty() || !removed_pins.empty())
			{
				auto ev = std::make_shared<RsGxsForumEvent>();
				ev->mForumGroupId = new_forum_grp_item->meta.mGroupId;
				ev->mForumEventCode = RsForumEventCode::PINNED_POSTS_CHANGED;
				rsEvents->postEvent(ev);
			}

			if( old_forum_grp_item->mGroup.mDescription != new_forum_grp_item->mGroup.mDescription
			        || old_forum_grp_item->meta.mGroupName  != new_forum_grp_item->meta.mGroupName
			        || old_forum_grp_item->meta.mGroupFlags != new_forum_grp_item->meta.mGroupFlags
			        || old_forum_grp_item->meta.mAuthorId   != new_forum_grp_item->meta.mAuthorId
			        || old_forum_grp_item->meta.mCircleId   != new_forum_grp_item->meta.mCircleId )
			{
				auto ev = std::make_shared<RsGxsForumEvent>();
				ev->mForumGroupId = new_forum_grp_item->meta.mGroupId;
				ev->mForumEventCode = RsForumEventCode::UPDATED_FORUM;
				rsEvents->postEvent(ev);
			}

			break;
		}

		default:
			RS_ERR( "Got a GXS event of type ", gxsChange->getType(),
			        " Currently not handled." );
			break;
		}
	}
}

void	p3GxsForums::service_tick()
{
	dummy_tick();
	RsTickEvent::tick_events();
	return;
}

rstime_t p3GxsForums::service_getLastGroupSeenTs(const RsGxsGroupId& gid)
{
     rstime_t now = time(nullptr);

    RS_STACK_MUTEX(mKnownForumsMutex);

    auto it = mKnownForums.find(gid);
    bool unknown_forum = it == mKnownForums.end();

    if(unknown_forum)
    {
        mKnownForums[gid] = now;
        IndicateConfigChanged();
        return now;
    }
    else
        return it->second;
}
bool p3GxsForums::service_checkIfGroupIsStillUsed(const RsGxsGrpMetaData& meta)
{
#ifdef GXSFORUMS_DEBUG
    std::cerr << "p3gxsForums: Checking unused forums: called by GxsCleaning." << std::endl;
#endif

    // request all group infos at once

    rstime_t now = time(nullptr);

    RS_STACK_MUTEX(mKnownForumsMutex);

    auto it = mKnownForums.find(meta.mGroupId);
    bool unknown_forum = it == mKnownForums.end();

#ifdef GXSFORUMS_DEBUG
    std::cerr << "  Forum " << meta.mGroupId ;
#endif

    if(unknown_forum)
    {
        // This case should normally not happen. It does because this forum was never registered since it may
        // arrived before this code was here

#ifdef GXSFORUMS_DEBUG
        std::cerr << ". Not known yet. Adding current time as new TS." << std::endl;
#endif
        mKnownForums[meta.mGroupId] = now;
        IndicateConfigChanged();

        return true;
    }
    else
    {
        bool used_by_friends = (now < it->second + FORUM_UNUSED_BY_FRIENDS_DELAY);
        bool subscribed = static_cast<bool>(meta.mSubscribeFlags & GXS_SERV::GROUP_SUBSCRIBE_SUBSCRIBED);

#ifdef GXSFORUMS_DEBUG
        std::cerr << ". subscribed: " << subscribed << ", used_by_friends: " << used_by_friends << " last TS: " << now - it->second << " secs ago (" << (now-it->second)/86400 << " days)";
#endif

        if(!subscribed && !used_by_friends)
        {
#ifdef GXSFORUMS_DEBUG
            std::cerr << ". Scheduling for deletion" << std::endl;
#endif
            return false;
        }
        else
        {
#ifdef GXSFORUMS_DEBUG
            std::cerr << ". Keeping!" << std::endl;
#endif
            return true;
        }
    }
}
bool p3GxsForums::getGroupData(const uint32_t &token, std::vector<RsGxsForumGroup> &groups)
{
	std::vector<RsGxsGrpItem*> grpData;
	bool ok = RsGenExchange::getGroupData(token, grpData);
		
	if(ok)
	{
		std::vector<RsGxsGrpItem*>::iterator vit = grpData.begin();
		
		for(; vit != grpData.end(); ++vit)
		{
			RsGxsForumGroupItem* item = dynamic_cast<RsGxsForumGroupItem*>(*vit);
			if (item)
			{
				RsGxsForumGroup grp = item->mGroup;
				grp.mMeta = item->meta;
				delete item;
				groups.push_back(grp);
			}
			else
			{
				std::cerr << "Not a GxsForumGrpItem, deleting!" << std::endl;
				delete *vit;
			}
		}
	}
	return ok;
}

bool p3GxsForums::getMsgMetaData(const uint32_t &token, GxsMsgMetaMap& msg_metas)
{
	return RsGenExchange::getMsgMeta(token, msg_metas);
}

/* Okay - chris is not going to be happy with this...
 * but I can't be bothered with crazy data structures
 * at the moment - fix it up later
 */

bool p3GxsForums::getMsgData(const uint32_t &token, std::vector<RsGxsForumMsg> &msgs)
{
	GxsMsgDataMap msgData;
	bool ok = RsGenExchange::getMsgData(token, msgData);

	if(ok)
	{
		GxsMsgDataMap::iterator mit = msgData.begin();

		for(; mit != msgData.end(); ++mit)
		{
			std::vector<RsGxsMsgItem*>& msgItems = mit->second;
			std::vector<RsGxsMsgItem*>::iterator vit = msgItems.begin();

			for(; vit != msgItems.end(); ++vit)
			{
				RsGxsForumMsgItem* item = dynamic_cast<RsGxsForumMsgItem*>(*vit);

				if(item)
				{
					RsGxsForumMsg msg = item->mMsg;
					msg.mMeta = item->meta;
					msgs.push_back(msg);
					delete item;
				}
				else
				{
					std::cerr << "Not a GxsForumMsgItem, deleting!" << std::endl;
					delete *vit;
				}
			}
		}
	}

	return ok;
}

bool p3GxsForums::createForumV2(
        const std::string& name, const std::string& description,
        const RsGxsId& authorId, const std::set<RsGxsId>& moderatorsIds,
        RsGxsCircleType circleType, const RsGxsCircleId& circleId,
        RsGxsGroupId& forumId, std::string& errorMessage )
{
	auto createFail = [&](std::string mErr)
	{
		errorMessage = mErr;
		RsErr() << __PRETTY_FUNCTION__ << " " << errorMessage << std::endl;
		return false;
	};

	if(name.empty()) return createFail("Forum name is required");

	if(!authorId.isNull() && !rsIdentity->isOwnId(authorId))
		return createFail("Author must be iether null or and identity owned by "
		                  "this node");

	switch(circleType)
	{
	case RsGxsCircleType::PUBLIC: // fallthrough
	case RsGxsCircleType::LOCAL: // fallthrough
	case RsGxsCircleType::YOUR_EYES_ONLY:
		break;
	case RsGxsCircleType::EXTERNAL:
		if(circleId.isNull())
			return createFail("circleType is EXTERNAL but circleId is null");
		break;
	case RsGxsCircleType::NODES_GROUP:
	{
		RsGroupInfo ginfo;
		if(!rsPeers->getGroupInfo(RsNodeGroupId(circleId), ginfo))
			return createFail("circleType is NODES_GROUP but circleId does not "
			                  "correspond to an actual group of friends");
		break;
	}
	default: return createFail("circleType has invalid value");
	}

	// Create a consistent channel group meta from the information supplied
	RsGxsForumGroup forum;

	forum.mMeta.mGroupName = name;
	forum.mMeta.mAuthorId = authorId;
	forum.mMeta.mCircleType = static_cast<uint32_t>(circleType);

	forum.mMeta.mSignFlags = GXS_SERV::FLAG_GROUP_SIGN_PUBLISH_NONEREQ
	        | GXS_SERV::FLAG_AUTHOR_AUTHENTICATION_REQUIRED;

	/* This flag have always this value even for circle restricted forums due to
	 * how GXS distribute/verify groups */
	forum.mMeta.mGroupFlags = GXS_SERV::FLAG_PRIVACY_PUBLIC;

	forum.mMeta.mCircleId.clear();
	forum.mMeta.mInternalCircle.clear();

	switch(circleType)
	{
	case RsGxsCircleType::NODES_GROUP:
		forum.mMeta.mInternalCircle = circleId; break;
	case RsGxsCircleType::EXTERNAL:
		forum.mMeta.mCircleId = circleId; break;
	default: break;
	}

	forum.mDescription = description;
	forum.mAdminList.ids = moderatorsIds;

	uint32_t token;
	if(!createGroup(token, forum))
		return createFail("Failed creating GXS group.");

	// wait for the group creation to complete.
	RsTokenService::GxsRequestStatus wSt =
	        waitToken( token, std::chrono::milliseconds(5000),
	                   std::chrono::milliseconds(20) );
	if(wSt != RsTokenService::COMPLETE)
		return createFail( "GXS operation waitToken failed with: "
		                   + std::to_string(wSt) );

	if(!RsGenExchange::getPublishedGroupMeta(token, forum.mMeta))
		return createFail("Failure getting updated group data.");

	forumId = forum.mMeta.mGroupId;

	return true;
}

bool p3GxsForums::createPost(
        const RsGxsGroupId& forumId, const std::string& title,
        const std::string& mBody,
        const RsGxsId& authorId, const RsGxsMessageId& parentId,
        const RsGxsMessageId& origPostId, RsGxsMessageId& postMsgId,
        std::string& errorMessage )
{
	RsGxsForumMsg post;

	auto failure = [&](std::string errMsg)
	{
		errorMessage = errMsg;
		RsErr() << __PRETTY_FUNCTION__ << " " << errorMessage << std::endl;
		return false;
	};

	if(title.empty()) return failure("Title is required");

	if(authorId.isNull()) return failure("Author id is needed");

	if(!rsIdentity->isOwnId(authorId))
		return failure( "Author id: " + authorId.toStdString() + " is not of"
		                "own identity" );

	if(!parentId.isNull())
	{
		std::vector<RsGxsForumMsg> msgs;
		if( getForumContent(forumId, std::set<RsGxsMessageId>({parentId}), msgs)
		        && msgs.size() == 1 )
		{
			post.mMeta.mParentId = parentId;
			post.mMeta.mThreadId = msgs[0].mMeta.mThreadId;
		}
		else return failure("Parent post " + parentId.toStdString()
		                    + " doesn't exists locally");
	}

	std::vector<RsGxsForumGroup> forumInfo;
	if(!getForumsInfo(std::list<RsGxsGroupId>({forumId}), forumInfo))
		return failure( "Forum with Id " + forumId.toStdString()
		                + " does not exist locally." );

	if(!origPostId.isNull())
	{
		std::vector<RsGxsForumMsg> msgs;
		if( getForumContent( forumId,
		                     std::set<RsGxsMessageId>({origPostId}), msgs)
		        && msgs.size() == 1 )
			post.mMeta.mOrigMsgId = origPostId;
		else return failure("Original post " + origPostId.toStdString()
		                    + " doesn't exists locally");
	}

	post.mMeta.mGroupId  = forumId;
	post.mMeta.mMsgName = title;
	post.mMeta.mAuthorId = authorId;
	post.mMsg = mBody;

	uint32_t token;
	if( !createMsg(token, post)
	        || waitToken(
	            token,
	            std::chrono::milliseconds(5000) ) != RsTokenService::COMPLETE )
		return failure("Failure creating GXS message");

	if(!RsGenExchange::getPublishedMsgMeta(token, post.mMeta))
		return failure("Failure getting created GXS message metadata");

	postMsgId = post.mMeta.mMsgId;
	return true;
}

bool p3GxsForums::createForum(RsGxsForumGroup& forum)
{
	uint32_t token;
	if(!createGroup(token, forum))
	{
		std::cerr << __PRETTY_FUNCTION__ << "Error! Failed creating group."
		          << std::endl;
		return false;
	}

	if(waitToken(token,std::chrono::milliseconds(5000)) != RsTokenService::COMPLETE)
	{
		std::cerr << __PRETTY_FUNCTION__ << "Error! GXS operation failed."
		          << std::endl;
		return false;
	}

	if(!RsGenExchange::getPublishedGroupMeta(token, forum.mMeta))
	{
		std::cerr << __PRETTY_FUNCTION__ << "Error! Failure getting updated "
		          << " group data." << std::endl;
		return false;
	}

	return true;
}

bool p3GxsForums::editForum(RsGxsForumGroup& forum)
{
	uint32_t token;
	if(!updateGroup(token, forum))
	{
		std::cerr << __PRETTY_FUNCTION__ << "Error! Failed updating group."
		          << std::endl;
		return false;
	}

	if(waitToken(token,std::chrono::milliseconds(5000)) != RsTokenService::COMPLETE)
	{
		std::cerr << __PRETTY_FUNCTION__ << "Error! GXS operation failed."
		          << std::endl;
		return false;
	}

	if(!RsGenExchange::getPublishedGroupMeta(token, forum.mMeta))
	{
		std::cerr << __PRETTY_FUNCTION__ << "Error! Failure getting updated "
		          << " group data." << std::endl;
		return false;
	}

	return true;
}

bool p3GxsForums::getForumsSummaries( std::list<RsGroupMetaData>& forums )
{
	uint32_t token;
	RsTokReqOptions opts;
	opts.mReqType = GXS_REQUEST_TYPE_GROUP_META;
	if( !requestGroupInfo(token, opts)
	        || waitToken(token,std::chrono::milliseconds(5000)) != RsTokenService::COMPLETE ) return false;
	return getGroupSummary(token, forums);
}

bool p3GxsForums::getForumsInfo( const std::list<RsGxsGroupId>& forumIds, std::vector<RsGxsForumGroup>& forumsInfo )
{
	uint32_t token;
	RsTokReqOptions opts;
	opts.mReqType = GXS_REQUEST_TYPE_GROUP_DATA;

    if(forumIds.empty())
    {
		if( !requestGroupInfo(token, opts) || waitToken(token,std::chrono::milliseconds(5000)) != RsTokenService::COMPLETE )
            return false;
    }
	else
    {
		if( !requestGroupInfo(token, opts, forumIds, forumIds.size()==1) || waitToken(token,std::chrono::milliseconds(5000)) != RsTokenService::COMPLETE )
            return false;
    }
	return getGroupData(token, forumsInfo);
}

bool p3GxsForums::getForumContent(
                    const RsGxsGroupId& forumId,
                    const std::set<RsGxsMessageId>& msgs_to_request,
                    std::vector<RsGxsForumMsg>& msgs )
{
	uint32_t token;
	RsTokReqOptions opts;
	opts.mReqType = GXS_REQUEST_TYPE_MSG_DATA;

	GxsMsgReq msgIds;
	msgIds[forumId] = msgs_to_request;

    if( !requestMsgInfo(token, opts, msgIds) || waitToken(token,std::chrono::seconds(5)) != RsTokenService::COMPLETE )
		return false;

	return getMsgData(token, msgs);
}


bool p3GxsForums::getForumMsgMetaData(const RsGxsGroupId& forumId, std::vector<RsMsgMetaData>& msg_metas)
{
	uint32_t token;
	RsTokReqOptions opts;
	opts.mReqType = GXS_REQUEST_TYPE_MSG_META;

    GxsMsgMetaMap meta_map;
    std::list<RsGxsGroupId> forumIds;
    forumIds.push_back(forumId);

	if( !requestMsgInfo(token, opts, forumIds) || waitToken(token,std::chrono::milliseconds(5000)) != RsTokenService::COMPLETE ) return false;

	bool res = getMsgMetaData(token, meta_map);

    msg_metas = meta_map[forumId];

    return res;
}

bool p3GxsForums::markRead(const RsGxsGrpMsgIdPair& msgId, bool read)
{
	uint32_t token;
	setMessageReadStatus(token, msgId, read);
	if(waitToken(token,std::chrono::milliseconds(5000)) != RsTokenService::COMPLETE ) return false;

    RsGxsGrpMsgIdPair p;
    acknowledgeMsg(token,p);

	return true;
}

bool p3GxsForums::subscribeToForum(const RsGxsGroupId& groupId, bool subscribe )
{
	uint32_t token;
	if( !RsGenExchange::subscribeToGroup(token, groupId, subscribe) ||
	        waitToken(token) != RsTokenService::COMPLETE ) return false;

	RsGxsGroupId grp;
	acknowledgeGrp(token, grp);

	/* Since subscribe has been requested, the caller is most probably
	 * interested in getting the group messages ASAP so check updates from peers
	 * without waiting GXS sync timer.
	 * Do it here as this is meaningful or not depending on the service.
	 * Do it only after the token has been completed otherwise the pull have no
	 * effect. */
	if(subscribe) RsGenExchange::netService()->checkUpdatesFromPeers();

	return true;
}

bool p3GxsForums::exportForumLink(
        std::string& link, const RsGxsGroupId& forumId, bool includeGxsData,
        const std::string& baseUrl, std::string& errMsg )
{
	constexpr auto fname = __PRETTY_FUNCTION__;
	const auto failure = [&](const std::string& err)
	{
		errMsg = err;
		RsErr() << fname << " " << err << std::endl;
		return false;
	};

	if(forumId.isNull()) return failure("forumId cannot be null");

	const bool outputRadix = baseUrl.empty();
	if(outputRadix && !includeGxsData) return
	        failure("includeGxsData must be true if format requested is base64");

	if( includeGxsData &&
	        !RsGenExchange::exportGroupBase64(link, forumId, errMsg) )
		return failure(errMsg);

	if(outputRadix) return true;

	std::vector<RsGxsForumGroup> forumsInfo;
	if( !getForumsInfo(std::list<RsGxsGroupId>({forumId}), forumsInfo)
	        || forumsInfo.empty() )
		return failure("failure retrieving forum information");

	RsUrl inviteUrl(baseUrl);
	inviteUrl.setQueryKV(FORUM_URL_ID_FIELD, forumId.toStdString());
	inviteUrl.setQueryKV(FORUM_URL_NAME_FIELD, forumsInfo[0].mMeta.mGroupName);
	if(includeGxsData) inviteUrl.setQueryKV(FORUM_URL_DATA_FIELD, link);

	link = inviteUrl.toString();
	return true;
}

bool p3GxsForums::importForumLink(
        const std::string& link, RsGxsGroupId& forumId, std::string& errMsg )
{
	constexpr auto fname = __PRETTY_FUNCTION__;
	const auto failure = [&](const std::string& err)
	{
		errMsg = err;
		RsErr() << fname << " " << err << std::endl;
		return false;
	};

	if(link.empty()) return failure("link is empty");

	const std::string* radixPtr(&link);

	RsUrl url(link);
	const auto& query = url.query();
	const auto qIt = query.find(FORUM_URL_DATA_FIELD);
	if(qIt != query.end()) radixPtr = &qIt->second;

	if(radixPtr->empty()) return failure(FORUM_URL_DATA_FIELD + " is empty");

	if(!RsGenExchange::importGroupBase64(*radixPtr, forumId, errMsg))
		return failure(errMsg);

	return true;
}

std::error_condition p3GxsForums::getChildPosts(
        const RsGxsGroupId& forumId, const RsGxsMessageId& parentId,
        std::vector<RsGxsForumMsg>& childPosts )
{
	RS_DBG3("forumId: ", forumId, " parentId: ", parentId);

	if(forumId.isNull() || parentId.isNull())
		return std::errc::invalid_argument;

	std::vector<RsGxsGrpMsgIdPair> msgIds;
	msgIds.push_back(RsGxsGrpMsgIdPair(forumId, parentId));

	RsTokReqOptions opts;
	opts.mReqType = GXS_REQUEST_TYPE_MSG_RELATED_DATA;
	opts.mOptions = RS_TOKREQOPT_MSG_PARENT | RS_TOKREQOPT_MSG_LATEST;

	uint32_t token;
	if( !requestMsgRelatedInfo(token, opts, msgIds) ||
	        waitToken(token) != RsTokenService::COMPLETE )
		return std::errc::timed_out;

	GxsMsgRelatedDataMap msgData;
	if(!getMsgRelatedData(token, msgData))
		return std::errc::no_message_available;

	for(auto& mit: msgData)
	{
		for(auto& vit: mit.second)
		{
			auto msgItem = dynamic_cast<RsGxsForumMsgItem*>(vit);
			if(msgItem)
			{
				RsGxsForumMsg post = msgItem->mMsg;
				post.mMeta = msgItem->meta;
				childPosts.push_back(post);
			}
			else RS_WARN("Got item of unexpected type: ", vit);

			delete vit;
		}
	}

	return std::error_condition();
}

bool p3GxsForums::createGroup(uint32_t &token, RsGxsForumGroup &group)
{
	std::cerr << "p3GxsForums::createGroup()" << std::endl;

	RsGxsForumGroupItem* grpItem = new RsGxsForumGroupItem();
	grpItem->mGroup = group;
	grpItem->meta = group.mMeta;

	RsGenExchange::publishGroup(token, grpItem);
	return true;
}

bool p3GxsForums::getForumServiceStatistics(GxsServiceStatistic& stat)
{
    uint32_t token;
	if(!RsGxsIfaceHelper::requestServiceStatistic(token) || waitToken(token) != RsTokenService::COMPLETE)
        return false;

    return RsGenExchange::getServiceStatistic(token,stat);
}

bool p3GxsForums::getForumGroupStatistics(const RsGxsGroupId& ForumId,GxsGroupStatistic& stat)
{
	uint32_t token;
	if(!RsGxsIfaceHelper::requestGroupStatistic(token, ForumId) || waitToken(token) != RsTokenService::COMPLETE)
        return false;

    return RsGenExchange::getGroupStatistic(token,stat);
}

bool p3GxsForums::getForumStatistics(const RsGxsGroupId& forumId,RsGxsForumStatistics& stat)
{
    // 1 - get group data

    std::vector<RsGxsForumGroup> groups;

    if(!getForumsInfo(std::list<RsGxsGroupId>{ forumId },groups) || groups.size() != 1)
    {
        std::cerr << __PRETTY_FUNCTION__ << " failed to retrieve forum group info for forum " << forumId << std::endl;
        return false;
    }

    // 2 - sort messages into a proper hierarchy, discarding old versions

    std::map<RsGxsMessageId,std::vector<std::pair<rstime_t, RsGxsMessageId> > > post_versions;
    std::vector<ForumPostEntry> vect ;

    if(!getForumPostsHierarchy(groups[0],vect,post_versions))
    {
        std::cerr << __PRETTY_FUNCTION__ << " failed to retrieve forum hierarchy of message info for forum " << forumId << std::endl;
        return false;
    }

    // 3 - now compute the actual statistics

    if(vect.empty())		// This should never happen since the hierachy always contain a toplevel sentinel post.
        return false;

    stat.mNumberOfMessages = vect.size()-1;
    stat.mNumberOfNewMessages = 0;
    stat.mNumberOfUnreadMessages = 0;

    for(uint32_t i=1;i<vect.size();++i)
    {
        const auto& f(vect[i].mMsgStatus);

        if(IS_MSG_NEW(f))    ++stat.mNumberOfNewMessages;
        if(IS_MSG_UNREAD(f)) ++stat.mNumberOfUnreadMessages;
    }

    return true;
}

bool p3GxsForums::getForumPostsHierarchy(const RsGxsForumGroup& group,
                                         std::vector<ForumPostEntry>& vect,
                                         std::map<RsGxsMessageId,std::vector<std::pair<rstime_t, RsGxsMessageId> > >& post_versions)
{
    post_versions.clear();
    vect.clear();

    std::vector<RsMsgMetaData> msg_metas;

    if(!getForumMsgMetaData(group.mMeta.mGroupId,msg_metas))
    {
        RsErr() << __PRETTY_FUNCTION__ << " failed to retrieve forum message info for forum " << group.mMeta.mGroupId ;
        return false;
    }

    computeMessagesHierarchy(group,msg_metas,vect,post_versions);

    return true;
}

static bool decreasing_time_comp(const std::pair<time_t,RsGxsMessageId>& e1,const std::pair<time_t,RsGxsMessageId>& e2) { return e2.first < e1.first ; }

void p3GxsForums::updateReputationLevel(uint32_t forum_sign_flags,ForumPostEntry& fentry) const
{
    uint32_t idflags =0;
    RsReputationLevel reputation_level =
            rsReputations->overallReputationLevel(fentry.mAuthorId, &idflags);

    if(reputation_level == RsReputationLevel::LOCALLY_NEGATIVE)
        fentry.mPostFlags |=  ForumPostEntry::FLAG_POST_IS_REDACTED;
    else
        fentry.mPostFlags &= ~ForumPostEntry::FLAG_POST_IS_REDACTED;

    // We use a specific item model for forums in order to handle the post pinning.

    if(reputation_level == RsReputationLevel::UNKNOWN)
        fentry.mReputationWarningLevel = 3 ;
    else if(reputation_level == RsReputationLevel::LOCALLY_NEGATIVE)
        fentry.mReputationWarningLevel = 2 ;
    else if(reputation_level < rsGxsForums->minReputationForForwardingMessages(forum_sign_flags,idflags))
        fentry.mReputationWarningLevel = 1 ;
    else
        fentry.mReputationWarningLevel = 0 ;
}

void p3GxsForums::computeMessagesHierarchy(const RsGxsForumGroup& forum_group,
                                               const std::vector<RsMsgMetaData>& msgs_metas_array,
                                               std::vector<ForumPostEntry>& posts,
                                               std::map<RsGxsMessageId,std::vector<std::pair<rstime_t,RsGxsMessageId> > >& mPostVersions )
{
#ifdef GXSFORUMS_DEBUG
    RsDbg() << "updating messages data with " << msgs_metas_array.size() << " messages";
#endif

    auto addEntry = [&posts](const ForumPostEntry& entry,uint32_t parent) -> uint32_t
    {
        uint32_t N = posts.size();
        posts.push_back(entry);

        posts[N].mParent = parent;
        posts[parent].mChildren.push_back(N);
#ifdef DEBUG_FORUMMODEL
        std::cerr << "Added new entry " << N << " children of " << parent << std::endl;
#endif
        if(N == parent)
            std::cerr << "(EE) trying to add a post as its own parent!" << std::endl;

        return N;
    };

    auto convertMsgToPostEntry = [this](const RsGxsForumGroup& mForumGroup,const RsMsgMetaData& msg, ForumPostEntry& fentry)
    {
        fentry.mTitle     = msg.mMsgName;
        fentry.mAuthorId  = msg.mAuthorId;
        fentry.mMsgId     = msg.mMsgId;
        fentry.mPublishTs = msg.mPublishTs;
        fentry.mPostFlags = 0;
        fentry.mMsgStatus = msg.mMsgStatus;

        if(mForumGroup.mPinnedPosts.ids.find(msg.mMsgId) != mForumGroup.mPinnedPosts.ids.end())
            fentry.mPostFlags |= ForumPostEntry::FLAG_POST_IS_PINNED;

        // Early check for a message that should be hidden because its author
        // is flagged with a bad reputation

        updateReputationLevel(mForumGroup.mMeta.mSignFlags,fentry);
    };
    auto generateMissingItem = [](const RsGxsMessageId &msgId,ForumPostEntry& entry)
    {
        entry.mPostFlags = ForumPostEntry::FLAG_POST_IS_MISSING ;
        entry.mTitle = std::string("[ ... Missing Message ... ]");
        entry.mMsgId = msgId;
        entry.mAuthorId.clear();
        entry.mPublishTs=0;
        entry.mReputationWarningLevel = 3;
    };

#ifdef DEBUG_FORUMS
    std::cerr << "Retrieved group data: " << std::endl;
    std::cerr << "  Group ID: " << forum_group.mMeta.mGroupId << std::endl;
    std::cerr << "  Admin lst: " << forum_group.mAdminList.ids.size() << " elements." << std::endl;
    for(auto it(forum_group.mAdminList.ids.begin());it!=forum_group.mAdminList.ids.end();++it)
        std::cerr << "    " << *it << std::endl;
    std::cerr << "  Pinned Post: " << forum_group.mPinnedPosts.ids.size() << " messages." << std::endl;
    for(auto it(forum_group.mPinnedPosts.ids.begin());it!=forum_group.mPinnedPosts.ids.end();++it)
        std::cerr << "    " << *it << std::endl;
#endif

    /* get messages */
    std::map<RsGxsMessageId,RsMsgMetaData> msgs;

    for(uint32_t i=0;i<msgs_metas_array.size();++i)
    {
#ifdef DEBUG_FORUMS
        std::cerr << "Adding message " << msgs_metas_array[i].mMeta.mMsgId << " with parent " << msgs_metas_array[i].mMeta.mParentId << " to message map" << std::endl;
#endif
        msgs[msgs_metas_array[i].mMsgId] = msgs_metas_array[i] ;
    }

#ifdef DEBUG_FORUMS
    size_t count = msgs.size();
#endif

    // Set a sentinel parent for all top-level posts.

    posts.resize(1);	// adds a sentinel item
    posts[0].mTitle = "Root sentinel post" ;
    posts[0].mParent = 0;

    // ThreadList contains the list of parent threads. The algorithm below iterates through all messages
    // and tries to establish parenthood relationships between them, given that we only know the
    // immediate parent of a message and now its children. Some messages have a missing parent and for them
    // a fake top level parent is generated.

    // In order to be efficient, we first create a structure that lists the children of every mesage ID in the list.
    // Then the hierarchy of message is build by attaching the kids to every message until all of them have been processed.
    // The messages with missing parents will be the last ones remaining in the list.

    std::list<std::pair< RsGxsMessageId, uint32_t > > threadStack;
    std::map<RsGxsMessageId,std::list<RsGxsMessageId> > kids_array ;
    std::set<RsGxsMessageId> missing_parents;

    // First of all, remove all older versions of posts. This is done by first adding all posts into a hierarchy structure
    // and then removing all posts which have a new versions available. The older versions are kept appart.

#ifdef DEBUG_FORUMS
    std::cerr << "GxsForumsFillThread::run() Collecting post versions" << std::endl;
#endif
    mPostVersions.clear();

    for ( auto msgIt = msgs.begin(); msgIt != msgs.end();++msgIt)
    {
        if(!msgIt->second.mOrigMsgId.isNull() && msgIt->second.mOrigMsgId != msgIt->second.mMsgId)
        {
#ifdef DEBUG_FORUMS
            std::cerr << "  Post " << msgIt->second.mMeta.mMsgId << " is a new version of " << msgIt->second.mMeta.mOrigMsgId << std::endl;
#endif
            auto msgIt2 = msgs.find(msgIt->second.mOrigMsgId);

            // Ensuring that the post exists allows to only collect the existing data.

            if(msgIt2 == msgs.end())
                continue ;

            // Make sure that the author is the same than the original message, or is a moderator. This should always happen when messages are constructed using
            // the UI but nothing can prevent a nasty user to craft a new version of a message with his own signature.

            if(msgIt2->second.mAuthorId != msgIt->second.mAuthorId)
            {
                if( !IS_FORUM_MSG_MODERATION(msgIt->second.mMsgFlags) )			// if authors are different the moderation flag needs to be set on the editing msg
                    continue ;

                if( !forum_group.canEditPosts(msgIt->second.mAuthorId))			// if author is not a moderator, continue
                    continue ;
            }

            // always add the post a self version

            if(mPostVersions[msgIt->second.mOrigMsgId].empty())
                mPostVersions[msgIt->second.mOrigMsgId].push_back(std::make_pair(msgIt2->second.mPublishTs,msgIt2->second.mMsgId)) ;

            mPostVersions[msgIt->second.mOrigMsgId].push_back(std::make_pair(msgIt->second.mPublishTs,msgIt->second.mMsgId)) ;
        }
    }

    // The following code assembles all new versions of a given post into the same array, indexed by the oldest version of the post.

    for(auto it(mPostVersions.begin());it!=mPostVersions.end();++it)
    {
        auto& v(it->second) ;

        for(size_t i=0;i<v.size();++i)
        {
            if(v[i].second != it->first)
            {
                RsGxsMessageId sub_msg_id = v[i].second ;

                auto it2 = mPostVersions.find(sub_msg_id);

                if(it2 != mPostVersions.end())
                {
                    for(size_t j=0;j<it2->second.size();++j)
                        if(it2->second[j].second != sub_msg_id)	// dont copy it, since it is already present at slot i
                            v.push_back(it2->second[j]) ;

                    mPostVersions.erase(it2) ;	// it2 is never equal to it
                }
            }
        }
    }


    // Now remove from msg ids, all posts except the most recent one. And make the mPostVersion be indexed by the most recent version of the post,
    // which corresponds to the item in the tree widget.

#ifdef DEBUG_FORUMS
    std::cerr << "Final post versions: " << std::endl;
#endif
    std::map<RsGxsMessageId,std::vector<std::pair<rstime_t,RsGxsMessageId> > > mTmp;
    std::map<RsGxsMessageId,RsGxsMessageId> most_recent_versions ;

    for(auto it(mPostVersions.begin());it!=mPostVersions.end();++it)
    {
#ifdef DEBUG_FORUMS
        std::cerr << "Original post: " << it.key() << std::endl;
#endif
        // Finally, sort the posts from newer to older

        std::sort(it->second.begin(),it->second.end(),decreasing_time_comp) ;

#ifdef DEBUG_FORUMS
        std::cerr << "   most recent version " << (*it)[0].first << "  " << (*it)[0].second << std::endl;
#endif
        for(size_t i=1;i<it->second.size();++i)
        {
            msgs.erase(it->second[i].second) ;

#ifdef DEBUG_FORUMS
            std::cerr << "   older version " << (*it)[i].first << "  " << (*it)[i].second << std::endl;
#endif
        }

        mTmp[it->second[0].second] = it->second ;	// index the versions map by the ID of the most recent post.

        // Now make sure that message parents are consistent. Indeed, an old post may have the old version of a post as parent. So we need to change that parent
        // to the newest version. So we create a map of which is the most recent version of each message, so that parent messages can be searched in it.

    for(size_t i=1;i<it->second.size();++i)
        most_recent_versions[it->second[i].second] = it->second[0].second ;
    }
    mPostVersions = mTmp ;

    // The next step is to find the top level thread messages. These are defined as the messages without
    // any parent message ID.

    // this trick is needed because while we remove messages, the parents a given msg may already have been removed
    // and wrongly understand as a missing parent.

    std::map<RsGxsMessageId,RsMsgMetaData> kept_msgs;

    for ( auto msgIt = msgs.begin(); msgIt != msgs.end();++msgIt)
    {

        if(msgIt->second.mParentId.isNull())
        {

            /* add all threads */
            const RsMsgMetaData& msg = msgIt->second;

#ifdef DEBUG_FORUMS
            std::cerr << "GxsForumsFillThread::run() Adding TopLevel Thread: mId: " << msg.mMsgId << std::endl;
#endif

            ForumPostEntry entry;
            convertMsgToPostEntry(forum_group,msg, entry);

            uint32_t entry_index = addEntry(entry,0);

            threadStack.push_back(std::make_pair(msg.mMsgId,entry_index)) ;
        }
        else
        {
#ifdef DEBUG_FORUMS
            std::cerr << "GxsForumsFillThread::run() Storing kid " << msgIt->first << " of message " << msgIt->second.mParentId << std::endl;
#endif
            // The same missing parent may appear multiple times, so we first store them into a unique container.

            RsGxsMessageId parent_msg = msgIt->second.mParentId;

            if(msgs.find(parent_msg) == msgs.end())
            {
                // also check that the message is not versionned

                std::map<RsGxsMessageId,RsGxsMessageId>::const_iterator mrit = most_recent_versions.find(parent_msg) ;

                if(mrit != most_recent_versions.end())
                    parent_msg = mrit->second ;
                else
                    missing_parents.insert(parent_msg);
            }

            kids_array[parent_msg].push_back(msgIt->first) ;
            kept_msgs.insert(*msgIt) ;
        }
    }

    msgs = kept_msgs;

    // Also create a list of posts by time, when they are new versions of existing posts. Only the last one will have an item created.

    // Add a fake toplevel item for the parent IDs that we dont actually have.

    for(std::set<RsGxsMessageId>::const_iterator it(missing_parents.begin());it!=missing_parents.end();++it)
    {
        // add dummy parent item
        ForumPostEntry e ;
        generateMissingItem(*it,e);

        uint32_t e_index = addEntry(e,0);	// no parent -> parent is level 0
        //mItems.append( e_index );

        threadStack.push_back(std::make_pair(*it,e_index)) ;
    }
#ifdef DEBUG_FORUMS
    std::cerr << "GxsForumsFillThread::run() Processing stack:" << std::endl;
#endif
    // Now use a stack to go down the hierarchy

    while (!threadStack.empty())
    {
        std::pair<RsGxsMessageId, uint32_t> threadPair = threadStack.front();
        threadStack.pop_front();

        std::map<RsGxsMessageId, std::list<RsGxsMessageId> >::iterator it = kids_array.find(threadPair.first) ;

#ifdef DEBUG_FORUMS
        std::cerr << "GxsForumsFillThread::run() Node: " << threadPair.first << std::endl;
#endif
        if(it == kids_array.end())
            continue ;


        for(std::list<RsGxsMessageId>::const_iterator it2(it->second.begin());it2!=it->second.end();++it2)
        {
            // We iterate through the top level thread items, and look for which message has the current item as parent.
            // When found, the item is put in the thread list itself, as a potential new parent.

            auto mit = msgs.find(*it2) ;

            if(mit == msgs.end())
            {
                std::cerr << "GxsForumsFillThread::run()    Cannot find submessage " << *it2 << " !!!" << std::endl;
                continue ;
            }

            const RsMsgMetaData& msg(mit->second) ;
#ifdef DEBUG_FORUMS
            std::cerr << "GxsForumsFillThread::run()    adding sub_item " << msg.mMsgId << std::endl;
#endif


            ForumPostEntry e ;
            convertMsgToPostEntry(forum_group,msg,e) ;
            uint32_t e_index = addEntry(e, threadPair.second);

            //calculateExpand(msg, item);

            /* add item to process list */
            threadStack.push_back(std::make_pair(msg.mMsgId, e_index));

            msgs.erase(mit);
        }

#ifdef DEBUG_FORUMS
        std::cerr << "GxsForumsFillThread::run() Erasing entry " << it->first << " from kids tab." << std::endl;
#endif
        kids_array.erase(it) ; // This is not strictly needed, but it improves performance by reducing the search space.
    }

#ifdef DEBUG_FORUMS
    std::cerr << "Kids array now has " << kids_array.size() << " elements" << std::endl;
    for(std::map<RsGxsMessageId,std::list<RsGxsMessageId> >::const_iterator it(kids_array.begin());it!=kids_array.end();++it)
    {
        std::cerr << "Node " << it->first << std::endl;
        for(std::list<RsGxsMessageId>::const_iterator it2(it->second.begin());it2!=it->second.end();++it2)
            std::cerr << "  " << *it2 << std::endl;
    }

    std::cerr << "GxsForumsFillThread::run() stopped: " << (wasStopped() ? "yes" : "no") << std::endl;
#endif
}


bool p3GxsForums::updateGroup(uint32_t &token, const RsGxsForumGroup &group)
{
	std::cerr << "p3GxsForums::updateGroup()" << std::endl;


        if(group.mMeta.mGroupId.isNull())
		return false;

	RsGxsForumGroupItem* grpItem = new RsGxsForumGroupItem();
	grpItem->mGroup = group;
        grpItem->meta = group.mMeta;

        RsGenExchange::updateGroup(token, grpItem);
	return true;
}

bool p3GxsForums::createMessage(RsGxsForumMsg& message)
{
	uint32_t token;
	if( !createMsg(token, message)
	        || waitToken(token,std::chrono::milliseconds(5000)) != RsTokenService::COMPLETE ) return false;

	if(RsGenExchange::getPublishedMsgMeta(token, message.mMeta)) return true;

	return false;
}

bool p3GxsForums::createMsg(uint32_t &token, RsGxsForumMsg &msg)
{
	std::cerr << "p3GxsForums::createForumMsg() GroupId: " << msg.mMeta.mGroupId;
	std::cerr << std::endl;

	RsGxsForumMsgItem* msgItem = new RsGxsForumMsgItem();
	msgItem->mMsg = msg;
	msgItem->meta = msg.mMeta;
	
	RsGenExchange::publishMsg(token, msgItem);
	return true;
}


/********************************************************************************************/
/********************************************************************************************/

void p3GxsForums::setMessageReadStatus(uint32_t& token, const RsGxsGrpMsgIdPair& msgId, bool read)
{
	uint32_t mask = GXS_SERV::GXS_MSG_STATUS_GUI_NEW | GXS_SERV::GXS_MSG_STATUS_GUI_UNREAD;
	uint32_t status = GXS_SERV::GXS_MSG_STATUS_GUI_UNREAD;
	if (read)
		status = 0;

	setMsgStatusFlags(token, msgId, status, mask);

	/* WARNING: The event may be received before the operation is completed!
	 * TODO: move notification to blocking method markRead(...) which wait the
	 * operation to complete */
	if (rsEvents)
	{
		auto ev = std::make_shared<RsGxsForumEvent>();

		ev->mForumMsgId = msgId.second;
		ev->mForumGroupId = msgId.first;
		ev->mForumEventCode = RsForumEventCode::READ_STATUS_CHANGED;
		rsEvents->postEvent(ev);
	}
}

/********************************************************************************************/
/********************************************************************************************/

std::error_condition p3GxsForums::setPostKeepForever(
        const RsGxsGroupId& forumId, const RsGxsMessageId& postId,
        bool keepForever )
{
	if(forumId.isNull() || postId.isNull()) return std::errc::invalid_argument;

	uint32_t mask = GXS_SERV::GXS_MSG_STATUS_KEEP_FOREVER;
	uint32_t status = keepForever ? GXS_SERV::GXS_MSG_STATUS_KEEP_FOREVER : 0;

	uint32_t token;
	setMsgStatusFlags(token, RsGxsGrpMsgIdPair(forumId, postId), status, mask);

	switch(waitToken(token))
	{
	case RsTokenService::PENDING: // [[fallthrough]];
	case RsTokenService::PARTIAL: return std::errc::timed_out;
	case RsTokenService::COMPLETE: // [[fallthrough]];
	case RsTokenService::DONE:
	{
		auto ev = std::make_shared<RsGxsForumEvent>();
		ev->mForumGroupId = forumId;
		ev->mForumMsgId = postId;
		ev->mForumEventCode = RsForumEventCode::UPDATED_MESSAGE;
		rsEvents->postEvent(ev);
		return std::error_condition();
	}
	case RsTokenService::CANCELLED: return std::errc::operation_canceled;
	default: return std::errc::bad_message;
	}
}

std::error_condition p3GxsForums::requestSynchronization()
{
	auto errc = RsGenExchange::netService()->checkUpdatesFromPeers();
	if(errc) return errc;
	return RsGenExchange::netService()->requestPull();
}

/* so we need the same tick idea as wiki for generating dummy forums
 */

#define 	MAX_GEN_GROUPS		5
#define 	MAX_GEN_MESSAGES	100

std::string p3GxsForums::genRandomId()
{
        std::string randomId;
        for(int i = 0; i < 20; i++)
        {
                randomId += (char) ('a' + (RSRandom::random_u32() % 26));
        }

        return randomId;
}

bool p3GxsForums::generateDummyData()
{
	mGenCount = 0;
	mGenRefs.resize(MAX_GEN_MESSAGES);

	std::string groupName;
	rs_sprintf(groupName, "TestForum_%d", mGenCount);

	std::cerr << "p3GxsForums::generateDummyData() Starting off with Group: " << groupName;
	std::cerr << std::endl;

	/* create a new group */
	generateGroup(mGenToken, groupName);

	mGenActive = true;

	return true;
}


void p3GxsForums::dummy_tick()
{
	/* check for a new callback */

	if (mGenActive)
	{
		std::cerr << "p3GxsForums::dummyTick() AboutActive";
		std::cerr << std::endl;

		uint32_t status = RsGenExchange::getTokenService()->requestStatus(mGenToken);
		if (status != RsTokenService::COMPLETE)
		{
			std::cerr << "p3GxsForums::dummy_tick() Status: " << status;
			std::cerr << std::endl;

			if (status == RsTokenService::FAILED)
			{
				std::cerr << "p3GxsForums::dummy_tick() generateDummyMsgs() FAILED";
				std::cerr << std::endl;
				mGenActive = false;
			}
			return;
		}

		if (mGenCount < MAX_GEN_GROUPS)
		{
			/* get the group Id */
			RsGxsGroupId groupId;
			RsGxsMessageId emptyId;
			if (!acknowledgeTokenGrp(mGenToken, groupId))
			{
				std::cerr << " ERROR ";
				std::cerr << std::endl;
				mGenActive = false;
				return;
			}

			std::cerr << "p3GxsForums::dummy_tick() Acknowledged GroupId: " << groupId;
			std::cerr << std::endl;

			ForumDummyRef ref(groupId, emptyId, emptyId);
			mGenRefs[mGenCount] = ref;
		}
		else if (mGenCount < MAX_GEN_MESSAGES)
		{
			/* get the msg Id, and generate next snapshot */
			RsGxsGrpMsgIdPair msgId;
			if (!acknowledgeTokenMsg(mGenToken, msgId))
			{
				std::cerr << " ERROR ";
				std::cerr << std::endl;
				mGenActive = false;
				return;
			}

			std::cerr << "p3GxsForums::dummy_tick() Acknowledged <GroupId: " << msgId.first << ", MsgId: " << msgId.second << ">";
			std::cerr << std::endl;

			/* store results for later selection */

			ForumDummyRef ref(msgId.first, mGenThreadId, msgId.second);
			mGenRefs[mGenCount] = ref;
		}
		else
		{
			std::cerr << "p3GxsForums::dummy_tick() Finished";
			std::cerr << std::endl;

			/* done */
			mGenActive = false;
			return;
		}

		mGenCount++;

		if (mGenCount < MAX_GEN_GROUPS)
		{
			std::string groupName;
			rs_sprintf(groupName, "TestForum_%d", mGenCount);

			std::cerr << "p3GxsForums::dummy_tick() Generating Group: " << groupName;
			std::cerr << std::endl;

			/* create a new group */
			generateGroup(mGenToken, groupName);
		}
		else
		{
			/* create a new message */
			uint32_t idx = (uint32_t) (mGenCount * RSRandom::random_f32());
			ForumDummyRef &ref = mGenRefs[idx];

			RsGxsGroupId grpId = ref.mGroupId;
			RsGxsMessageId parentId = ref.mMsgId;
			mGenThreadId = ref.mThreadId;
			if (mGenThreadId.isNull())
			{
				mGenThreadId = parentId;
			}

			std::cerr << "p3GxsForums::dummy_tick() Generating Msg ... ";
			std::cerr << " GroupId: " << grpId;
			std::cerr << " ThreadId: " << mGenThreadId;
			std::cerr << " ParentId: " << parentId;
			std::cerr << std::endl;

			generateMessage(mGenToken, grpId, parentId, mGenThreadId);
		}
	}
}


bool p3GxsForums::generateMessage(uint32_t &token, const RsGxsGroupId &grpId, const RsGxsMessageId &parentId, const RsGxsMessageId &threadId)
{
	RsGxsForumMsg msg;

	std::string rndId = genRandomId();

	rs_sprintf(msg.mMsg, "Forum Msg: GroupId: %s, ThreadId: %s, ParentId: %s + some randomness: %s", 
		grpId.toStdString().c_str(), threadId.toStdString().c_str(), parentId.toStdString().c_str(), rndId.c_str());
	
	msg.mMeta.mMsgName = msg.mMsg;

	msg.mMeta.mGroupId = grpId;
	msg.mMeta.mThreadId = threadId;
	msg.mMeta.mParentId = parentId;

	msg.mMeta.mMsgStatus = GXS_SERV::GXS_MSG_STATUS_UNPROCESSED;

	/* chose a random Id to sign with */
	std::list<RsGxsId> ownIds;
	std::list<RsGxsId>::iterator it;

	rsIdentity->getOwnIds(ownIds);

	uint32_t idx = (uint32_t) (ownIds.size() * RSRandom::random_f32());
    uint32_t i = 0;
	for(it = ownIds.begin(); (it != ownIds.end()) && (i < idx); ++it, i++) ;

	if (it != ownIds.end())
	{
		std::cerr << "p3GxsForums::generateMessage() Author: " << *it;
		std::cerr << std::endl;
		msg.mMeta.mAuthorId = *it;
	} 
	else
	{
		std::cerr << "p3GxsForums::generateMessage() No Author!";
		std::cerr << std::endl;
	} 

	createMsg(token, msg);

	return true;
}


bool p3GxsForums::generateGroup(uint32_t &token, std::string groupName)
{
	/* generate a new forum */
	RsGxsForumGroup forum;
	forum.mMeta.mGroupName = groupName;

	createGroup(token, forum);

	return true;
}


        // Overloaded from RsTickEvent for Event callbacks.
void p3GxsForums::handle_event(uint32_t event_type, const std::string &/*elabel*/)
{
	std::cerr << "p3GxsForums::handle_event(" << event_type << ")";
	std::cerr << std::endl;

	// stuff.
	switch(event_type)
	{
		case FORUM_TESTEVENT_DUMMYDATA:
			generateDummyData();
			break;

		default:
			/* error */
			std::cerr << "p3GxsForums::handle_event() Unknown Event Type: " << event_type;
			std::cerr << std::endl;
			break;
	}
}

void RsGxsForumGroup::serial_process(
        RsGenericSerializer::SerializeJob j,
        RsGenericSerializer::SerializeContext& ctx )
{
	RS_SERIAL_PROCESS(mMeta);
	RS_SERIAL_PROCESS(mDescription);

	/* Work around to have usable JSON API, without breaking binary
	 * serialization retrocompatibility */
	switch (j)
	{
	case RsGenericSerializer::TO_JSON: // fallthrough
	case RsGenericSerializer::FROM_JSON:
		RsTypeSerializer::serial_process( j, ctx,
		                                  mAdminList.ids, "mAdminList" );
		RsTypeSerializer::serial_process( j, ctx,
		                                  mPinnedPosts.ids, "mPinnedPosts" );
		break;
	default:
		RS_SERIAL_PROCESS(mAdminList);
		RS_SERIAL_PROCESS(mPinnedPosts);
	}
}

bool RsGxsForumGroup::canEditPosts(const RsGxsId& id) const
{
	return mAdminList.ids.find(id) != mAdminList.ids.end() ||
	        id == mMeta.mAuthorId;
}

std::error_condition p3GxsForums::getContentSummaries(
        const RsGxsGroupId& forumId,
        const std::set<RsGxsMessageId>& contentIds,
        std::vector<RsMsgMetaData>& summaries )
{
	uint32_t token;
	RsTokReqOptions opts;
	opts.mReqType = GXS_REQUEST_TYPE_MSG_META;

	GxsMsgReq msgReq;
	msgReq[forumId] = contentIds;


	if(!requestMsgInfo(token, opts, msgReq))
	{
		RS_ERR("requestMsgInfo failed");
		return std::errc::invalid_argument;
	}

	switch(waitToken(token, std::chrono::seconds(5)))
	{
	case RsTokenService::COMPLETE:
	{
		GxsMsgMetaMap metaMap;
		if(!RsGenExchange::getMsgMeta(token, metaMap))
			return std::errc::result_out_of_range;
		summaries = metaMap[forumId];
		return std::error_condition();
	}
	case RsTokenService::PARTIAL: // [[fallthrough]];
	case RsTokenService::PENDING:
		return std::errc::timed_out;
	default:
		return std::errc::not_supported;
	}
}

#ifdef RS_DEEP_FORUMS_INDEX
std::error_condition p3GxsForums::handleDistantSearchRequest(
        rs_view_ptr<uint8_t> requestData, uint32_t requestSize,
        rs_owner_ptr<uint8_t>& resultData, uint32_t& resultSize )
{
	RS_DBG1("");

	RsGxsForumsSearchRequest request;
	{
		RsGenericSerializer::SerializeContext ctx(requestData, requestSize);
		RsGenericSerializer::SerializeJob j =
		        RsGenericSerializer::SerializeJob::DESERIALIZE;
		RS_SERIAL_PROCESS(request);
	}

	if(request.mType != RsGxsForumsItems::SEARCH_REQUEST)
	{
		// If more types are implemented we would put a switch on mType instead
		RS_WARN( "Got search request with unkown type: ",
		         static_cast<uint32_t>(request.mType) );
		return std::errc::bad_message;
	}

	RsGxsForumsSearchReply reply;
	auto mErr = prepareSearchResults(request.mQuery, true, reply.mResults);
	if(mErr || reply.mResults.empty()) return mErr;

	{
		RsGenericSerializer::SerializeContext ctx;
		RsGenericSerializer::SerializeJob j =
		        RsGenericSerializer::SerializeJob::SIZE_ESTIMATE;
		RS_SERIAL_PROCESS(reply);
		resultSize = ctx.mOffset;
	}

	resultData = rs_malloc<uint8_t>(resultSize);
	RsGenericSerializer::SerializeContext ctx(resultData, resultSize);
	RsGenericSerializer::SerializeJob j =
	        RsGenericSerializer::SerializeJob::SERIALIZE;
	RS_SERIAL_PROCESS(reply);

	return std::error_condition();
}

std::error_condition p3GxsForums::distantSearchRequest(
        const std::string& matchString, TurtleRequestId& searchId )
{
	RsGxsForumsSearchRequest request;
	request.mQuery = matchString;

	uint32_t requestSize;
	{
		RsGenericSerializer::SerializeContext ctx;
		RsGenericSerializer::SerializeJob j =
		        RsGenericSerializer::SerializeJob::SIZE_ESTIMATE;
		RS_SERIAL_PROCESS(request);
		requestSize = ctx.mOffset;
	}

	std::error_condition ec;
	auto requestData = rs_malloc<uint8_t>(requestSize, &ec);
	if(!requestData) return ec;
	{
		RsGenericSerializer::SerializeContext ctx(requestData, requestSize);
		RsGenericSerializer::SerializeJob j =
		        RsGenericSerializer::SerializeJob::SERIALIZE;
		RS_SERIAL_PROCESS(request);
	}

	return netService()->distantSearchRequest(
	            requestData, requestSize,
	            static_cast<RsServiceType>(serviceType()), searchId );
}

std::error_condition p3GxsForums::localSearch(
        const std::string& matchString,
        std::vector<RsGxsSearchResult>& searchResults )
{ return prepareSearchResults(matchString, false, searchResults); }

std::error_condition p3GxsForums::prepareSearchResults(
        const std::string& matchString, bool publicOnly,
        std::vector<RsGxsSearchResult>& searchResults )
{
	std::vector<DeepForumsSearchResult> results;
	auto mErr = mDeepIndex.search(matchString, results);
	if(mErr) return mErr;

	searchResults.clear();
	for(auto uRes: results)
	{
		RsUrl resUrl(uRes.mUrl);
		const auto forumIdStr = resUrl.getQueryV(RsGxsForums::FORUM_URL_ID_FIELD);
		if(!forumIdStr)
		{
			RS_ERR( "Forum URL retrieved from deep index miss ID. ",
			        "Should never happen! ", uRes.mUrl );
			print_stacktrace();
			return std::errc::address_not_available;
		}

		std::vector<RsGxsForumGroup> forumsInfo;
		RsGxsGroupId forumId(*forumIdStr);
		if(forumId.isNull())
		{
			RS_ERR( "Forum ID retrieved from deep index is invalid. ",
			        "Should never happen! ", uRes.mUrl );
			print_stacktrace();
			return std::errc::bad_address;
		}

		if( !getForumsInfo(std::list<RsGxsGroupId>{forumId}, forumsInfo) ||
		        forumsInfo.empty() )
		{
			RS_ERR( "Forum just parsed from deep index link not found. "
			        "Should never happen! ", forumId, " ", uRes.mUrl );
			print_stacktrace();
			return std::errc::identifier_removed;
		}

		RsGroupMetaData& fMeta(forumsInfo[0].mMeta);

		// Avoid leaking sensitive information to unkown peers
		if( publicOnly &&
		        ( static_cast<RsGxsCircleType>(fMeta.mCircleType) !=
		          RsGxsCircleType::PUBLIC ) ) continue;

		RsGxsSearchResult res;
		res.mGroupId = forumId;
		res.mGroupName = fMeta.mGroupName;
		res.mAuthorId = fMeta.mAuthorId;
		res.mPublishTs = fMeta.mPublishTs;
		res.mSearchContext = uRes.mSnippet;

		auto postIdStr =
		        resUrl.getQueryV(RsGxsForums::FORUM_URL_MSG_ID_FIELD);
		if(postIdStr)
		{
			RsGxsMessageId msgId(*postIdStr);
			if(msgId.isNull())
			{
				RS_ERR( "Post just parsed from deep index link is invalid. "
				        "Should never happen! ", postIdStr, " ", uRes.mUrl );
				print_stacktrace();
				return std::errc::bad_address;
			}

			std::vector<RsMsgMetaData> msgSummaries;
			auto errc = getContentSummaries(
			            forumId, std::set<RsGxsMessageId>{msgId}, msgSummaries);
			if(errc) return errc;

			if(msgSummaries.size() != 1)
			{
				RS_ERR( "getContentSummaries returned: ", msgSummaries.size(),
				        "should never happen!" );
				return std::errc::result_out_of_range;
			}

			RsMsgMetaData& msgMeta(msgSummaries[0]);
			res.mMsgId = msgMeta.mMsgId;
			res.mMsgName = msgMeta.mMsgName;
			res.mAuthorId = msgMeta.mAuthorId;
		}

		RS_DBG4(res);
		searchResults.push_back(res);
	}

	return std::error_condition();
}

std::error_condition p3GxsForums::receiveDistantSearchResult(
        const TurtleRequestId requestId,
        rs_owner_ptr<uint8_t>& resultData, uint32_t& resultSize )
{
	RsGxsForumsSearchReply reply;
	{
		RsGenericSerializer::SerializeContext ctx(resultData, resultSize);
		RsGenericSerializer::SerializeJob j =
		        RsGenericSerializer::SerializeJob::DESERIALIZE;
		RS_SERIAL_PROCESS(reply);
	}
	free(resultData);

	if(reply.mType != RsGxsForumsItems::SEARCH_REPLY)
	{
		// If more types are implemented we would put a switch on mType instead
		RS_WARN( "Got search request with unkown type: ",
		         static_cast<uint32_t>(reply.mType) );
		return std::errc::bad_message;
	}

	auto event = std::make_shared<RsGxsForumsDistantSearchEvent>();
	event->mSearchId = requestId;
	event->mSearchResults = reply.mResults;
	rsEvents->postEvent(event);
	return std::error_condition();
}

#else  // def RS_DEEP_FORUMS_INDEX

std::error_condition p3GxsForums::distantSearchRequest(
        const std::string&, TurtleRequestId& )
{ return std::errc::function_not_supported; }

std::error_condition p3GxsForums::localSearch(
        const std::string&,
        std::vector<RsGxsSearchResult>& )
{ return std::errc::function_not_supported; }

#endif // def RS_DEEP_FORUMS_INDEX

/*static*/ const std::string RsGxsForums::DEFAULT_FORUM_BASE_URL =
        "retroshare:///forums";
/*static*/ const std::string RsGxsForums::FORUM_URL_NAME_FIELD =
        "forumName";
/*static*/ const std::string RsGxsForums::FORUM_URL_ID_FIELD =
        "forumId";
/*static*/ const std::string RsGxsForums::FORUM_URL_DATA_FIELD =
        "forumData";
/*static*/ const std::string RsGxsForums::FORUM_URL_MSG_TITLE_FIELD =
        "forumMsgTitle";
/*static*/ const std::string RsGxsForums::FORUM_URL_MSG_ID_FIELD =
        "forumMsgId";

RsGxsForumGroup::~RsGxsForumGroup() = default;
RsGxsForumMsg::~RsGxsForumMsg() = default;
RsGxsForums::~RsGxsForums() = default;
RsGxsForumEvent::~RsGxsForumEvent() = default;
