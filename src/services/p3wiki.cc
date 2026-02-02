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
#include <set>

RsWiki *rsWiki = NULL;

p3Wiki::p3Wiki(RsGeneralDataService* gds, RsNetworkExchangeService* nes, RsGixs *gixs)
	:RsGenExchange(gds, nes, new RsGxsWikiSerialiser(), RS_SERVICE_GXS_TYPE_WIKI, gixs, wikiAuthenPolicy()), 
	RsWiki(static_cast<RsGxsIface&>(*this)),
	mKnownWikisMutex("GXS wiki known collections timestamp cache")
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
            event->mWikiEventCode = RsWikiEventCode::UPDATED_SNAPSHOT;
            
            // Handle message changes (snapshots and comments)
            RsGxsMsgChange* msgChange = dynamic_cast<RsGxsMsgChange*>(change);
            if (msgChange) {
                // Check if this is a comment or a snapshot
                if (nullptr != dynamic_cast<RsGxsWikiCommentItem*>(msgChange->mNewMsgItem)) {
                    // This is a comment
                    if (msgChange->getType() == RsGxsNotify::TYPE_RECEIVED_NEW || 
                        msgChange->getType() == RsGxsNotify::TYPE_PUBLISHED) {
                        event->mWikiEventCode = RsWikiEventCode::NEW_COMMENT;
                    } else {
                        // Comments are typically not updated, but handle it as UPDATED_SNAPSHOT
                        event->mWikiEventCode = RsWikiEventCode::UPDATED_SNAPSHOT;
                    }
                } else {
                    // This is a snapshot (page)
                    if (msgChange->getType() == RsGxsNotify::TYPE_RECEIVED_NEW || 
                        msgChange->getType() == RsGxsNotify::TYPE_PUBLISHED) {
                        event->mWikiEventCode = RsWikiEventCode::NEW_SNAPSHOT;
                    } else {
                        event->mWikiEventCode = RsWikiEventCode::UPDATED_SNAPSHOT;
                    }
                }
            }

            // Handle message delete changes (no message data available)
            RsGxsMsgDeletedChange* msgDeletedChange = dynamic_cast<RsGxsMsgDeletedChange*>(change);
            if (msgDeletedChange) {
                event->mWikiEventCode = RsWikiEventCode::UPDATED_SNAPSHOT;
            }
            
            // Handle group changes (collections)
            RsGxsGroupChange* grpChange = dynamic_cast<RsGxsGroupChange*>(change);
            if (grpChange) {
                switch (grpChange->getType()) {
                    case RsGxsNotify::TYPE_PROCESSED:
                        // User subscribed/unsubscribed to wiki
                        event->mWikiEventCode = RsWikiEventCode::SUBSCRIBE_STATUS_CHANGED;
                        break;
                    
                    case RsGxsNotify::TYPE_RECEIVED_NEW:
                    case RsGxsNotify::TYPE_PUBLISHED:
                    {
                        // Check if this is a new wiki or an update
                        bool isNew;
                        {
                            RS_STACK_MUTEX(mKnownWikisMutex);
                            isNew = (mKnownWikis.find(grpChange->mGroupId) == mKnownWikis.end());
                            mKnownWikis[grpChange->mGroupId] = time(NULL);
                        }
                        
                        if (isNew) {
                            event->mWikiEventCode = RsWikiEventCode::NEW_COLLECTION;
                        } else {
                            event->mWikiEventCode = RsWikiEventCode::UPDATED_COLLECTION;
                        }
                        break;
                    }
                    
                    default:
                        // For other group events, use UPDATED_COLLECTION
                        event->mWikiEventCode = RsWikiEventCode::UPDATED_COLLECTION;
                        break;
                }
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
	// A termination date of 0 means the moderator is active without an expiry.
	collection.mModeratorTerminationDates[moderatorId] = 0;

	uint32_t token;
	return updateCollection(token, collection) && waitToken(token) == RsTokenService::COMPLETE;
}

