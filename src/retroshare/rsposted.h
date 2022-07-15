/*******************************************************************************
 * libretroshare/src/retroshare: rsposted.h                                    *
 *                                                                             *
 * libretroshare: retroshare core library                                      *
 *                                                                             *
 * Copyright (C) 2008-2012  Robert Fernie, Christopher Evi-Parker              *
 * Copyright (C) 2020  Gioacchino Mazzurco <gio@eigenlab.org>                  *
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

#include <inttypes.h>
#include <string>
#include <list>
#include <functional>

#include "retroshare/rstokenservice.h"
#include "retroshare/rsgxsifacehelper.h"
#include "retroshare/rsgxscommon.h"
#include "retroshare/rsgxscircles.h"
#include "serialiser/rsserializable.h"

class RsPosted;

/**
 * Pointer to global instance of RsPosted service implementation
 * @jsonapi{development}
 */
extern RsPosted* rsPosted;

struct RsPostedGroup: public RsSerializable, RsGxsGenericGroupData
{
	std::string mDescription;
	RsGxsImage mGroupImage;

	/// @see RsSerializable
	virtual void serial_process( RsGenericSerializer::SerializeJob j,
								 RsGenericSerializer::SerializeContext& ctx ) override
	{
		RS_SERIAL_PROCESS(mMeta);
		RS_SERIAL_PROCESS(mDescription);
		RS_SERIAL_PROCESS(mGroupImage);
	}
};

struct RsPostedPost: public RsSerializable, RsGxsGenericMsgData
{
	RsPostedPost(): mHaveVoted(false), mUpVotes(0), mDownVotes(0), mComments(0),
	    mHotScore(0), mTopScore(0), mNewScore(0) {}

	bool calculateScores(rstime_t ref_time);

	std::string mLink;
	std::string mNotes;

	bool     mHaveVoted;

	// Calculated.
	uint32_t mUpVotes;
	uint32_t mDownVotes;
	uint32_t mComments;


	// and Calculated Scores:???
	double  mHotScore;
	double  mTopScore;
	double  mNewScore;

	RsGxsImage mImage;

	/// @see RsSerializable
	virtual void serial_process( RsGenericSerializer::SerializeJob j,
								 RsGenericSerializer::SerializeContext& ctx ) override
	{
		RS_SERIAL_PROCESS(mImage);
		RS_SERIAL_PROCESS(mMeta);
		RS_SERIAL_PROCESS(mLink);
		RS_SERIAL_PROCESS(mNotes);
		RS_SERIAL_PROCESS(mHaveVoted);
		RS_SERIAL_PROCESS(mUpVotes);
		RS_SERIAL_PROCESS(mDownVotes);
		RS_SERIAL_PROCESS(mComments);
		RS_SERIAL_PROCESS(mHotScore);
		RS_SERIAL_PROCESS(mTopScore);
		RS_SERIAL_PROCESS(mNewScore);
	}
};


//#define RSPOSTED_MSGTYPE_POST		0x0001
//#define RSPOSTED_MSGTYPE_VOTE		0x0002
//#define RSPOSTED_MSGTYPE_COMMENT	0x0004

#define RSPOSTED_PERIOD_YEAR		1
#define RSPOSTED_PERIOD_MONTH		2
#define RSPOSTED_PERIOD_WEEK		3
#define RSPOSTED_PERIOD_DAY			4
#define RSPOSTED_PERIOD_HOUR		5

#define RSPOSTED_VIEWMODE_LATEST	1
#define RSPOSTED_VIEWMODE_TOP		2
#define RSPOSTED_VIEWMODE_HOT		3
#define RSPOSTED_VIEWMODE_COMMENTS	4


enum class RsPostedEventCode: uint8_t
{
	UNKNOWN                  = 0x00,
	NEW_POSTED_GROUP         = 0x01,
	NEW_MESSAGE              = 0x02,
	SUBSCRIBE_STATUS_CHANGED = 0x03,
	UPDATED_POSTED_GROUP     = 0x04,
	UPDATED_MESSAGE          = 0x05,
	READ_STATUS_CHANGED      = 0x06,
	STATISTICS_CHANGED       = 0x07,
	MESSAGE_VOTES_UPDATED    = 0x08,
	SYNC_PARAMETERS_UPDATED  = 0x09,
	NEW_COMMENT              = 0x0a,
	NEW_VOTE                 = 0x0b,
	BOARD_DELETED            = 0x0c,
};


