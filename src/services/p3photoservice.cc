/*******************************************************************************
 * libretroshare/src/services: p3photoservice.cc                               *
 *                                                                             *
 * libretroshare: retroshare core library                                      *
 *                                                                             *
 * Copyright 2008-2012 Robert Fernie,Chris Evi-Parker <retroshare@lunamutt.com>*
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
#include "p3photoservice.h"
#include "rsitems/rsphotoitems.h"
#include "rsitems/rsgxscommentitems.h"
#include "retroshare/rsgxsflags.h"
#include <retroshare/rsidentity.h>

RsPhoto *rsPhoto = NULL;

RsPhotoPhoto::RsPhotoPhoto()
	:mOrder(0)
{
	return;
}

RsPhotoAlbum::RsPhotoAlbum()
	:mShareMode(RSPHOTO_SHAREMODE_LOWRESONLY), mAutoDownload(false)
{
	return;
}

std::ostream &operator<<(std::ostream &out, const RsPhotoPhoto &photo)
{
	out << "RsPhotoPhoto [ ";
	out << "Title: " << photo.mMeta.mMsgName;
	out << "]";
	return out;
}

std::ostream &operator<<(std::ostream &out, const RsPhotoAlbum &album)
{
	out << "RsPhotoAlbum [ ";
	out << "Title: " << album.mMeta.mGroupName;
	out << "]";
	return out;
}

p3PhotoService::p3PhotoService(RsGeneralDataService* gds, RsNetworkExchangeService* nes, RsGixs* gixs)
	: RsGenExchange(gds, nes, new RsGxsPhotoSerialiser(), RS_SERVICE_GXS_TYPE_PHOTO, gixs, photoAuthenPolicy()),
	RsPhoto(static_cast<RsGxsIface&>(*this)),
	mPhotoMutex(std::string("Photo Mutex"))
{
	mCommentService = new p3GxsCommentService(this, RS_SERVICE_GXS_TYPE_PHOTO);
}

const std::string GXS_PHOTO_APP_NAME = "gxsphoto";
const uint16_t GXS_PHOTO_APP_MAJOR_VERSION  =   1;
const uint16_t GXS_PHOTO_APP_MINOR_VERSION  =   0;
const uint16_t GXS_PHOTO_MIN_MAJOR_VERSION  =   1;
const uint16_t GXS_PHOTO_MIN_MINOR_VERSION  =   0;

RsServiceInfo p3PhotoService::getServiceInfo()
{
	return RsServiceInfo(RS_SERVICE_GXS_TYPE_PHOTO,
				GXS_PHOTO_APP_NAME,
				GXS_PHOTO_APP_MAJOR_VERSION,
				GXS_PHOTO_APP_MINOR_VERSION,
				GXS_PHOTO_MIN_MAJOR_VERSION,
				GXS_PHOTO_MIN_MINOR_VERSION);
}

uint32_t p3PhotoService::photoAuthenPolicy()
{
	uint32_t policy = 0;
	uint8_t flag = 0;

	flag = GXS_SERV::MSG_AUTHEN_ROOT_PUBLISH_SIGN | GXS_SERV::MSG_AUTHEN_CHILD_AUTHOR_SIGN;
	RsGenExchange::setAuthenPolicyFlag(flag, policy, RsGenExchange::PUBLIC_GRP_BITS);

	flag |= GXS_SERV::MSG_AUTHEN_CHILD_PUBLISH_SIGN;
	RsGenExchange::setAuthenPolicyFlag(flag, policy, RsGenExchange::RESTRICTED_GRP_BITS);
	RsGenExchange::setAuthenPolicyFlag(flag, policy, RsGenExchange::PRIVATE_GRP_BITS);

	flag = GXS_SERV::GRP_OPTION_AUTHEN_AUTHOR_SIGN;
	RsGenExchange::setAuthenPolicyFlag(flag, policy, RsGenExchange::GRP_OPTION_BITS);

	return policy;
}

bool p3PhotoService::updated()
{
	RsStackMutex stack(mPhotoMutex);

	bool changed =  (!mGroupChange.empty() || !mMsgChange.empty());

	return changed;
}

void p3PhotoService::service_tick()
{
	mCommentService->comment_tick();
}


void p3PhotoService::groupsChanged(std::list<RsGxsGroupId>& grpIds)
{
	RsStackMutex stack(mPhotoMutex);

	while(!mGroupChange.empty())
	{
		RsGxsGroupChange* gc = mGroupChange.back();
        grpIds.push_back(gc->mGroupId);

		mGroupChange.pop_back();
		delete gc;
	}
}


void p3PhotoService::msgsChanged(GxsMsgIdResult& msgs)
{
	RsStackMutex stack(mPhotoMutex);

	while(!mMsgChange.empty())
	{
		RsGxsMsgChange* mc = mMsgChange.back();

        msgs[mc->mGroupId].insert(mc->mMsgId);
		mMsgChange.pop_back();
		delete mc;
	}
}


RsTokenService* p3PhotoService::getTokenService() {

	return RsGenExchange::getTokenService();
}


bool p3PhotoService::getGroupList(const uint32_t& token,
		std::list<RsGxsGroupId>& groupIds)
{
	bool okay = RsGenExchange::getGroupList(token, groupIds);
	return okay;
}


bool p3PhotoService::getMsgList(const uint32_t& token,
		GxsMsgIdResult& msgIds)
{

	return RsGenExchange::getMsgList(token, msgIds);
}


bool p3PhotoService::getGroupSummary(const uint32_t& token,
		std::list<RsGroupMetaData>& groupInfo)
{
	bool okay = RsGenExchange::getGroupMeta(token, groupInfo);
	return okay;
}


bool p3PhotoService::getMsgSummary(const uint32_t& token,
		MsgMetaResult& msgInfo)
{
	return RsGenExchange::getMsgMeta(token, msgInfo);
}


bool p3PhotoService::getAlbum(const uint32_t& token, std::vector<RsPhotoAlbum>& albums)
{
	std::vector<RsGxsGrpItem*> grpData;
	bool ok = RsGenExchange::getGroupData(token, grpData);

	if(ok)
	{
		std::vector<RsGxsGrpItem*>::iterator vit = grpData.begin();

		for(; vit != grpData.end(); ++vit)
		{
			RsGxsPhotoAlbumItem* item = dynamic_cast<RsGxsPhotoAlbumItem*>(*vit);
			if (item)
			{
				RsPhotoAlbum album = item->album;
				item->album.mMeta = item->meta;
				album.mMeta = item->album.mMeta;
				delete item;
				albums.push_back(album);
			}
			else
			{
				std::cerr << "Not a RsGxsPhotoAlbumItem, deleting!" << std::endl;
				delete *vit;
			}
		}
	}

	return ok;
}

bool p3PhotoService::getPhoto(const uint32_t& token, PhotoResult& photos)
{
	GxsMsgDataMap msgData;
	bool ok = RsGenExchange::getMsgData(token, msgData);

	if(ok)
	{
		GxsMsgDataMap::iterator mit = msgData.begin();

		for(; mit != msgData.end(); ++mit)
		{
			RsGxsGroupId grpId = mit->first;
			std::vector<RsGxsMsgItem*>& msgItems = mit->second;
			std::vector<RsGxsMsgItem*>::iterator vit = msgItems.begin();

			for(; vit != msgItems.end(); ++vit)
			{
				RsGxsPhotoPhotoItem* item = dynamic_cast<RsGxsPhotoPhotoItem*>(*vit);

				if(item)
				{
					RsPhotoPhoto photo = item->photo;
					photo.mMeta = item->meta;
					photos[grpId].push_back(photo);
					delete item;
				}else
				{
					std::cerr << "Not a photo Item, deleting!" << std::endl;
					delete *vit;
				}
			}
		}
	}

	return ok;
}

bool p3PhotoService::submitAlbumDetails(uint32_t& token, RsPhotoAlbum& album)
{
	RsGxsPhotoAlbumItem* albumItem = new RsGxsPhotoAlbumItem();
	albumItem->album = album;
	albumItem->meta = album.mMeta;
	RsGenExchange::publishGroup(token, albumItem);
	return true;
}

void p3PhotoService::notifyChanges(std::vector<RsGxsNotify*>& changes)
{

	RsStackMutex stack(mPhotoMutex);

	std::vector<RsGxsNotify*>::iterator vit = changes.begin();

	for(; vit != changes.end(); ++vit)
	{
		RsGxsNotify* n = *vit;
		RsGxsGroupChange* gc;
		RsGxsMsgChange* mc;
		if((mc = dynamic_cast<RsGxsMsgChange*>(n)) != NULL)
		{
			mMsgChange.push_back(mc);
		}
		else if((gc = dynamic_cast<RsGxsGroupChange*>(n)) != NULL)
		{
			mGroupChange.push_back(gc);
		}
		else
		{
			delete n;
		}
	}
}

bool p3PhotoService::submitPhoto(uint32_t& token, RsPhotoPhoto& photo)
{
	RsGxsPhotoPhotoItem* photoItem = new RsGxsPhotoPhotoItem();
	photoItem->photo = photo;
	photoItem->meta = photo.mMeta;

	RsGenExchange::publishMsg(token, photoItem);
	return true;
}

bool p3PhotoService::acknowledgeMsg(const uint32_t& token,
		std::pair<RsGxsGroupId, RsGxsMessageId>& msgId)
{
	return RsGenExchange::acknowledgeTokenMsg(token, msgId);
}


bool p3PhotoService::acknowledgeGrp(const uint32_t& token,
		RsGxsGroupId& grpId)
{
	return RsGenExchange::acknowledgeTokenGrp(token, grpId);
}

bool p3PhotoService::subscribeToAlbum(uint32_t &token, const RsGxsGroupId &grpId, bool subscribe)
{
	if(subscribe)
		RsGenExchange::setGroupSubscribeFlags(token, grpId, GXS_SERV::GROUP_SUBSCRIBE_SUBSCRIBED, GXS_SERV::GROUP_SUBSCRIBE_MASK);
	else
		RsGenExchange::setGroupSubscribeFlags(token, grpId, 0, GXS_SERV::GROUP_SUBSCRIBE_MASK);

	return true;
}

// Blocking versions =============================================================

bool p3PhotoService::createAlbum(RsPhotoAlbum &album)
{
	uint32_t token;
	return submitAlbumDetails(token, album) && waitToken(token) == RsTokenService::COMPLETE;
}

bool p3PhotoService::updateAlbum(const RsPhotoAlbum &/*album*/)
{
	// TODO
	return false;
}

