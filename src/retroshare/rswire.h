/*******************************************************************************
 * libretroshare/src/retroshare: rsturtle.h                                    *
 *                                                                             *
 * libretroshare: retroshare core library                                      *
 *                                                                             *
 * Copyright 2012-2020 by Robert Fernie <retroshare@lunamutt.com>              *
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
#ifndef RETROSHARE_WIRE_GUI_INTERFACE_H
#define RETROSHARE_WIRE_GUI_INTERFACE_H

#include <inttypes.h>
#include <string>
#include <list>

#include "retroshare/rstokenservice.h"
#include "retroshare/rsgxsifacehelper.h"
#include "retroshare/rsgxscommon.h"


/* The Main Interface Class - for information about your Peers */
class RsWire;
/** Global Wire service. @jsonapi{development} */
extern RsWire *rsWire;

class RsWireGroup;
typedef std::shared_ptr<RsWireGroup> RsWireGroupSPtr;
typedef std::shared_ptr<const RsWireGroup> RsWireGroupConstSPtr;

struct RsWireStatistics: RsSerializable
{
    uint32_t mNumberOfPulses = 0;
    uint32_t mNumberOfRepliesAndLikes = 0;
    uint32_t mNumberOfUnreadPulses = 0;
    uint32_t mNumberOfNewPulses = 0;

    /// @see RsSerializable
    virtual void serial_process(
            RsGenericSerializer::SerializeJob j,
            RsGenericSerializer::SerializeContext& ctx ) override
    {
        RS_SERIAL_PROCESS(mNumberOfPulses);
        RS_SERIAL_PROCESS(mNumberOfRepliesAndLikes);
        RS_SERIAL_PROCESS(mNumberOfUnreadPulses);
        RS_SERIAL_PROCESS(mNumberOfNewPulses);
    }
};

class RsWireGroup: public RsSerializable, public RsGxsGenericGroupData
{
	public:
	RsWireGroup();

	std::string mTagline;
	std::string mLocation;
    std::string mDescription;

	// Images max size should be enforced.
	RsGxsImage  mHeadshot; // max size?
	RsGxsImage  mMasthead; // max size?

	// Unserialised stuff ---------------------

	// These are this groups top-level msgs.
	uint32_t mGroupPulses;
	uint32_t mGroupRepublishes;
	uint32_t mGroupLikes;
	uint32_t mGroupReplies;

	uint32_t mGroupFollowing;
	uint32_t mGroupFollowers;

	// These are this groups REF / RESPONSE msgs from others.
	uint32_t mRefMentions; // TODO how to handle this?
	uint32_t mRefRepublishes;
	uint32_t mRefLikes;
	uint32_t mRefReplies;

    void serial_process(RsGenericSerializer::SerializeJob j,
            RsGenericSerializer::SerializeContext& ctx) override
    {
        RS_SERIAL_PROCESS(mMeta);
        RS_SERIAL_PROCESS(mTagline);
        RS_SERIAL_PROCESS(mLocation);
        RS_SERIAL_PROCESS(mDescription);
        RS_SERIAL_PROCESS(mHeadshot);
        RS_SERIAL_PROCESS(mMasthead);
        RS_SERIAL_PROCESS(mGroupPulses);
        RS_SERIAL_PROCESS(mGroupRepublishes);
        RS_SERIAL_PROCESS(mGroupLikes);
        RS_SERIAL_PROCESS(mGroupReplies);
        RS_SERIAL_PROCESS(mGroupFollowing);
        RS_SERIAL_PROCESS(mGroupFollowers);
        RS_SERIAL_PROCESS(mRefMentions);
        RS_SERIAL_PROCESS(mRefRepublishes);
        RS_SERIAL_PROCESS(mRefLikes);
        RS_SERIAL_PROCESS(mRefReplies);
    }
};


/***********************************************************************
 * RsWire - is intended to be a Twitter clone - but fully decentralised.
 *
 * From Twitter: 
 *  twitter can be: embedded, replied to, favourited, unfavourited, 
 *    retweeted, unretweeted and deleted
 *
 * See: https://dev.twitter.com/docs/platform-objects
 *
 * Format of message: .... 
 *
 *  #HashTags.
 *  @68769381495134  => ID of Sender. 
 *  <http>
 *
 ***********************************************************************/