struct RsGxsPostedEvent: RsEvent
{
	RsGxsPostedEvent():
	    RsEvent(RsEventType::GXS_POSTED),
	    mPostedEventCode(RsPostedEventCode::UNKNOWN) {}

	RsPostedEventCode mPostedEventCode;
	RsGxsGroupId mPostedGroupId;
	RsGxsMessageId mPostedMsgId;
	RsGxsMessageId mPostedThreadId;

	///* @see RsEvent @see RsSerializable
	void serial_process( RsGenericSerializer::SerializeJob j,RsGenericSerializer::SerializeContext& ctx) override
	{
		RsEvent::serial_process(j, ctx);
		RS_SERIAL_PROCESS(mPostedEventCode);
		RS_SERIAL_PROCESS(mPostedGroupId);
		RS_SERIAL_PROCESS(mPostedMsgId);
		RS_SERIAL_PROCESS(mPostedThreadId);
	}

	~RsGxsPostedEvent() override;
};

class RsPosted : public RsGxsIfaceHelper, public RsGxsCommentService
{
public:
	explicit RsPosted(RsGxsIface& gxs) : RsGxsIfaceHelper(gxs) {}

	/**
	 * @brief Get boards information (description, thumbnail...).
	 * Blocking API.
	 * @jsonapi{development}
	 * @param[in] boardsIds ids of the boards of which to get the informations
	 * @param[out] boardsInfo storage for the boards informations
	 * @return false if something failed, true otherwhise
	 */
	virtual bool getBoardsInfo(
	        const std::list<RsGxsGroupId>& boardsIds,
	        std::vector<RsPostedGroup>& boardsInfo ) = 0;

	/**
	 * @brief Get boards summaries list. Blocking API.
	 * @jsonapi{development}
	 * @param[out] groupInfo list where to store the boards
	 * @return false if something failed, true otherwhise
	 */
	virtual bool getBoardsSummaries(std::list<RsGroupMetaData>& groupInfo) =0;

    /**
     * @brief Subscribe to a board. Blocking API
     * @jsonapi{development}
     * @param[in] boardId Board id
     * @param[in] subscribe true to subscribe, false to unsubscribe
     * @return false on error, true otherwise
     */
    virtual bool subscribeToBoard( const RsGxsGroupId& boardId, bool subscribe ) = 0;

	/**
	 * @brief Get all board messages, comments and votes in a given board
	 * @note It's the client's responsibility to figure out which message (resp. comment)
	 * a comment (resp. vote) refers to.
	 *
	 * @jsonapi{development}
	 * @param[in] boardId id of the board of which the content is requested
	 * @param[out] posts storage for posts
	 * @param[out] comments storage for the comments
	 * @param[out] votes storage for votes
	 * @return false if something failed, true otherwhise
	 */
	virtual bool getBoardAllContent(
	        const RsGxsGroupId& boardId,
	        std::vector<RsPostedPost>& posts,
	        std::vector<RsGxsComment>& comments,
	        std::vector<RsGxsVote>& votes ) = 0;

	/**
	 * @brief Get board messages, comments and votes corresponding to the given IDs.
	 * @note Since comments are internally themselves messages, this function actually
	 * returns the data for messages, comments or votes that have the given ID.
	 * It *does not* automatically retrieve the comments or votes for a given message
	 * which Id you supplied.
	 *
	 * @jsonapi{development}
	 * @param[in] boardId id of the channel of which the content is requested
	 * @param[in] contentsIds ids of requested contents
	 * @param[out] posts storage for posts
	 * @param[out] comments storage for the comments
	 * @param[out] votes storage for the votes
	 * @return false if something failed, true otherwhise
	 */
	virtual bool getBoardContent(
	        const RsGxsGroupId& boardId,
	        const std::set<RsGxsMessageId>& contentsIds,
	        std::vector<RsPostedPost>& posts,
	        std::vector<RsGxsComment>& comments,
	        std::vector<RsGxsVote>& votes ) = 0;

	/**
	 * @brief Edit board details.
	 * @jsonapi{development}
	 * @param[in] board Board data (name, description...) with modifications
	 * @return false on error, true otherwise
	 */
	virtual bool editBoard(RsPostedGroup& board) =0;

