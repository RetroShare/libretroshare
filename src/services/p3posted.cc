/*******************************************************************************
 * libretroshare/src/services: p3posted.cc                                     *
 *                                                                             *
 * libretroshare: retroshare core library                                      *
 *                                                                             *
 * Copyright 2012-2013 Robert Fernie <retroshare@lunamutt.com>                 *
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
#include "services/p3posted.h"
#include "retroshare/rsgxscircles.h"
#include "retroshare/rspeers.h"
#include "rsitems/rsposteditems.h"

#include <math.h>
#include <typeinfo>


/****
 * #define POSTED_DEBUG 1
 ****/

/*extern*/ RsPosted* rsPosted = nullptr;

/********************************************************************************/
/******************* Startup / Tick    ******************************************/
/********************************************************************************/

p3Posted::p3Posted(
        RsGeneralDataService *gds, RsNetworkExchangeService *nes,
        RsGixs* gixs ) :
    p3PostBase( gds, nes, gixs, new RsGxsPostedSerialiser(), RS_SERVICE_GXS_TYPE_POSTED ),
    RsPosted(static_cast<RsGxsIface&>(*this)) {}

const std::string GXS_POSTED_APP_NAME = "gxsposted";
const uint16_t GXS_POSTED_APP_MAJOR_VERSION  =       1;
const uint16_t GXS_POSTED_APP_MINOR_VERSION  =       0;
const uint16_t GXS_POSTED_MIN_MAJOR_VERSION  =       1;
const uint16_t GXS_POSTED_MIN_MINOR_VERSION  =       0;

static const uint32_t GXS_POSTED_CONFIG_MAX_TIME_NOTIFY_STORAGE = 86400*30*2 ; // ignore notifications for 2 months
static const uint8_t  GXS_POSTED_CONFIG_SUBTYPE_NOTIFY_RECORD   = 0x01 ;

RsServiceInfo p3Posted::getServiceInfo()
{
        return RsServiceInfo(RS_SERVICE_GXS_TYPE_POSTED,
                GXS_POSTED_APP_NAME,
                GXS_POSTED_APP_MAJOR_VERSION,
                GXS_POSTED_APP_MINOR_VERSION,
                GXS_POSTED_MIN_MAJOR_VERSION,
                GXS_POSTED_MIN_MINOR_VERSION);
}

bool p3Posted::groupShareKeys(const RsGxsGroupId& groupId,const std::set<RsPeerId>& peers)
{
        RsGenExchange::shareGroupPublishKey(groupId,peers) ;
        return true ;
}

bool p3Posted::getGroupData(const uint32_t &token, std::vector<RsPostedGroup> &groups)
{
	std::vector<RsGxsGrpItem*> grpData;
	bool ok = RsGenExchange::getGroupData(token, grpData);
		
	if(ok)
	{
		std::vector<RsGxsGrpItem*>::iterator vit = grpData.begin();
		
		for(; vit != grpData.end(); ++vit)
		{
			RsGxsPostedGroupItem* item = dynamic_cast<RsGxsPostedGroupItem*>(*vit);
			if (item)
			{
				RsPostedGroup grp;
				item->toPostedGroup(grp, true);
				delete item;
				groups.push_back(grp);
			}
			else
			{
				std::cerr << "Not a RsGxsPostedGroupItem, deleting!" << std::endl;
				delete *vit;
			}
		}
	}
	return ok;
}