bool p3PhotoService::getAlbums(const std::list<RsGxsGroupId> &groupIds,
			std::vector<RsPhotoAlbum> &albums)
{
	uint32_t token;
	RsTokReqOptions opts;
	opts.mReqType = GXS_REQUEST_TYPE_GROUP_DATA;

	if (groupIds.empty())
	{
		if (!requestGroupInfo(token, opts) || waitToken(token) != RsTokenService::COMPLETE )
			return false;
	}
	else
	{
		if (!requestGroupInfo(token, opts, groupIds) || waitToken(token) != RsTokenService::COMPLETE )
			return false;
	}

	return getAlbum(token, albums) && !albums.empty();
}

bool p3PhotoService::getRelatedComments(const RsGxsGroupId& gid, const std::set<RsGxsMessageId>& messageIds, std::vector<RsGxsComment> &comments)
{
	std::vector<RsGxsGrpMsgIdPair> msgIds;

	for (auto& msg : messageIds)
		msgIds.push_back(RsGxsGrpMsgIdPair(gid, msg));

	RsTokReqOptions opts;
	opts.mReqType = GXS_REQUEST_TYPE_MSG_RELATED_DATA;
	opts.mOptions = RS_TOKREQOPT_MSG_THREAD | RS_TOKREQOPT_MSG_LATEST;

	uint32_t token;
	if (!requestMsgRelatedInfo(token, opts, msgIds) || waitToken(token) != RsTokenService::COMPLETE)
		return false;

	return getRelatedComments(token, comments);
}