/************************************************************************
 * Pulse comes in three flavours.
 *
 *
 * Original Msg Pulse
 * - Spontaneous msg, on your own group.
 * - mPulseType = WIRE_PULSE_TYPE_ORIGINAL_MSG
 * - Ref fields are empty.
 *
 * Reply to a Pulse (i.e Retweet), has two parts.
 * as we want the retweet to reference the original, and the original to know about reply.
 * This info will be duplicated in two msgs - but allow data to spread easier.
 *
 * Reply Msg Pulse, will be Top-Level Msg on Publisher's Group.
 * - mPulseMode = WIRE_PULSE_TYPE_RESPONSE | WIRE_PULSE_TYPE_REPLY
 * - Ref fields refer to Parent (InReplyTo) Msg.
 *
 * Reply Reference, is Child Msg of Parent Msg, on Parent Publisher's Group.
 * - mPulseMode = WIRE_PULSE_TYPE_REFERENCE | WIRE_PULSE_TYPE_REPLY
 * - Ref fields refer to Reply Msg.
 * - NB: This Msg requires Parent Msg for complete info, while other two are self-contained.
 *
 * Additionally need to sort out additional relationships.
 * - Mentions.
 * - Followers.
 * - Following.
 ***********************************************************************/

#define  WIRE_PULSE_TYPE_ORIGINAL            (0x0001)
#define  WIRE_PULSE_TYPE_RESPONSE            (0x0002)
#define  WIRE_PULSE_TYPE_REFERENCE           (0x0004)

#define  WIRE_PULSE_RESPONSE_MASK            (0x0f00)
#define  WIRE_PULSE_TYPE_REPLY               (0x0100)
#define  WIRE_PULSE_TYPE_REPUBLISH           (0x0200)
#define  WIRE_PULSE_TYPE_LIKE                (0x0400)

#define  WIRE_PULSE_SENTIMENT_NO_SENTIMENT   (0x0000)
#define  WIRE_PULSE_SENTIMENT_POSITIVE       (0x0001)
#define  WIRE_PULSE_SENTIMENT_NEUTRAL        (0x0002)
#define  WIRE_PULSE_SENTIMENT_NEGATIVE       (0x0003)

class RsWirePulse;

typedef std::shared_ptr<RsWirePulse> RsWirePulseSPtr;
typedef std::shared_ptr<const RsWirePulse> RsWirePulseConstSPtr;

struct RsWirePulse : RsSerializable, RsGxsGenericMsgData
{
    public:

	// Store actual Pulse here.
	std::string mPulseText;

	uint32_t mPulseType = 0;
	uint32_t mSentiment = 0; // sentiment can be asserted at any point.

	// These Ref to the related (parent or reply) if reply (RESPONSE set)
	// Mode                             RESPONSE          REFERENCE
	RsGxsGroupId   mRefGroupId;    //   PARENT_GrpId      REPLY_GrpId
	std::string    mRefGroupName;  //   PARENT_GrpName    REPLY_GrpName
	RsGxsMessageId mRefOrigMsgId;  //   PARENT_OrigMsgId  REPLY_OrigMsgId
	RsGxsId        mRefAuthorId;   //   PARENT_AuthorId   REPLY_AuthorId
	rstime_t       mRefPublishTs = 0;  //   PARENT_PublishTs  REPLY_PublishTs
	std::string    mRefPulseText;  //   PARENT_PulseText  REPLY_PulseText
	uint32_t       mRefImageCount; //   PARENT_#Images    REPLY_#Images

	// Additional Fields for version 2.
	// Images, need to enforce 20k limit?
	RsGxsImage mImage1;
	RsGxsImage mImage2;
	RsGxsImage mImage3;
	RsGxsImage mImage4;

	// Below Here is not serialised.
	// They are additional fields linking pulses together or parsing elements of msg.

	// functions.
	uint32_t ImageCount();

	// can't have self referencial list, so need to use pointers.
	// using SharedPointers to automatically cleanup.

	// Pointer to WireGroups
	// mRefGroupPtr is opportunistically filled in, but will often be empty.
	RsWireGroupSPtr mRefGroupPtr; //  ORIG: N/A, RESP: Parent, REF: Reply Group
	RsWireGroupSPtr mGroupPtr;    //  ORIG: Own, RESP: Own,    REF: Parent Group