bool p3Posted::getPostData(
        const uint32_t &token, std::vector<RsPostedPost> &msgs,
        std::vector<RsGxsComment> &cmts,
        std::vector<RsGxsVote> &vots)
{
#ifdef POSTED_DEBUG
	RsDbg() << __PRETTY_FUNCTION__ << std::endl;
#endif

	GxsMsgDataMap msgData;
	rstime_t now = time(NULL);
	if(!RsGenExchange::getMsgData(token, msgData))
	{
		RsErr() << __PRETTY_FUNCTION__ << " ERROR in request" << std::endl;
		return false;
	}

	GxsMsgDataMap::iterator mit = msgData.begin();

	for(; mit != msgData.end(); ++mit)
	{
		std::vector<RsGxsMsgItem*>& msgItems = mit->second;
		std::vector<RsGxsMsgItem*>::iterator vit = msgItems.begin();

		for(; vit != msgItems.end(); ++vit)
		{
			RsGxsPostedPostItem* postItem =
			        dynamic_cast<RsGxsPostedPostItem*>(*vit);

			if(postItem)
			{
				// TODO Really needed all of these lines?
				RsPostedPost msg = postItem->mPost;
				msg.mMeta = postItem->meta;
				postItem->toPostedPost(msg, true);
				msg.calculateScores(now);

				msgs.push_back(msg);
				delete postItem;
			}
			else
			{
				RsGxsCommentItem* cmtItem =
				        dynamic_cast<RsGxsCommentItem*>(*vit);
				if(cmtItem)
				{
					RsGxsComment cmt;
					RsGxsMsgItem *mi = (*vit);
					cmt = cmtItem->mMsg;
					cmt.mMeta = mi->meta;
#ifdef GXSCOMMENT_DEBUG
					RsDbg() << __PRETTY_FUNCTION__ << " Found Comment:" << std::endl;
					cmt.print(std::cerr,"  ", "cmt");
#endif
					cmts.push_back(cmt);
					delete cmtItem;
				}
				else
				{
					RsGxsVoteItem* votItem =
					        dynamic_cast<RsGxsVoteItem*>(*vit);
					if(votItem)
					{
						RsGxsVote vot;
						RsGxsMsgItem *mi = (*vit);
						vot = votItem->mMsg;
						vot.mMeta = mi->meta;
						vots.push_back(vot);
						delete votItem;
					}
					else
					{
						RsGxsMsgItem* msg = (*vit);
						//const uint16_t RS_SERVICE_GXS_TYPE_CHANNELS    = 0x0217;
						//const uint8_t RS_PKT_SUBTYPE_GXSCHANNEL_POST_ITEM = 0x03;
						//const uint8_t RS_PKT_SUBTYPE_GXSCOMMENT_COMMENT_ITEM = 0xf1;
						//const uint8_t RS_PKT_SUBTYPE_GXSCOMMENT_VOTE_ITEM = 0xf2;
						RsErr() << __PRETTY_FUNCTION__
						        << "Not a PostedPostItem neither a "
						        << "RsGxsCommentItem neither a RsGxsVoteItem"
						        << " PacketService=" << std::hex << (int)msg->PacketService() << std::dec
						        << " PacketSubType=" << std::hex << (int)msg->PacketSubType() << std::dec
						        << " type name    =" << typeid(*msg).name()
						        << " , deleting!" << std::endl;
						delete *vit;
					}
				}
			}
		}
	}

	return true;
}

bool p3Posted::getPostData(
        const uint32_t &token, std::vector<RsPostedPost> &posts, std::vector<RsGxsComment> &cmts)
{
	std::vector<RsGxsVote> vots;
	return getPostData( token, posts, cmts, vots);
}

bool p3Posted::getPostData(
        const uint32_t &token, std::vector<RsPostedPost> &posts)
{
	std::vector<RsGxsComment> cmts;
	std::vector<RsGxsVote> vots;
	return getPostData( token, posts, cmts, vots);
}

//Not currently used
/*bool p3Posted::getRelatedPosts(const uint32_t &token, std::vector<RsPostedPost> &msgs)
{
	GxsMsgRelatedDataMap msgData;
	bool ok = RsGenExchange::getMsgRelatedData(token, msgData);
	rstime_t now = time(NULL);
			
	if(ok)
	{
		GxsMsgRelatedDataMap::iterator mit = msgData.begin();
		
		for(; mit != msgData.end(); ++mit)
		{
			std::vector<RsGxsMsgItem*>& msgItems = mit->second;
			std::vector<RsGxsMsgItem*>::iterator vit = msgItems.begin();
			
			for(; vit != msgItems.end(); ++vit)
			{
				RsGxsPostedPostItem* item = dynamic_cast<RsGxsPostedPostItem*>(*vit);
		
				if(item)
				{
					RsPostedPost msg = item->mPost;
					msg.mMeta = item->meta;
					msg.calculateScores(now);

					msgs.push_back(msg);
					delete item;
				}
				else
				{
					std::cerr << "Not a PostedPostItem, deleting!" << std::endl;
					delete *vit;
				}
			}
		}
	}
			
	return ok;
}*/