bool p3Wiki::removeModerator(const RsGxsGroupId& grpId, const RsGxsId& moderatorId)
{
	std::vector<RsWikiCollection> collections;
	if (!getCollections({grpId}, collections) || collections.empty())
		return false;

	RsWikiCollection& collection = collections.front();
	auto it = collection.mModeratorTerminationDates.find(moderatorId);
	if (it == collection.mModeratorTerminationDates.end())
		return false;

	it->second = time(nullptr);

	uint32_t token;
	return updateCollection(token, collection) && waitToken(token) == RsTokenService::COMPLETE;
}

bool p3Wiki::getModerators(const RsGxsGroupId& grpId, std::list<RsGxsId>& moderators)
{
	std::vector<RsWikiCollection> collections;
	if (!getCollections({grpId}, collections) || collections.empty())
		return false;

	moderators.clear();
	for (const auto& entry : collections.front().mModeratorTerminationDates)
	{
		// A termination date of 0 means the moderator is active without an expiry.
		if (entry.second == 0)
			moderators.push_back(entry.first);
	}
	return true;
}

bool p3Wiki::isActiveModerator(const RsGxsGroupId& grpId, const RsGxsId& authorId, rstime_t editTime)
{
	RsWikiCollection collection;
	if (!getCollectionData(grpId, collection))
		return false;

	auto it = collection.mModeratorTerminationDates.find(authorId);
	if (it == collection.mModeratorTerminationDates.end())
		return false;

	// A termination date of 0 means the moderator is active without an expiry.
	if (it->second == 0)
		return true;

	// Reject edits made at or after the termination timestamp (termination is inclusive)
	if (editTime >= it->second)
		return false;

	return true;
}

bool p3Wiki::getSnapshotContent(const RsGxsGroupId& grpId,
                                const RsGxsMessageId& snapshotId,
                                std::string& content)
{
	if (grpId.isNull() || snapshotId.isNull())
		return false;

	RsTokReqOptions metaOpts;
	metaOpts.mReqType = GXS_REQUEST_TYPE_MSG_META;
	GxsMsgReq msgReq;
	msgReq[grpId].insert(snapshotId);

	uint32_t metaToken;
	if (!requestMsgInfo(metaToken, metaOpts, msgReq) || waitToken(metaToken) != RsTokenService::COMPLETE)
	{
		std::cerr << "p3Wiki::getSnapshotContent() meta request failed" << std::endl;
		return false;
	}

	GxsMsgMetaMap metaMap;
	if (!RsGenExchange::getMsgMeta(metaToken, metaMap) || metaMap[grpId].empty())
	{
		std::cerr << "p3Wiki::getSnapshotContent() missing meta for snapshot: " << snapshotId << std::endl;
		return false;
	}

	const RsMsgMetaData& meta = metaMap[grpId].front();
	const RsGxsMessageId rootId = (!meta.mOrigMsgId.isNull()) ? meta.mOrigMsgId : snapshotId;

	RsTokReqOptions relatedOpts;
	relatedOpts.mReqType = GXS_REQUEST_TYPE_MSG_RELATED_DATA;
	relatedOpts.mOptions = RS_TOKREQOPT_MSG_VERSIONS;

	std::vector<RsGxsGrpMsgIdPair> relatedIds;
	relatedIds.emplace_back(grpId, rootId);

	uint32_t relatedToken;
	if (requestMsgRelatedInfo(relatedToken, relatedOpts, relatedIds)
	        && waitToken(relatedToken) == RsTokenService::COMPLETE)
	{
		std::vector<RsWikiSnapshot> snapshots;
		if (getRelatedSnapshots(relatedToken, snapshots) && !snapshots.empty())
		{
			auto latestIt = std::max_element(
				snapshots.begin(), snapshots.end(),
				[](const RsWikiSnapshot& left, const RsWikiSnapshot& right)
				{
					return left.mMeta.mPublishTs < right.mMeta.mPublishTs;
				});

			content = latestIt->mPage;
			return true;
		}
	}

	RsTokReqOptions dataOpts;
	dataOpts.mReqType = GXS_REQUEST_TYPE_MSG_DATA;

	uint32_t dataToken;
	if (!requestMsgInfo(dataToken, dataOpts, msgReq) || waitToken(dataToken) != RsTokenService::COMPLETE)
	{
		std::cerr << "p3Wiki::getSnapshotContent() data request failed" << std::endl;
		return false;
	}

	std::vector<RsWikiSnapshot> snapshots;
	if (!getSnapshots(dataToken, snapshots) || snapshots.empty())
	{
		std::cerr << "p3Wiki::getSnapshotContent() snapshot not found: " << snapshotId << std::endl;
		return false;
	}

	content = snapshots.front().mPage;
	return true;
}