	// These are the direct children of this message
	// split into likes, replies and retweets.
	std::list<RsWirePulseSPtr> mReplies;
	std::list<RsWirePulseSPtr> mLikes;
	std::list<RsWirePulseSPtr> mRepublishes;

	// parsed from msg.
	// do we need references..?
	std::list<std::string> mHashTags;
	std::list<std::string> mMentions;
	std::list<std::string> mUrls;

    void serial_process(RsGenericSerializer::SerializeJob j,
            RsGenericSerializer::SerializeContext& ctx) override
    {
        RS_SERIAL_PROCESS(mMeta);
        RS_SERIAL_PROCESS(mPulseText);
        RS_SERIAL_PROCESS(mPulseType);
        RS_SERIAL_PROCESS(mSentiment);
        RS_SERIAL_PROCESS(mRefGroupId);
        RS_SERIAL_PROCESS(mRefGroupName);
        RS_SERIAL_PROCESS(mRefOrigMsgId);
        RS_SERIAL_PROCESS(mRefAuthorId);
        RS_SERIAL_PROCESS(mRefPublishTs);
        RS_SERIAL_PROCESS(mRefPulseText);
        RS_SERIAL_PROCESS(mRefImageCount);
        RS_SERIAL_PROCESS(mImage1);
        RS_SERIAL_PROCESS(mImage2);
        RS_SERIAL_PROCESS(mImage3);
        RS_SERIAL_PROCESS(mImage4);
        RS_SERIAL_PROCESS(mHashTags);
        RS_SERIAL_PROCESS(mMentions);
        RS_SERIAL_PROCESS(mUrls);
    }

};


std::ostream &operator<<(std::ostream &out, const RsWireGroup &group);
std::ostream &operator<<(std::ostream &out, const RsWirePulse &pulse);


/** JSON view of a pulse graph. Optional groups are zero or one element arrays.
 * The service supplies a bounded tree; no shared pointers cross the API.
 */
struct RsWirePulseData : RsSerializable
{
    RsWirePulse pulse;
    std::vector<RsWireGroup> group, refGroup;
    std::vector<RsWirePulseData> replies, likes, republishes;

    RsWirePulseData() = default;
    explicit RsWirePulseData(const RsWirePulse& source);

    void serial_process(RsGenericSerializer::SerializeJob j,
            RsGenericSerializer::SerializeContext& ctx) override
    {
        RS_SERIAL_PROCESS(pulse);
        RS_SERIAL_PROCESS(group);
        RS_SERIAL_PROCESS(refGroup);
        RS_SERIAL_PROCESS(replies);
        RS_SERIAL_PROCESS(likes);
        RS_SERIAL_PROCESS(republishes);
    }
};

class RsWire: public RsGxsIfaceHelper
{
	public:

	explicit RsWire(RsGxsIface& gxs) : RsGxsIfaceHelper(gxs) {}
	virtual ~RsWire() {}

	/*!
	 * To acquire a handle to token service handler
	 * needed to make requests to the service
	 * @return handle to token service for this gxs service
	 */
	virtual RsTokenService* getTokenService() = 0;