/********************************************************************************************/
/********************************************************************************************/
/********************************************************************************************/

/* Switched from having explicit Ranking calculations to calculating the set of scores
 * on each RsPostedPost item.
 *
 * TODO: move this function to be part of RsPostedPost - then the GUI 
 * can reuse is as necessary.
 *
 */

bool RsPostedPost::calculateScores(rstime_t ref_time)
{
	/* so we want to calculate all the scores for this Post. */

	PostStats stats;
	extractPostCache(mMeta.mServiceString, stats);

	mUpVotes = stats.up_votes;
	mDownVotes = stats.down_votes;
	mComments = stats.comments;
	mHaveVoted = (mMeta.mMsgStatus & GXS_SERV::GXS_MSG_STATUS_VOTE_MASK);

	rstime_t age_secs = ref_time - mMeta.mPublishTs;
#define POSTED_AGESHIFT (2.0)
#define POSTED_AGEFACTOR (3600.0)

	mTopScore = ((int) mUpVotes - (int) mDownVotes);
	if (mTopScore > 0)
	{
		// score drops with time.
		mHotScore =  mTopScore / pow(POSTED_AGESHIFT + age_secs / POSTED_AGEFACTOR, 1.5);
	}
	else
	{
		// gets more negative with time.
		mHotScore =  mTopScore * pow(POSTED_AGESHIFT + age_secs / POSTED_AGEFACTOR, 1.5);
	}
	mNewScore = -age_secs;

	return true;
}

/********************************************************************************************/
/********************************************************************************************/

bool p3Posted::createGroup(uint32_t &token, RsPostedGroup &group)
{
	std::cerr << "p3Posted::createGroup()" << std::endl;

	RsGxsPostedGroupItem* grpItem = new RsGxsPostedGroupItem();
	grpItem->fromPostedGroup(group, true);


	RsGenExchange::publishGroup(token, grpItem);
	return true;
}


bool p3Posted::updateGroup(uint32_t &token, RsPostedGroup &group)
{
	std::cerr << "p3Posted::updateGroup()" << std::endl;

	RsGxsPostedGroupItem* grpItem = new RsGxsPostedGroupItem();
	grpItem->fromPostedGroup(group, true);


	RsGenExchange::updateGroup(token, grpItem);
	return true;
}

bool p3Posted::createPost(uint32_t &token, RsPostedPost &msg)
{
	std::cerr << "p3Posted::createPost() GroupId: " << msg.mMeta.mGroupId;
	std::cerr << std::endl;

	RsGxsPostedPostItem* msgItem = new RsGxsPostedPostItem();
	//msgItem->mPost = msg;
	//msgItem->meta = msg.mMeta;
	msgItem->fromPostedPost(msg, true);
		
	
	RsGenExchange::publishMsg(token, msgItem);
	return true;
}

bool p3Posted::subscribeToBoard( const RsGxsGroupId& boardId, bool subscribe )
{
    uint32_t token;

    if( !RsGenExchange::subscribeToGroup(token, boardId,subscribe) || waitToken(token) != RsTokenService::COMPLETE )
            return false;

    return true;
}

bool p3Posted::getBoardsInfo(
        const std::list<RsGxsGroupId>& boardsIds,
        std::vector<RsPostedGroup>& groupsInfo )
{
	uint32_t token;
	RsTokReqOptions opts;
	opts.mReqType = GXS_REQUEST_TYPE_GROUP_DATA;

    if(boardsIds.empty())
    {
		if( !requestGroupInfo(token, opts) || waitToken(token) != RsTokenService::COMPLETE )
            return false;
    }
    else
    {
		if( !requestGroupInfo(token, opts, boardsIds) || waitToken(token) != RsTokenService::COMPLETE )
            return false;
    }

	return getGroupData(token, groupsInfo) && !groupsInfo.empty();
}