bool p3Wiki::getSnapshotsContent(const RsGxsGroupId& grpId,
                                 const std::vector<RsGxsMessageId>& snapshotIds,
                                 std::map<RsGxsMessageId, std::string>& contents)
{
	// Allow empty input - just return success with empty map
	if (snapshotIds.empty())
		return true;

	// Ensure output map does not contain stale entries from previous calls
	contents.clear();

	if (grpId.isNull())
		return false;

	RsTokReqOptions opts;
	opts.mReqType = GXS_REQUEST_TYPE_MSG_DATA;

	GxsMsgReq msgReq;
	std::set<RsGxsMessageId>& requested = msgReq[grpId];
	requested.insert(snapshotIds.begin(), snapshotIds.end());

	uint32_t token;
	if (!requestMsgInfo(token, opts, msgReq))
	{
		std::cerr << "p3Wiki::getSnapshotsContent() requestMsgInfo failed" << std::endl;
		return false;
	}

	if (waitToken(token) != RsTokenService::COMPLETE)
	{
		std::cerr << "p3Wiki::getSnapshotsContent() request failed" << std::endl;
		return false;
	}

	std::vector<RsWikiSnapshot> snapshots;
	if (!getSnapshots(token, snapshots))
	{
		std::cerr << "p3Wiki::getSnapshotsContent() failed to get snapshots" << std::endl;
		return false;
	}

	for (const auto& snapshot : snapshots)
	{
		contents[snapshot.mMeta.mMsgId] = snapshot.mPage;
	}

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
	return isActiveModerator(grpId, authorId, editTime);
}

bool p3Wiki::getCollectionData(const RsGxsGroupId& grpId, RsWikiCollection& collection)
{
	std::vector<RsWikiCollection> collections;
	if (!getCollections({grpId}, collections) || collections.empty())
		return false;

	collection = collections.front();
	return true;
}

bool p3Wiki::getOriginalMessageAuthor(const RsGxsGroupId& grpId, const RsGxsMessageId& msgId, RsGxsId& authorId)
{
	GxsMsgReq req;
	req[grpId].insert(msgId);

	RsTokReqOptions opts;
	opts.mReqType = GXS_REQUEST_TYPE_MSG_META;

	uint32_t token;
	if (!requestMsgInfo(token, opts, req) || waitToken(token) != RsTokenService::COMPLETE)
		return false;

	GxsMsgMetaMap metaMap;
	if (!RsGenExchange::getMsgMeta(token, metaMap))
		return false;

	auto groupIt = metaMap.find(grpId);
	if (groupIt == metaMap.end())
		return false;

	for (const auto& meta : groupIt->second)
	{
		if (meta.mMsgId == msgId)
		{
			authorId = meta.mAuthorId;
			return true;
		}
	}

	return false;
}

bool p3Wiki::getWikiStatistics(GxsServiceStatistic& stats)
{
	// Use the protected blocking helper from RsGxsIfaceHelper
	return getServiceStatistics(stats);
}

void p3Wiki::setMessageReadStatus(uint32_t& token, const RsGxsGrpMsgIdPair& msgId, bool read)
{
	const uint32_t mask = GXS_SERV::GXS_MSG_STATUS_GUI_NEW | GXS_SERV::GXS_MSG_STATUS_GUI_UNREAD;
	const uint32_t status = read ? 0 : GXS_SERV::GXS_MSG_STATUS_GUI_UNREAD;

	setMsgStatusFlags(token, msgId, status, mask);

	if (rsEvents)
	{
		RsEventType wikiEventType = rsEvents->getDynamicEventType("GXS_WIKI");
		auto event = std::make_shared<RsGxsWikiEvent>(wikiEventType);
		event->mWikiEventCode = RsWikiEventCode::READ_STATUS_CHANGED;
		event->mWikiGroupId = msgId.first;
		event->mWikiMsgId = msgId.second;
		rsEvents->postEvent(event);
	}
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
