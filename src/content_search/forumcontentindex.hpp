/*******************************************************************************
 * RetroShare full text indexing and search implementation based on SQLite FTS *
 *                                                                             *
 * Copyright (C) 2018-2021  Gioacchino Mazzurco <gio@retroshare.cc>            *
 * Copyright (C) 2026  jolavillette                                            *
 *                                                                             *
 * This program is free software: you can redistribute it and/or modify        *
 * it under the terms of the GNU Affero General Public License version 3 as    *
 * published by the Free Software Foundation.                                  *
 *                                                                             *
 * This program is distributed in the hope that it will be useful,             *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of              *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the               *
 * GNU Affero General Public License for more details.                         *
 *                                                                             *
 * You should have received a copy of the GNU Affero General Public License    *
 * along with this program. If not, see <https://www.gnu.org/licenses/>.       *
 *                                                                             *
 *******************************************************************************/
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <system_error>
#include <vector>

#include "retroshare/rsgxsforums.h"
#include "util/rsthreads.h"
#include "util/rstime.h"

class RetroDb;

struct ForumContentSearchResult
{
	std::string mUrl;
	double mWeight;
	std::string mSnippet;
};

/**
 * Full text index of the forums and their posts.
 *
 * The index lives in its own SQLite database, encrypted with the GXS key like
 * the other GXS databases, and uses an FTS4 virtual table: FTS4 is available
 * in every SQLCipher build RetroShare links against, FTS5 is not (the Debian
 * and Ubuntu libsqlcipher packages are built without it).
 *
 * All the methods are thread safe: the index is fed from the GXS thread and
 * searched from the GUI, the JSON API and the turtle search threads.
 */
struct ForumContentIndex
{
	ForumContentIndex(const std::string& dbPath, const std::string& dbKey);
	~ForumContentIndex();

	/**
	 * @brief Search indexed forums and posts, newest first
	 * @param[in] queryStr words to look for, each one matched as a prefix,
	 *	all of them must be present
	 * @param[out] results matching documents
	 * @param[in] maxResults maximum number of results, 0 for no limit
	 * @param[in] forumId if not null only the posts of this forum are searched,
	 *	and only their content, not their title (replies inherit the thread
	 *	title, matching it would return every reply of the thread)
	 * @return error details, function_not_supported if the index is unusable
	 */
	std::error_condition search(
	        const std::string& queryStr,
	        std::vector<ForumContentSearchResult>& results,
	        uint32_t maxResults = 100,
	        const RsGxsGroupId& forumId = RsGxsGroupId() );

	std::error_condition indexForumGroup(const RsGxsForumGroup& forum);

	std::error_condition removeForumFromIndex(const RsGxsGroupId& grpId);

	std::error_condition indexForumPost(const RsGxsForumMsg& post);

	std::error_condition removeForumPostFromIndex(
	        const RsGxsGroupId& grpId, const RsGxsMessageId& msgId );

	/**
	 * @brief Start a full re-index: open the transaction that holds the whole
	 *	run, empty the index and clear its "filled" mark. If RetroShare stops
	 *	before endFullReindex(), SQLite rolls the transaction back at next open:
	 *	the previous index and its mark are back, nothing is lost.
	 */
	std::error_condition beginFullReindex();

	/**
	 * @brief Commit the full re-index started by beginFullReindex()
	 * @param[in] complete false if some forum or post could not be read: the
	 *	index is kept but left unmarked, so needsFullReindex() runs it again
	 */
	std::error_condition endFullReindex(bool complete);

	/**
	 * @return true if the index has just been created, its schema has been
	 *	upgraded or its last full re-index was incomplete, so it must be filled
	 *	from the GXS database
	 */
	bool needsFullReindex() const;

	static std::string dbDefaultPath();

	/// Build the FTS query from the user input
	static std::string buildMatchExpression(
	        const std::string& queryStr, bool contentOnly );

private:
	std::error_condition openDatabase();
	void removeOldIndexes();
	std::error_condition indexDocument(
	        const RsGxsGroupId& grpId, const RsGxsMessageId& msgId,
	        const std::string& title, const std::string& content,
	        rstime_t publishTs );
	bool exec(const char* sql);

	static int64_t docId(
	        const RsGxsGroupId& grpId, const RsGxsMessageId& msgId );
	static std::string forumIndexId(const RsGxsGroupId& grpId);
	static std::string postIndexId(
	        const RsGxsGroupId& grpId, const RsGxsMessageId& msgId );

	const std::string mDbPath;
	const std::string mDbKey;

	mutable RsMutex mIndexMtx;
	std::unique_ptr<RetroDb> mDb;
	bool mUsable;
	bool mNeedsFullReindex;
};