	/**
	 * @brief Create board. Blocking API.
	 * @jsonapi{development}
	 * @param[inout] board Board data (name, description...)
	 * @return false on error, true otherwise
	 */
	virtual bool createBoard(RsPostedGroup& board) =0;

    /**
     * @brief createBoardV2				Create a board. Blocking API.
     * @jsonapi{development}
     * @param[in] board_name			Name of the board to create
     * @param[in] board_description		Description of the board
     * @param[in] board_image			Image/thumbnail
     * @param[in] authorId				Contact author (optional)
     * @param[in] circleType			Type of the circle to limit the board
     * @param[in] circleId				Id of the circle to limit the board
     * @param[out] boardId				Id of the board that was created
     * @param[out] errorMessage			Error message if anything goes wrong
     * @return 							true when the board is correctly created, false otherwise.
     */
    virtual bool createBoardV2(const std::string& board_name,
                       const std::string& board_description,
                       const RsGxsImage& board_image,
                       const RsGxsId& authorId,
                       RsGxsCircleType circleType,
                       const RsGxsCircleId& circleId,
                       RsGxsGroupId& boardId,
                       std::string& errorMessage ) =0;

    /**
     * @brief Create post. Blocking API.
     * @jsonapi{development}
     * @param[in]  post    Post data (Content, description, files,...)
     * @param[out] post_id Id of the post message
     * @return             false on error, true otherwise
     */
    virtual bool createPost(const RsPostedPost& post,RsGxsMessageId& post_id) =0;

    /**
     * @brief createPostV2. Create post. Blocking API
     * @jsonapi{development}
     * @param[in] boardId        Id of the board where to post
     * @param[in] title          title of the post
     * @param[in] link           link attached to the post. Should be a https/http link
     * @param[in] notes          text attached to the post.
     * @param[in] authorId       signing author. Should be our own ID.
     * @param[in] image          optional post image.
     * @param[out] postId        id of the post after it's been generated
     * @param[out] error_message possible error message if the method returns false
     * @return true if ok, false if an error occured (see error_message)
     */
    virtual bool createPostV2(const RsGxsGroupId& boardId,
                      const std::string& title,
                      const RsUrl& link,
                      const std::string& notes,
                      const RsGxsId& authorId,
                      const RsGxsImage& image,
                      RsGxsMessageId& postId,
                      std::string& error_message) =0;

    /** @brief Add a comment on a post or on another comment. Blocking API.
     * @jsonapi{development}
     * @param[in]  boardId   Id of the board in which the comment is to be posted
     * @param[in]  postId    Id of the post in the board where the comment is placed
     * @param[in]  comment   UTF-8 string containing the comment itself
     * @param[in]  authorId  Id of the author of the comment
     * @param[in]  parentId  Id of the parent of the comment that is either a
     *                       board post Id or the message Id of another comment.
     * @param[in]  origCommentId  If this is supposed to replace an already
     *                            existing comment, the id of the old post.
     *                            If left blank a new comment will be created.
     * @param[out] commentMessageId Optional storage for the id of the comment that was created, meaningful only on success.
     * @param[out] errorMessage Optional storage for error message, meaningful only on failure.
     * @return false on error, true otherwise
     */
   virtual bool createCommentV2(
           const RsGxsGroupId&   boardId,
           const RsGxsMessageId& postId,
           const std::string&    comment,
           const RsGxsId&        authorId,
           const RsGxsMessageId& parentId = RsGxsMessageId(),
           const RsGxsMessageId& origCommentId = RsGxsMessageId(),
           RsGxsMessageId&       commentMessageId = RS_DEFAULT_STORAGE_PARAM(RsGxsMessageId),
           std::string&          errorMessage     = RS_DEFAULT_STORAGE_PARAM(std::string)
           ) = 0;

    /**
	 * \brief Retrieve statistics about the given board
	 * @jsonapi{development}
	 * \param[in]  boardId  Id of the channel group
	 * \param[out] stat       Statistics structure
	 * \return
	 */
	virtual bool getBoardStatistics(const RsGxsGroupId& boardId,GxsGroupStatistic& stat) =0;

	/**
	 * \brief Retrieve statistics about the board service
	 * @jsonapi{development}
	 * \param[out] stat       Statistics structure
	 * \return
	 */
	virtual bool getBoardsServiceStatistics(GxsServiceStatistic& stat) =0;