bool p3PhotoService::voteForComment(const RsGxsGroupId& postGroupId, const RsGxsMessageId& postMsgId,
		const RsGxsMessageId& postCommentId, const RsGxsId& authorId,
		RsGxsVoteType tVote,
		RsGxsMessageId& voteId, std::string& errorMessage)
{
	// Basic checks
	if(!rsIdentity->isOwnId(authorId))
	{
		errorMessage = "Vote submitted with an ID that is not yours!";
		return false;
	}

	// Retrieve the parent message metadata and check if it's already voted
	uint32_t meta_token;
	RsTokReqOptions opts;
	GxsMsgReq msgReq;
	msgReq[postGroupId] = { postCommentId };
	opts.mReqType = GXS_REQUEST_TYPE_MSG_META;

	if (!requestMsgInfo(meta_token, opts, msgReq) || waitToken(meta_token) != RsTokenService::COMPLETE)
	{
		errorMessage = "GXS operation failed while retrieving parent message.";
		return false;
	}

	GxsMsgMetaMap msgMetaInfo;
	if (!RsGenExchange::getMsgMeta(meta_token, msgMetaInfo) || msgMetaInfo.size() != 1 || msgMetaInfo.begin()->second.size() != 1)
	{
		errorMessage = "Failure to find parent post!";
		return false;
	}

	if (msgMetaInfo.begin()->second.front().mMsgStatus & GXS_SERV::GXS_MSG_STATUS_VOTE_MASK)
	{
		errorMessage = "Post has already been voted";
		return false;
	}

	// Create the vote
	RsGxsVote vote_msg;
	vote_msg.mMeta.mGroupId  = postGroupId;
	vote_msg.mMeta.mThreadId = postMsgId;
	vote_msg.mMeta.mParentId = postCommentId;
	vote_msg.mMeta.mAuthorId = authorId;
	vote_msg.mVoteType       = (tVote == RsGxsVoteType::UP) ? GXS_VOTE_UP : GXS_VOTE_DOWN;

	RsGxsVoteItem* msgItem = new RsGxsVoteItem(getServiceInfo().serviceTypeUInt16());
	msgItem->mMsg = vote_msg;
	msgItem->meta = vote_msg.mMeta;

	uint32_t vote_token;
	publishMsg(vote_token, msgItem);

	if (waitToken(vote_token) != RsTokenService::COMPLETE)
	{
		errorMessage = "GXS operation failed while publishing vote.";
		return false;
	}

	RsMsgMetaData vote_meta;
	if (!RsGenExchange::getPublishedMsgMeta(vote_token, vote_meta))
	{
		errorMessage = "Failure getting generated vote data.";
		return false;
	}

	voteId = vote_meta.mMsgId;

	// Update the parent message vote status
	uint32_t status_token;
	uint32_t vote_flag = (vote_msg.mVoteType == GXS_VOTE_UP) ? GXS_SERV::GXS_MSG_STATUS_VOTE_UP : GXS_SERV::GXS_MSG_STATUS_VOTE_DOWN;
	setMsgStatusFlags(status_token, RsGxsGrpMsgIdPair(postGroupId, postCommentId), vote_flag, GXS_SERV::GXS_MSG_STATUS_VOTE_MASK);

	if (waitToken(status_token) != RsTokenService::COMPLETE)
	{
		errorMessage = "GXS operation failed while updating vote status.";
		return false;
	}

	return true;
}

bool p3PhotoService::setCommentReadStatus(const RsGxsGrpMsgIdPair& msgId, bool read)
{
	uint32_t mask = GXS_SERV::GXS_MSG_STATUS_GUI_NEW | GXS_SERV::GXS_MSG_STATUS_GUI_UNREAD;
	uint32_t status = read ? 0 : GXS_SERV::GXS_MSG_STATUS_GUI_UNREAD;

	uint32_t token;
	setMsgStatusFlags(token, msgId, status, mask);

	if (waitToken(token) != RsTokenService::COMPLETE)
		return false;

	RsGxsGrpMsgIdPair p;
	acknowledgeMsg(token, p);

	return true;
}

