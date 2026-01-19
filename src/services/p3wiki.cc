/*******************************************************************************
 * libretroshare/src/services: p3wiki.cc                                       *
 *                                                                             *
 * libretroshare: retroshare core library                                      *
 *                                                                             *
 * Copyright 2012-2012 by Robert Fernie <retroshare@lunamutt.com>              *
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
#include "services/p3wiki.h"
#include "retroshare/rsgxsflags.h"
#include "rsitems/rswikiitems.h"
#include "util/rsrandom.h"
#include "retroshare/rsevents.h"
#include <algorithm>
#include <memory>

RsWiki *rsWiki = NULL;

p3Wiki::p3Wiki(RsGeneralDataService* gds, RsNetworkExchangeService* nes, RsGixs *gixs)
	:RsGenExchange(gds, nes, new RsGxsWikiSerialiser(), RS_SERVICE_GXS_TYPE_WIKI, gixs, wikiAuthenPolicy()), 
	RsWiki(static_cast<RsGxsIface&>(*this))
{
}

RsServiceInfo p3Wiki::getServiceInfo()
{
    return RsServiceInfo(RS_SERVICE_GXS_TYPE_WIKI, "gxswiki", 1, 0, 1, 0);
}

uint32_t p3Wiki::wikiAuthenPolicy()
{
	uint32_t policy = 0;
	uint8_t flag = GXS_SERV::MSG_AUTHEN_ROOT_PUBLISH_SIGN | GXS_SERV::MSG_AUTHEN_CHILD_AUTHOR_SIGN;
	RsGenExchange::setAuthenPolicyFlag(flag, policy, RsGenExchange::PUBLIC_GRP_BITS);
	flag |= GXS_SERV::MSG_AUTHEN_CHILD_PUBLISH_SIGN;
	RsGenExchange::setAuthenPolicyFlag(flag, policy, RsGenExchange::RESTRICTED_GRP_BITS);
	RsGenExchange::setAuthenPolicyFlag(flag, policy, RsGenExchange::PRIVATE_GRP_BITS);
	flag = 0;
	RsGenExchange::setAuthenPolicyFlag(flag, policy, RsGenExchange::GRP_OPTION_BITS);
	return policy;
}

void p3Wiki::service_tick()
{
    /* Service tick required by RsGenExchange */
}

void p3Wiki::notifyChanges(std::vector<RsGxsNotify*>& changes)
{
    if (rsEvents) {
        RsEventType wikiEventType = (RsEventType)rsEvents->getDynamicEventType("GXS_WIKI");

        for(auto change : changes) {
            std::shared_ptr<RsGxsWikiEvent> event = std::make_shared<RsGxsWikiEvent>(wikiEventType);
            event->mWikiGroupId = change->mGroupId; 
            
            if (dynamic_cast<RsGxsMsgChange*>(change)) {
                event->mWikiEventCode = RsWikiEventCode::UPDATED_SNAPSHOT;
            } else {
                // This handles new Wikis
                event->mWikiEventCode = RsWikiEventCode::UPDATED_COLLECTION;
            }
            rsEvents->postEvent(event);
            delete change;
        }
    } else {
        for(auto change : changes) delete change;
    }
    changes.clear();
}

/* GXS Data Retrieval Methods */

bool p3Wiki::getCollections(const uint32_t &token, std::vector<RsWikiCollection> &collections)
{
	std::vector<RsGxsGrpItem*> grpData;
	bool ok = RsGenExchange::getGroupData(token, grpData);
	if(ok) {
		for(auto it : grpData) {
			RsGxsWikiCollectionItem* item = dynamic_cast<RsGxsWikiCollectionItem*>(it);
			if (item) {
				RsWikiCollection collection = item->collection;
				collection.mMeta = item->meta;
				collections.push_back(collection);
			}
			delete it;
		}
	}
	return ok;
}

bool p3Wiki::getSnapshots(const uint32_t &token, std::vector<RsWikiSnapshot> &snapshots)
{
	GxsMsgDataMap msgData;
	bool ok = RsGenExchange::getMsgData(token, msgData);
	if(ok) {
		for(auto& mit : msgData) {
			for(auto vit : mit.second) {
				RsGxsWikiSnapshotItem* item = dynamic_cast<RsGxsWikiSnapshotItem*>(vit);
				if(item) {
					RsWikiSnapshot snapshot = item->snapshot;
					snapshot.mMeta = item->meta;
					snapshots.push_back(snapshot);
				}
				delete vit;
			}
		}
	}
	return ok;
}