bool p3Posted::getBoardAllContent( const RsGxsGroupId& groupId,
                                   std::vector<RsPostedPost>& posts,
                                   std::vector<RsGxsComment>& comments,
                                   std::vector<RsGxsVote>& votes )
{
	uint32_t token;
	RsTokReqOptions opts;
	opts.mReqType = GXS_REQUEST_TYPE_MSG_DATA;

	if( !requestMsgInfo(token, opts, std::list<RsGxsGroupId>({groupId})) || waitToken(token) != RsTokenService::COMPLETE )
		return false;

	return getPostData(token, posts, comments, votes);
}

bool p3Posted::getRelatedComments( const RsGxsGroupId& gid,const std::set<RsGxsMessageId>& messageIds, std::vector<RsGxsComment> &comments )
{
    std::vector<RsGxsGrpMsgIdPair> msgIds;
    for (auto& msg:messageIds)
        msgIds.push_back(RsGxsGrpMsgIdPair(gid,msg));

    RsTokReqOptions opts;
    opts.mReqType = GXS_REQUEST_TYPE_MSG_RELATED_DATA;
    opts.mOptions = RS_TOKREQOPT_MSG_THREAD | RS_TOKREQOPT_MSG_LATEST;

    uint32_t token;
    if( !requestMsgRelatedInfo(token, opts, msgIds) || waitToken(token) != RsTokenService::COMPLETE )
        return false;

    return getRelatedComments(token,comments);
}
bool p3Posted::getBoardContent( const RsGxsGroupId& groupId,
                                const std::set<RsGxsMessageId>& contentsIds,
                                std::vector<RsPostedPost>& posts,
                                std::vector<RsGxsComment>& comments,
                                std::vector<RsGxsVote>& votes )
{
	uint32_t token;
	RsTokReqOptions opts;
	opts.mReqType = GXS_REQUEST_TYPE_MSG_DATA;

	GxsMsgReq msgIds;
	msgIds[groupId] = contentsIds;

	if( !requestMsgInfo(token, opts, msgIds) ||
	        waitToken(token) != RsTokenService::COMPLETE ) return false;

	return getPostData(token, posts, comments, votes);
}

bool p3Posted::getBoardsSummaries(std::list<RsGroupMetaData>& boards )
{
	uint32_t token;
	RsTokReqOptions opts;
	opts.mReqType = GXS_REQUEST_TYPE_GROUP_META;
	if( !requestGroupInfo(token, opts) || waitToken(token) != RsTokenService::COMPLETE ) return false;

	return getGroupSummary(token, boards);
}

bool p3Posted::getBoardsServiceStatistics(GxsServiceStatistic& stat)
{
    uint32_t token;
	if(!RsGxsIfaceHelper::requestServiceStatistic(token) || waitToken(token) != RsTokenService::COMPLETE)
        return false;

    return RsGenExchange::getServiceStatistic(token,stat);
}


bool p3Posted::getBoardStatistics(const RsGxsGroupId& boardId,GxsGroupStatistic& stat)
{
	uint32_t token;
	if(!RsGxsIfaceHelper::requestGroupStatistic(token, boardId) || waitToken(token) != RsTokenService::COMPLETE)
        return false;

    return RsGenExchange::getGroupStatistic(token,stat);
}