	/**
     * @brief Create a vote for a comment of a post
	 * @jsonapi{development}
     * @param[in]  boardId      Id of the board where to vote
     * @param[in]  postId       Id of the board post of which a comment is voted.
     * @param[in]  commentId    Id of the comment that is voted
     * @param[in]  authorId     Id of the author. Needs to be of an owned identity.
     * @param[in]  vote         Vote value, either RsGxsVoteType::DOWN or RsGxsVoteType::UP
     * @param[out] voteId       Optional storage for the id of the created vote,
     *                          meaningful only on success.
     * @param[out] errorMessage Optional storage for error message, meaningful
     *                          only on failure.
     * @return false on error, true otherwise
     */
    virtual bool voteForComment(const RsGxsGroupId& boardId,
                                const RsGxsMessageId& postId,
                                const RsGxsMessageId& commentId,
                                const RsGxsId& authorId,
                                RsGxsVoteType vote,
                                RsGxsMessageId& voteId,
                                std::string& errorMessage ) override =0;

    /**
     * @brief Create a vote for a post
     * @jsonapi{development}
     * @param[in]  postGrpId    Id of the board where to vote
     * @param[in]  postMsgId    Id of the board post
     * @param[in]  authorId     Id of the author that have voted
     * @param[in]  vote         Vote value, either RsGxsVoteType::DOWN or RsGxsVoteType::UP
     * @param[out] voteId       Id of the created vote
     * @param[out] errorMessage Error message if applicable
     * @return     false on error, true otherwise
     */
    virtual bool voteForPost(const RsGxsGroupId& postGrpId,
                             const RsGxsMessageId& postMsgId,
                             const RsGxsId& authorId,
                             RsGxsVoteType vote,
                             RsGxsMessageId& voteId,
                             std::string& errorMessage ) =0;

    /**
     * @brief Updates the read status of a post
     * @jsonapi{development}
     * @param[in]  msgId        Pair containing the group ID and message ID to act on
     * @param[in]  read         New read status
     * @return false on error, true otherwise
     */
    virtual bool setCommentReadStatus(const RsGxsGrpMsgIdPair& msgId, bool read) override = 0;

    /**
     * @brief Updates the read status of a post
     * @jsonapi{development}
     * @param[in]  msgId        Pair containing the group ID and post ID to act on
     * @param[in]  read         New read status
     * @return false on error, true otherwise
     */
    virtual bool setPostReadStatus(const RsGxsGrpMsgIdPair& msgId, bool read) = 0;

	enum RS_DEPRECATED RankType {TopRankType, HotRankType, NewRankType };

	RS_DEPRECATED_FOR(getBoardsInfo)
	virtual bool getGroupData( const uint32_t& token,
	                           std::vector<RsPostedGroup> &groups ) = 0;

	RS_DEPRECATED_FOR(getBoardsContent)
	virtual bool getPostData(
	        const uint32_t& token, std::vector<RsPostedPost>& posts,
	        std::vector<RsGxsComment>& cmts, std::vector<RsGxsVote>& vots) = 0;

	RS_DEPRECATED_FOR(getBoardsContent)
	virtual bool getPostData(
	        const uint32_t& token, std::vector<RsPostedPost>& posts,
	        std::vector<RsGxsComment>& cmts) = 0;

	RS_DEPRECATED_FOR(getBoardsContent)
	virtual bool getPostData(
	        const uint32_t& token, std::vector<RsPostedPost>& posts) = 0;

    RS_DEPRECATED_FOR(setCommentReadStatus)
    virtual bool setCommentAsRead(uint32_t& token,const RsGxsGroupId& gid,const RsGxsMessageId& comment_msg_id) override =0;

    RS_DEPRECATED_FOR(setPostReadStatus)
    virtual void setMessageReadStatus(uint32_t& token, const RsGxsGrpMsgIdPair& msgId, bool read) = 0;

	RS_DEPRECATED_FOR(createBoard)
	virtual bool createGroup(uint32_t &token, RsPostedGroup &group) = 0;

	virtual bool createPost(uint32_t &token, RsPostedPost &post) = 0;

	RS_DEPRECATED_FOR(editBoard)
	virtual bool updateGroup(uint32_t &token, RsPostedGroup &group) = 0;

	virtual bool groupShareKeys(const RsGxsGroupId& group,const std::set<RsPeerId>& peers) = 0 ;

	virtual ~RsPosted();
};