bool p3Wiki::getRelatedSnapshots(const uint32_t &token, std::vector<RsWikiSnapshot> &snapshots)
{
    GxsMsgRelatedDataMap msgData;
	bool ok = RsGenExchange::getMsgRelatedData(token, msgData);
	if(ok) {
		for(auto& mit : msgData) {
			for(auto vit : mit.second) {
				RsGxsWikiSnapshotItem* item = dynamic_cast<RsGxsWikiSnapshotItem*>(vit);
				if(item) {
					RsWikiSnapshot snapshot = item->snapshot;
					snapshot.mMeta = item->meta;
					snapshots.push_back(snapshot);
				}
				delete vit;
			}
		}
	}
	return ok;
}

bool p3Wiki::getComments(const uint32_t &token, std::vector<RsWikiComment> &comments)
{
	GxsMsgDataMap msgData;
	bool ok = RsGenExchange::getMsgData(token, msgData);
	if(ok) {
		for(auto& mit : msgData) {
			for(auto vit : mit.second) {
				RsGxsWikiCommentItem* item = dynamic_cast<RsGxsWikiCommentItem*>(vit);
				if(item) {
					RsWikiComment comment = item->comment;
					comment.mMeta = item->meta;
					comments.push_back(comment);
				}
				delete vit;
			}
		}
	}
	return ok;
}

/* Submission Methods */

bool p3Wiki::submitCollection(uint32_t &token, RsWikiCollection &collection)
{
	RsGxsWikiCollectionItem* collectionItem = new RsGxsWikiCollectionItem();
	collectionItem->collection = collection;
	collectionItem->meta = collection.mMeta;
	RsGenExchange::publishGroup(token, collectionItem);
	return true;
}

bool p3Wiki::submitSnapshot(uint32_t &token, RsWikiSnapshot &snapshot)
{
    RsGxsWikiSnapshotItem* snapshotItem = new RsGxsWikiSnapshotItem();
    snapshotItem->snapshot = snapshot;
    snapshotItem->meta = snapshot.mMeta;
    snapshotItem->meta.mMsgFlags = FLAG_MSG_TYPE_WIKI_SNAPSHOT;
    RsGenExchange::publishMsg(token, snapshotItem);
	return true;
}

bool p3Wiki::submitComment(uint32_t &token, RsWikiComment &comment)
{
    RsGxsWikiCommentItem* commentItem = new RsGxsWikiCommentItem();
    commentItem->comment = comment;
    commentItem->meta = comment.mMeta;
    commentItem->meta.mMsgFlags = FLAG_MSG_TYPE_WIKI_COMMENT;
    RsGenExchange::publishMsg(token, commentItem);
	return true;
}

bool p3Wiki::updateCollection(uint32_t &token, RsWikiCollection &group)
{
    RsGxsWikiCollectionItem* grpItem = new RsGxsWikiCollectionItem();
    grpItem->collection = group;
    grpItem->meta = group.mMeta;
    RsGenExchange::updateGroup(token, grpItem);
    return true;
}

/* Blocking Interfaces */

bool p3Wiki::createCollection(RsWikiCollection &group)
{
	uint32_t token;
	return submitCollection(token, group) && waitToken(token) == RsTokenService::COMPLETE;
}

bool p3Wiki::updateCollection(const RsWikiCollection &group)
{
	uint32_t token;
	RsWikiCollection update(group);
	return updateCollection(token, update) && waitToken(token) == RsTokenService::COMPLETE;
}

bool p3Wiki::getCollections(const std::list<RsGxsGroupId> groupIds, std::vector<RsWikiCollection> &groups)
{
	uint32_t token;
	RsTokReqOptions opts;
	opts.mReqType = GXS_REQUEST_TYPE_GROUP_DATA;

	if (groupIds.empty()) {
		if (!requestGroupInfo(token, opts) || waitToken(token) != RsTokenService::COMPLETE ) return false;
	} else {
		if (!requestGroupInfo(token, opts, groupIds) || waitToken(token) != RsTokenService::COMPLETE ) return false;
	}
	return getCollections(token, groups) && !groups.empty();
}

bool p3Wiki::addModerator(const RsGxsGroupId& grpId, const RsGxsId& moderatorId)
{
	std::vector<RsWikiCollection> collections;
	if (!getCollections({grpId}, collections) || collections.empty())
		return false;

	RsWikiCollection& collection = collections.front();
	collection.mModeratorList.push_back(moderatorId);
	collection.mModeratorList.sort();
	collection.mModeratorList.unique();
	collection.mModeratorTerminationDates.erase(moderatorId);

	uint32_t token;
	return updateCollection(token, collection) && waitToken(token) == RsTokenService::COMPLETE;
}

bool p3Wiki::removeModerator(const RsGxsGroupId& grpId, const RsGxsId& moderatorId)
{
	std::vector<RsWikiCollection> collections;
	if (!getCollections({grpId}, collections) || collections.empty())
		return false;

	RsWikiCollection& collection = collections.front();
	collection.mModeratorList.remove(moderatorId);
	collection.mModeratorTerminationDates[moderatorId] = time(nullptr);

	uint32_t token;
	return updateCollection(token, collection) && waitToken(token) == RsTokenService::COMPLETE;
}