bool p3Posted::createBoardV2(
        const std::string& board_name,
        const std::string& board_description,
        const RsGxsImage& board_image,
        const RsGxsId& authorId,
        RsGxsCircleType circleType,
        const RsGxsCircleId& circleId,
        RsGxsGroupId& boardId,
        std::string& errorMessage )
{
    const auto fname = __PRETTY_FUNCTION__;
    const auto failure = [&](const std::string& err)
    {
        errorMessage = err;
        RsErr() << fname << " " << err << std::endl;
        return false;
    };

    if(!authorId.isNull() && !rsIdentity->isOwnId(authorId))
        return failure("authorId must be either null, or of an owned identity");

    if(        circleType != RsGxsCircleType::PUBLIC
            && circleType != RsGxsCircleType::EXTERNAL
            && circleType != RsGxsCircleType::NODES_GROUP
            && circleType != RsGxsCircleType::LOCAL
            && circleType != RsGxsCircleType::YOUR_EYES_ONLY)
        return failure("circleType has invalid value");

    switch(circleType)
    {
    case RsGxsCircleType::EXTERNAL:
        if(circleId.isNull())
            return failure("circleType is EXTERNAL but circleId is null");
        break;
    case RsGxsCircleType::NODES_GROUP:
    {
        RsGroupInfo ginfo;

        if(!rsPeers->getGroupInfo(RsNodeGroupId(circleId), ginfo))
            return failure( "circleType is NODES_GROUP but circleId does not correspond to an actual group of friends" );
        break;
    }
    default:
        if(!circleId.isNull())
            return failure( "circleType requires a null circleId, but a non null circleId (" + circleId.toStdString() + ") was supplied" );
        break;
    }

    // Create a consistent posted group meta from the information supplied
    RsPostedGroup board;

    board.mMeta.mGroupName = board_name;
    board.mMeta.mAuthorId = authorId;
    board.mMeta.mCircleType = static_cast<uint32_t>(circleType);

    board.mMeta.mSignFlags = GXS_SERV::FLAG_GROUP_SIGN_PUBLISH_NONEREQ | GXS_SERV::FLAG_AUTHOR_AUTHENTICATION_REQUIRED;
    board.mMeta.mGroupFlags = GXS_SERV::FLAG_PRIVACY_PUBLIC;

    board.mMeta.mCircleId.clear();
    board.mMeta.mInternalCircle.clear();

    switch(circleType)
    {
    case RsGxsCircleType::NODES_GROUP:
        board.mMeta.mInternalCircle = circleId; break;
    case RsGxsCircleType::EXTERNAL:
        board.mMeta.mCircleId = circleId; break;
    default: break;
    }

    board.mGroupImage = board_image;
    board.mDescription = board_description;

    RsGenericSerializer::SerializeContext ctx;
    board.serial_process(RsGenericSerializer::SIZE_ESTIMATE,ctx);

    if(ctx.mSize > 200000)
        return failure("Maximum size of 200000 bytes exceeded for board.");

    bool res = createBoard(board);

    if(res)
        boardId = board.mMeta.mGroupId;

    return res;
}

bool p3Posted::createBoard(RsPostedGroup& board)
{
	uint32_t token;
	if(!createGroup(token, board))
	{
		std::cerr << __PRETTY_FUNCTION__ << "Error! Failed creating group." << std::endl;
		return false;
	}

	if(waitToken(token,std::chrono::milliseconds(5000)) != RsTokenService::COMPLETE)
	{
		std::cerr << __PRETTY_FUNCTION__ << "Error! GXS operation failed." << std::endl;
		return false;
	}

	if(!RsGenExchange::getPublishedGroupMeta(token, board.mMeta))
	{
		std::cerr << __PRETTY_FUNCTION__ << "Error! Failure getting updated " << " group data." << std::endl;
		return false;
	}

	return true;
}

bool p3Posted::createPost(const RsPostedPost& post,RsGxsMessageId& post_id)
{
    std::cerr << "p3Posted::createPost() GroupId: " << post.mMeta.mGroupId;
    std::cerr << std::endl;

    RsGxsPostedPostItem *msgItem = new RsGxsPostedPostItem();

    uint32_t token;

        auto msg(post);
        msgItem->fromPostedPost(msg, true);
        RsGenExchange::publishMsg(token, msgItem);

    if(waitToken(token) != RsTokenService::COMPLETE)
    {
        std::cerr << __PRETTY_FUNCTION__ << "Error! GXS operation failed." << std::endl;
        return false;
    }

    if(!RsGenExchange::getPublishedMsgMeta(token, msg.mMeta))
    {
        std::cerr << __PRETTY_FUNCTION__ << "Error! Failure getting updated " << " group data." << std::endl;
        return false;
    }

    std::cerr << "New post created, with message ID " << msg.mMeta.mMsgId << std::endl;
    post_id = msg.mMeta.mMsgId;

    return true;
}