	/* Specific Service Data */
virtual bool getGroupData(const uint32_t &token, std::vector<RsWireGroup> &groups) = 0;
virtual bool getPulseData(const uint32_t &token, std::vector<RsWirePulse> &pulses) = 0;

virtual bool createGroup(uint32_t &token, RsWireGroup &group) = 0;
virtual bool createPulse(uint32_t &token, RsWirePulse &pulse) = 0;
/** Update an owned Wire profile.
 * @jsonapi{development}
 * @param[in,out] wire Profile including its group ID; receives updated metadata.
 * @return true on success, false on failure.
 */
virtual bool editWire(RsWireGroup& wire) = 0;

/** Create a pulse using the same validation as the Qt GUI.
 * @jsonapi{development}
 * @param[in] grpId Publishing Wire ID.
 * @param[in] pulse Text, sentiment and up to four images.
 * @return true on success, false on failure.
 */
bool createOriginalPulse(const RsGxsGroupId& grpId, const RsWirePulse& pulse);

/** Reply, republish or like a pulse.
 * @jsonapi{development}
 * @param[in] grpId Target Wire ID.
 * @param[in] msgId Target message ID.
 * @param[in] replyWith Owned publishing Wire ID.
 * @param[in] reply_type 256 reply, 512 republish, 1024 like.
 * @param[in] pulse Text, sentiment and images.
 * @return true on success, false on failure.
 */
bool createReplyPulse(RsGxsGroupId grpId, RsGxsMessageId msgId, RsGxsGroupId replyWith, uint32_t reply_type, const RsWirePulse& pulse);

/** Get a Wire profile.
 * @jsonapi{development}
 * @param[in] groupId Wire ID.
 * @param[out] group Profile.
 * @return true on success, false on failure.
 */
bool getWireGroup(const RsGxsGroupId& groupId, RsWireGroup& group);

/** Get a pulse.
 * @jsonapi{development}
 * @param[in] groupId Wire ID.
 * @param[in] msgId Message ID.
 * @param[out] pulse Pulse and available relationships.
 * @return true on success, false on failure.
 */
bool getWirePulse(const RsGxsGroupId& groupId, const RsGxsMessageId& msgId, RsWirePulseData& pulse);

/** Get the feed for selected Wires.
 * @jsonapi{development}
 * @param[in] groupIds Wire IDs, as selected by the client.
 * @param[out] pulses Feed pulse graphs.
 * @return true on success, false on failure.
 */
bool getPulsesForGroups(const std::list<RsGxsGroupId>& groupIds, std::vector<RsWirePulseData>& pulses);

/** Get a focused thread with replies, likes and republishes.
 * @jsonapi{development}
 * @param[in] groupId Wire ID.
 * @param[in] msgId Message ID.
 * @param[in] type Qt focus type; currently ignored by the service.
 * @param[out] pulse Expanded pulse graph.
 * @return true on success, false on failure.
 */
bool getPulseFocus(const RsGxsGroupId& groupId, const RsGxsMessageId& msgId, int type, RsWirePulseData& pulse);

	// Blocking Interfaces.
/** Create a Wire profile.
 * @jsonapi{development}
 * @param[in,out] group Profile data; receives the created metadata.
 * @return true on success, false on failure.
 */
virtual bool createGroup(RsWireGroup &group) = 0;
virtual bool updateGroup(uint32_t &token, RsWireGroup& group) = 0;
/** List Wire profiles; empty IDs requests all known profiles.
 * @jsonapi{development}
 * @param[in] grpIds Requested group IDs.
 * @param[out] groups Profiles.
 * @return true on success, false on failure.
 */
virtual bool getGroups(const std::list<RsGxsGroupId> grpIds,
				std::vector<RsWireGroup> &groups) = 0;

	// New Blocking Interfaces.
	// Plan to migrate all GUI calls to these, and remove old interfaces above.
	// These are not single requests, but return data graphs for display.
virtual bool createOriginalPulse(const RsGxsGroupId &grpId, RsWirePulseSPtr pPulse) = 0;
virtual bool createReplyPulse(RsGxsGroupId grpId, RsGxsMessageId msgId,
				RsGxsGroupId replyWith, uint32_t reply_type,
				RsWirePulseSPtr pPulse) = 0;


	// Provide Individual Group Details for display.
virtual bool getWireGroup(const RsGxsGroupId &groupId, RsWireGroupSPtr &grp) = 0;
virtual bool getWirePulse(const RsGxsGroupId &groupId, const RsGxsMessageId &msgId, RsWirePulseSPtr &pPulse) = 0;

	// Provide list of pulses associated with groups.
virtual bool getPulsesForGroups(const std::list<RsGxsGroupId> &groupIds,
				std::list<RsWirePulseSPtr> &pulsePtrs) = 0;

	// Provide pulse, and associated replies / like etc.
virtual bool getPulseFocus(const RsGxsGroupId &groupId, const RsGxsMessageId &msgId,
				int type, RsWirePulseSPtr &pPulse) = 0;

    // Retrieve statistics about the given wire
/** Get pulse and unread counts.
 * @jsonapi{development}
 * @param[in] wireId Wire ID.
 * @param[out] stat Statistics.
 * @return true on success, false on failure.
 */
    virtual bool getWireStatistics(const RsGxsGroupId& wireId, RsWireStatistics& stat) = 0;
/** Get network group statistics.
 * @jsonapi{development}
 * @param[in] wireId Wire ID.
 * @param[out] stat Statistics.
 * @return true on success, false on failure.
 */
    virtual bool getWireGroupStatistics(const RsGxsGroupId& wireId,GxsGroupStatistic& stat) = 0;