bool p3Wiki::getModerators(const RsGxsGroupId& grpId, std::list<RsGxsId>& moderators)
{
	std::vector<RsWikiCollection> collections;
	if (!getCollections({grpId}, collections) || collections.empty())
		return false;

	moderators = collections.front().mModeratorList;
	return true;
}

bool p3Wiki::isActiveModerator(const RsGxsGroupId& grpId, const RsGxsId& authorId, rstime_t editTime)
{
	RsWikiCollection collection;
	if (!getCollectionData(grpId, collection))
		return false;

	if (std::find(collection.mModeratorList.begin(), collection.mModeratorList.end(), authorId) == collection.mModeratorList.end())
		return false;

	auto it = collection.mModeratorTerminationDates.find(authorId);
	// Reject edits made at or after the termination timestamp (termination is inclusive)
	if (it != collection.mModeratorTerminationDates.end() && editTime >= it->second)
		return false;

	return true;
}

bool p3Wiki::acceptNewMessage(const RsGxsMsgMetaData *msgMeta, uint32_t /*size*/)
{
	if (!msgMeta)
		return false;

	if (msgMeta->mOrigMsgId.isNull() || msgMeta->mOrigMsgId == msgMeta->mMsgId)
		return true;

	RsGxsId originalAuthorId;
	if (!getOriginalMessageAuthor(msgMeta->mGroupId, msgMeta->mOrigMsgId, originalAuthorId))
	{
		std::cerr << "p3Wiki: Rejecting edit " << msgMeta->mMsgId
		          << " in group " << msgMeta->mGroupId
		          << " without original author data." << std::endl;
		return false;
	}

	if (msgMeta->mAuthorId == originalAuthorId)
		return true;

	if (!checkModeratorPermission(msgMeta->mGroupId, msgMeta->mAuthorId, originalAuthorId, msgMeta->mPublishTs))
	{
		std::cerr << "p3Wiki: Rejecting edit from non-moderator " << msgMeta->mAuthorId
		          << " in group " << msgMeta->mGroupId
		          << " on message by " << originalAuthorId << std::endl;
		return false;
	}

	return true;
}

bool p3Wiki::checkModeratorPermission(const RsGxsGroupId& grpId, const RsGxsId& authorId, const RsGxsId& originalAuthorId, rstime_t editTime)
{
	if (authorId == originalAuthorId)
		return true;

	return isActiveModerator(grpId, authorId, editTime);
}

bool p3Wiki::getCollectionData(const RsGxsGroupId& grpId, RsWikiCollection& collection) const
{
	if (!mDataAccess || !mSerialiser)
		return false;

	RsNxsGrp* grpData = nullptr;
	if (!mDataAccess->getGroupData(grpId, grpData) || !grpData)
		return false;

	std::unique_ptr<RsNxsGrp> grpCleanup(grpData);
	RsItem* item = nullptr;
	RsTlvBinaryData& data = grpData->grp;

	if (data.bin_len != 0)
		item = mSerialiser->deserialise(data.bin_data, &data.bin_len);

	std::unique_ptr<RsItem> itemCleanup(item);
	auto collectionItem = dynamic_cast<RsGxsWikiCollectionItem*>(item);
	if (!collectionItem)
		return false;

	collection = collectionItem->collection;
	return true;
}

bool p3Wiki::getOriginalMessageAuthor(const RsGxsGroupId& grpId, const RsGxsMessageId& msgId, RsGxsId& authorId) const
{
	if (!mDataStore)
		return false;

	GxsMsgReq req;
	req[grpId].insert(msgId);

	GxsMsgMetaResult metaResult;
	if (mDataStore->retrieveGxsMsgMetaData(req, metaResult) != 1)
		return false;

	auto groupIt = metaResult.find(grpId);
	if (groupIt == metaResult.end())
		return false;

	for (const auto& metaPtr : groupIt->second)
	{
		if (metaPtr && metaPtr->mMsgId == msgId)
		{
			authorId = metaPtr->mAuthorId;
			return true;
		}
	}

	return false;
}

/* Stream operators for debugging */

std::ostream &operator<<(std::ostream &out, const RsWikiCollection &group)
{
    out << "RsWikiCollection [ Name: " << group.mMeta.mGroupName << " ]";
    return out;
}

std::ostream &operator<<(std::ostream &out, const RsWikiSnapshot &shot)
{
    out << "RsWikiSnapshot [ Title: " << shot.mMeta.mMsgName << "]";
    return out;
}

std::ostream &operator<<(std::ostream &out, const RsWikiComment &comment)
{
    out << "RsWikiComment [ Title: " << comment.mMeta.mMsgName << "]";
    return out;
}