bool p3Posted::voteForPost(const RsGxsGroupId& boardId, const RsGxsMessageId& postMsgId, const RsGxsId& authorId, RsGxsVoteType vote, RsGxsMessageId& voteId, std::string& errorMessage )
{
    RsGxsVote vote_msg;

    vote_msg.mMeta.mGroupId  = boardId;
    vote_msg.mMeta.mThreadId = postMsgId;
    vote_msg.mMeta.mParentId = postMsgId;
    vote_msg.mMeta.mAuthorId = authorId;
    vote_msg.mVoteType       = (vote==RsGxsVoteType::UP)?GXS_VOTE_UP:GXS_VOTE_DOWN;

    return this->vote(vote_msg,voteId,errorMessage);
}

bool p3Posted::voteForComment(const RsGxsGroupId& boardId,
                              const RsGxsMessageId& postId,
                              const RsGxsMessageId& commentId,
                              const RsGxsId& authorId,
                              RsGxsVoteType vote,
                              RsGxsMessageId& voteId,
                              std::string& errorMessage )
{
    RsGxsVote vote_msg;

    vote_msg.mMeta.mGroupId  = boardId;
    vote_msg.mMeta.mThreadId = postId;
    vote_msg.mMeta.mParentId = commentId;
    vote_msg.mMeta.mAuthorId = authorId;
    vote_msg.mVoteType       = (vote==RsGxsVoteType::UP)?GXS_VOTE_UP:GXS_VOTE_DOWN;

    return this->vote(vote_msg,voteId,errorMessage);
}

bool p3Posted::vote(const RsGxsVote& vote,RsGxsMessageId& voteId,std::string& errorMessage)
{
    // 0 - Do some basic tests

    if(!rsIdentity->isOwnId(vote.mMeta.mAuthorId))	// This is ruled out before waitToken complains. Not sure it's needed.
    {
        std::cerr << __PRETTY_FUNCTION__ << ": vote submitted with an ID that is not yours! This cannot work." << std::endl;
        return false;
    }

    // 1 - Retrieve the parent message metadata and check if it's already voted. This should be pretty fast
    //     thanks to the msg meta cache.

    uint32_t meta_token;
    RsTokReqOptions opts;
    GxsMsgReq msgReq;
    msgReq[vote.mMeta.mGroupId] = { vote.mMeta.mParentId };

    opts.mReqType = GXS_REQUEST_TYPE_MSG_META;

    std::list<RsGxsGroupId> groupIds({vote.mMeta.mGroupId});

    if( !requestMsgInfo(meta_token, opts, msgReq) || waitToken(meta_token) != RsTokenService::COMPLETE)
    {
        std::cerr << __PRETTY_FUNCTION__ << " Error! GXS operation failed." << std::endl;
        return false;
    }

    GxsMsgMetaMap msgMetaInfo;
    if(!RsGenExchange::getMsgMeta(meta_token,msgMetaInfo) || msgMetaInfo.size() != 1 || msgMetaInfo.begin()->second.size() != 1)
    {
        errorMessage = "Failure to find parent post!" ;
        return false;
    }

    if(msgMetaInfo.begin()->second.front().mMsgStatus & GXS_SERV::GXS_MSG_STATUS_VOTE_MASK)
    {
        errorMessage = "Post has already been voted" ;
        return false;
    }

    // 2 - create the vote, and get back the vote Id from RsGenExchange

    uint32_t vote_token;

    RsGxsVoteItem* msgItem = new RsGxsVoteItem(getServiceInfo().serviceTypeUInt16());
    msgItem->mMsg = vote;
    msgItem->meta = vote.mMeta;

    publishMsg(vote_token, msgItem);

    if(waitToken(vote_token) != RsTokenService::COMPLETE)
    {
        std::cerr << __PRETTY_FUNCTION__ << " Error! GXS operation failed." << std::endl;
        return false;
    }
    RsMsgMetaData vote_meta;
    if(!RsGenExchange::getPublishedMsgMeta(vote_token, vote_meta))
    {
        errorMessage = "Failure getting generated vote data.";
        return false;
    }

    voteId = vote_meta.mMsgId;

    // 3 - update the parent message vote status

    uint32_t status_token;
    uint32_t vote_flag = (vote.mVoteType == GXS_VOTE_UP)?(GXS_SERV::GXS_MSG_STATUS_VOTE_UP):(GXS_SERV::GXS_MSG_STATUS_VOTE_DOWN);

    setMsgStatusFlags(status_token, RsGxsGrpMsgIdPair(vote.mMeta.mGroupId,vote.mMeta.mParentId), vote_flag, GXS_SERV::GXS_MSG_STATUS_VOTE_MASK);

    if(waitToken(status_token) != RsTokenService::COMPLETE)
    {
        std::cerr << __PRETTY_FUNCTION__ << " Error! GXS operation failed." << std::endl;
        return false;
    }

    return true;
}