    // Mark a single pulse as read/unread. Blocking, no token management needed.
/** Mark a pulse read or unread.
 * @jsonapi{development}
 * @param[in] msgId Group/message ID pair.
 * @param[in] read Desired read state.
 * @return true on success, false on failure.
 */
    virtual bool setMessageReadStatus(const RsGxsGrpMsgIdPair& msgId, bool read) = 0;

    RS_DEPRECATED_FOR(setMessageReadStatus)
    virtual void setMessageReadStatus(uint32_t& token, const RsGxsGrpMsgIdPair& msgId, bool read) = 0;

/** Get message metadata for a Wire.
 * @jsonapi{development}
 * @param[in] groupId Wire ID.
 * @param[out] summaries Message metadata.
 * @return true on success, false on failure.
 */
    virtual bool getContentSummaries( const RsGxsGroupId& groupId,
                                      std::vector<RsMsgMetaData>& summaries ) = 0;

    virtual bool subscribeToGroup(uint32_t& token, const RsGxsGroupId& groupId, bool subscribe) = 0;

    // Simple subscribe/unsubscribe API without token management (for UI use)
    // Tokens are handled internally by libretroshare
/** Follow or unfollow a Wire.
 * @jsonapi{development}
 * @param[in] groupId Wire ID.
 * @param[in] subscribe True to follow, false to unfollow.
 * @return true on success, false on failure.
 */
    virtual bool subscribe(const RsGxsGroupId& groupId, bool subscribe) = 0;

/** Number of followed Wires. @jsonapi{development} */
    virtual uint32_t getFollowingCount() = 0;
/** List followed Wires.
 * @jsonapi{development}
 * @param[out] groupIds Followed Wire IDs.
 * @return true on success, false on failure.
 */
    virtual bool getSubscribedGroups(std::list<RsGxsGroupId>& groupIds) = 0;

};


enum class RsWireEventCode: uint8_t
{
    UNKNOWN                         = 0x00, // event not recognized
    NEW_WIRE                        = 0x01, // emitted when new wire is received
    NEW_POST                        = 0x02, // this event happens when there is a new post
    FOLLOW_STATUS_CHANGED           = 0x03, // this event happens when we follow someone
    POST_UPDATED                    = 0x04, // this event happens when there is any change to a post (creation, like, republish, reply)
    NEW_REPLY                       = 0x05, // this event happens when there is a new reply to a post
    NEW_LIKE                        = 0x06, // this event happens when there is a new like to a post
    NEW_REPUBLISH                   = 0x07, // this event happens when there is a new republish of a post
    WIRE_UPDATED                    = 0x08, // this event happens when there is any change to the wire account
    READ_STATUS_CHANGED             = 0x09, // existing message has been read or set to unread
    STATISTICS_CHANGED              = 0x10  // statistics changed
};

struct RsWireEvent: RsEvent
{
    RsWireEvent()
        : RsEvent(RsEventType::WIRE),
          mWireEventCode(RsWireEventCode::UNKNOWN) {}


    RsWireEventCode mWireEventCode;
    RsGxsGroupId mWireGroupId;
    RsGxsMessageId mWireMsgId;          // Id of the message/reply/like
    RsGxsMessageId mWireThreadId;		// for likes/reply, Id of the relevant message
    RsGxsMessageId mWireParentId;		// for reply, Id of the parent comment


    ///* @see RsEvent @see RsSerializable
    void serial_process(
            RsGenericSerializer::SerializeJob j,
            RsGenericSerializer::SerializeContext& ctx ) override
    {
        RsEvent::serial_process(j, ctx);
        RS_SERIAL_PROCESS(mWireEventCode);
        RS_SERIAL_PROCESS(mWireGroupId);
        RS_SERIAL_PROCESS(mWireMsgId);
        RS_SERIAL_PROCESS(mWireThreadId);
        RS_SERIAL_PROCESS(mWireParentId);
    }

    virtual ~RsWireEvent() {}
};

#endif
