/*******************************************************************************
 * libretroshare/src/retroshare: rsturtle.h                                    *
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
#ifndef RETROSHARE_WIKI_GUI_INTERFACE_H
#define RETROSHARE_WIKI_GUI_INTERFACE_H

#include <inttypes.h>
#include <string>
#include <list>
#include <vector>
#include <map>
#include <iostream>

#include "retroshare/rstokenservice.h"
#include "retroshare/rsgxsifacehelper.h"
#include "retroshare/rsevents.h"

/* The Main Interface Class - for information about your Peers */
class RsWiki;
extern RsWiki *rsWiki;


/* so the basic idea of Wiki is a set of Collections about subjects.
 *
 * Collection: RS
 *   - page: DHT
 *       - edit
 *           - edit
 *     - official revision. (new version of thread head).
 *
 * A collection will be moderated by it creator - important to prevent stupid changes.
 * We need a way to swap out / replace / fork collections if moderator is rubbish.
 *
 * This should probably be done that the collection level.
 * and enable all the references to be modified.
 *
 * Collection1 (RS DHT)
 *  : Turtle Link: Collection 0x54e4dafc34
 *    - Page 1
 *    - Page 2
 *       - Link to Self:Page 1
 *       - Link to Turtle:Page 1
 *
 *    
 */

#define FLAG_MSG_TYPE_WIKI_SNAPSHOT	0x0001
#define FLAG_MSG_TYPE_WIKI_COMMENT	0x0002

/** Wiki Event Codes */
enum class RsWikiEventCode : uint8_t
{
	UPDATED_SNAPSHOT   = 0x01,
	UPDATED_COLLECTION = 0x02
};

/** Specific Wiki Event for UI updates */
struct RsGxsWikiEvent : public RsEvent
{
	/* Constructor accepts dynamic event type */
	RsGxsWikiEvent(RsEventType type) : RsEvent(type) {}
	virtual ~RsGxsWikiEvent() override = default;

	RsWikiEventCode mWikiEventCode;
	RsGxsGroupId mWikiGroupId;

	void serial_process(RsGenericSerializer::SerializeJob j, RsGenericSerializer::SerializeContext& ctx) override
	{
		RsEvent::serial_process(j, ctx);
		RS_SERIAL_PROCESS(mWikiEventCode);
		RS_SERIAL_PROCESS(mWikiGroupId);
	}
};

struct RsWikiCollection: RsGxsGenericGroupData
{
	std::string mDescription;
	std::string mCategory;
	std::string mHashTags;
	// List of current/active moderator IDs for this collection.
	std::list<RsGxsId> mModeratorList;
	// Map of moderator IDs to their termination timestamps (for removed moderators).
	std::map<RsGxsId, rstime_t> mModeratorTerminationDates;
};

class RsWikiSnapshot
{
public:
	RsMsgMetaData mMeta;
	std::string mPage; 
	std::string mHashTags;
};

class RsWikiComment
{
public:
	RsMsgMetaData mMeta;
	std::string mComment; 
};

std::ostream &operator<<(std::ostream &out, const RsWikiCollection &group);
std::ostream &operator<<(std::ostream &out, const RsWikiSnapshot &shot);
std::ostream &operator<<(std::ostream &out, const RsWikiComment &comment);

class RsWiki: public RsGxsIfaceHelper
{
public:
	RsWiki(RsGxsIface& gxs): RsGxsIfaceHelper(gxs) {}
	virtual ~RsWiki() {}

	/* GXS Data Access */
	virtual bool getCollections(const uint32_t &token, std::vector<RsWikiCollection> &collections) = 0;
	virtual bool getSnapshots(const uint32_t &token, std::vector<RsWikiSnapshot> &snapshots) = 0;
	virtual bool getComments(const uint32_t &token, std::vector<RsWikiComment> &comments) = 0;
	virtual bool getRelatedSnapshots(const uint32_t &token, std::vector<RsWikiSnapshot> &snapshots) = 0;
	virtual bool submitCollection(uint32_t &token, RsWikiCollection &collection) = 0;
	virtual bool submitSnapshot(uint32_t &token, RsWikiSnapshot &snapshot) = 0;
	virtual bool submitComment(uint32_t &token, RsWikiComment &comment) = 0;
	virtual bool updateCollection(uint32_t &token, RsWikiCollection &collection) = 0;

	/* Blocking Interfaces */
	virtual bool createCollection(RsWikiCollection &collection) = 0;
	virtual bool updateCollection(const RsWikiCollection &collection) = 0;
	virtual bool getCollections(const std::list<RsGxsGroupId> groupIds, std::vector<RsWikiCollection> &groups) = 0;

	/* Moderator Management */
	/**
	 * @brief Add a moderator to a wiki collection
	 * @param grpId The ID of the wiki collection/group
	 * @param moderatorId The ID of the user to add as moderator
	 * @return true if the moderator was successfully added, false otherwise
	 */
	virtual bool addModerator(const RsGxsGroupId& grpId, const RsGxsId& moderatorId) = 0;
	
	/**
	 * @brief Remove a moderator from a wiki collection
	 * @param grpId The ID of the wiki collection/group
	 * @param moderatorId The ID of the moderator to remove
	 * @return true if the moderator was successfully removed, false otherwise
	 */
	virtual bool removeModerator(const RsGxsGroupId& grpId, const RsGxsId& moderatorId) = 0;
	
	/**
	 * @brief Get the list of moderators for a wiki collection
	 * @param grpId The ID of the wiki collection/group
	 * @param moderators Output parameter that will contain the list of moderator IDs
	 * @return true if the list was successfully retrieved, false otherwise
	 */
	virtual bool getModerators(const RsGxsGroupId& grpId, std::list<RsGxsId>& moderators) = 0;
	
	/**
	 * @brief Check if a user is an active moderator at a given time
	 * @param grpId The ID of the wiki collection/group
	 * @param authorId The ID of the user to check
	 * @param editTime The time at which to check moderator status
	 * @return true if the user is an active moderator at the specified time, false otherwise
	 */
	virtual bool isActiveModerator(const RsGxsGroupId& grpId, const RsGxsId& authorId, rstime_t editTime) = 0;

	/* Content fetching for merge operations (Todo 3) */
	/**
	 * @brief Get page content from a single snapshot for merging
	 * @param snapshotId The message ID of the snapshot
	 * @param content Output parameter for page content
	 * @return true if snapshot found and content retrieved
	 */
	virtual bool getSnapshotContent(const RsGxsMessageId& snapshotId, 
	                                std::string& content) = 0;

	/**
	 * @brief Get page content from multiple snapshots efficiently (bulk fetch)
	 * @param snapshotIds Vector of snapshot message IDs to fetch
	 * @param contents Output map of snapshotId -> content
	 * @return true if the operation completed successfully (contents may be empty)
	 */
	virtual bool getSnapshotsContent(const std::vector<RsGxsMessageId>& snapshotIds,
	                                 std::map<RsGxsMessageId, std::string>& contents) = 0;
};

#endif