bool p3Posted::setPostReadStatus(const RsGxsGrpMsgIdPair &msgId, bool read)
{
    return setCommentReadStatus(msgId,read);
}
bool p3Posted::setCommentReadStatus(const RsGxsGrpMsgIdPair &msgId, bool read)
{
    uint32_t token;

    setMessageReadStatus(token,msgId,read);

    if(waitToken(token) != RsTokenService::COMPLETE)
    {
        std::cerr << __PRETTY_FUNCTION__ << " Error! GXS operation failed." << std::endl;
        return false;
    }
    RsGxsGrpMsgIdPair p;
    acknowledgeMsg(token,p);
    return true;
}

bool p3Posted::createPostV2(const RsGxsGroupId& boardId,
                            const std::string& title,
                            const RsUrl& link,
                            const std::string& notes,
                            const RsGxsId& authorId,
                            const RsGxsImage& image,
                            RsGxsMessageId& postId,
                            std::string& error_message)
{
    // check boardId

    std::vector<RsPostedGroup> groupsInfo;

    if(!getBoardsInfo( { boardId }, groupsInfo))
    {
        error_message = "Board with Id " + boardId.toStdString() + " does not exist.";
        RsErr() << error_message;
        return false;
    }

    // check author

    if(!rsIdentity->isOwnId(authorId))
    {
        error_message = "Attempt to create a board post with an author that is not a own ID: " + authorId.toStdString() ;
        RsErr() << error_message;
        return false;
    }

    RsPostedPost post;
    post.mMeta.mGroupId = boardId;
    post.mLink = link.toString();
    post.mImage = image;
    post.mNotes = notes;
    post.mMeta.mAuthorId = authorId;
    post.mMeta.mMsgName = title;

    // check size

    RsGenericSerializer::SerializeContext ctx;
    post.serial_process(RsGenericSerializer::SIZE_ESTIMATE,ctx);

    if(ctx.mSize > 200000) {
        error_message = "Maximum size of 200000 bytes exceeded for board post.";
        RsErr() << error_message;
        return false;
    }

    return createPost(post,postId);
}

bool p3Posted::createCommentV2(
        const RsGxsGroupId&   boardId,
        const RsGxsMessageId& postId,
        const std::string&    comment,
        const RsGxsId&        authorId,
        const RsGxsMessageId& parentId,
        const RsGxsMessageId& origCommentId,
        RsGxsMessageId&       commentMessageId,
        std::string&          errorMessage )
{
    constexpr auto fname = __PRETTY_FUNCTION__;
    const auto failure = [&](const std::string& err)
    {
        errorMessage = err;
        RsErr() << fname << " " << err << std::endl;
        return false;
    };

    if(boardId.isNull()) return  failure("boardId cannot be null");
    if(postId.isNull()) return  failure("postId cannot be null");
    if(parentId.isNull()) return failure("parentId cannot be null");

    std::vector<RsPostedGroup> channelsInfo;
    if(!getBoardsInfo(std::list<RsGxsGroupId>({boardId}),channelsInfo))
        return failure( "Channel with Id " + boardId.toStdString() + " does not exist." );

    std::vector<RsPostedPost> posts;
    std::vector<RsGxsComment> comments;
    std::vector<RsGxsVote> votes;

    if(!getBoardContent( boardId, std::set<RsGxsMessageId>({postId}), posts, comments, votes) )
        return failure( "You cannot comment post " + postId.toStdString() + " of channel with Id " + boardId.toStdString() + ": this post does not exists locally!" );

    // check that the post thread Id is actually that of a post thread

    if(posts.size() != 1 || !posts[0].mMeta.mParentId.isNull())
        return failure( "You cannot comment post " + postId.toStdString() +
                        " of channel with Id " + boardId.toStdString() +
                        ": supplied postId is not a thread, or parentMsgId is"
                        " not a comment!");

    if(!getBoardContent(  boardId, std::set<RsGxsMessageId>({parentId}), posts, comments, votes) )// does the post parent exist?
        return failure( "You cannot comment post " + parentId.toStdString() + ": supplied parent doesn't exists locally!" );

    if(!origCommentId.isNull())
    {
        std::set<RsGxsMessageId> s({origCommentId});
        std::vector<RsGxsComment> cmts;

        if( !getBoardContent(boardId, s, posts, cmts, votes) || comments.size() != 1 )
            return failure( "You cannot edit comment " +
                            origCommentId.toStdString() +
                            " of channel with Id " + boardId.toStdString() +
                            ": this comment does not exist locally!");

        const RsGxsId& commentAuthor = comments[0].mMeta.mAuthorId;

        if(commentAuthor != authorId)
            return failure( "Editor identity and creator doesn't match "
                            + authorId.toStdString() + " != "
                            + commentAuthor.toStdString() );
    }

    if(!rsIdentity->isOwnId(authorId)) // is the author ID actually ours?
        return failure( "You cannot comment to channel with Id " +
                        boardId.toStdString() + " with identity " +
                        authorId.toStdString() + " because it is not yours." );

    // Now create the comment
    RsGxsComment cmt;
    cmt.mMeta.mGroupId  = boardId;
    cmt.mMeta.mThreadId = postId;
    cmt.mMeta.mParentId = parentId;
    cmt.mMeta.mAuthorId = authorId;
    cmt.mMeta.mOrigMsgId = origCommentId;
    cmt.mComment = comment;

    uint32_t token;
    if(!createNewComment(token, cmt))
        return failure("createNewComment failed");

    RsTokenService::GxsRequestStatus wSt = waitToken(token);
    if(wSt != RsTokenService::COMPLETE)
        return failure( "GXS operation waitToken failed with: " + std::to_string(wSt) );

    if(!RsGenExchange::getPublishedMsgMeta(token, cmt.mMeta))
        return failure("Failure getting created comment data.");

    commentMessageId = cmt.mMeta.mMsgId;
    return true;
}

bool p3Posted::editBoard(RsPostedGroup& board)
{
	uint32_t token;
	if(!updateGroup(token, board))
	{
		std::cerr << __PRETTY_FUNCTION__ << " Error! Failed updating group." << std::endl;
		return false;
	}

	if(waitToken(token) != RsTokenService::COMPLETE)
	{
		std::cerr << __PRETTY_FUNCTION__ << " Error! GXS operation failed." << std::endl;
		return false;
	}

	if(!RsGenExchange::getPublishedGroupMeta(token, board.mMeta))
	{
		std::cerr << __PRETTY_FUNCTION__ << " Error! Failure getting updated " << " group data." << std::endl;
		return false;
	}

	return true;
}

RsPosted::~RsPosted() = default;
RsGxsPostedEvent::~RsGxsPostedEvent() = default;
