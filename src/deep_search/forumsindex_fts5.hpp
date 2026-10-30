/*******************************************************************************
 * RetroShare full text indexing and search implementation based on SQLite FTS5*
 *                                                                             *
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

#include <system_error>
#include <vector>
#include <memory>

#include "util/rstime.h"
#include "util/retrodb.h"
#include "retroshare/rsgxsforums.h"
#include "retroshare/rsevents.h"
#include "deep_search/commonutils.hpp"

struct DeepForumsSearchResult
{
	std::string mUrl;
	double mWeight;
	std::string mSnippet;
};

struct DeepForumsIndexFTS5
{
	explicit DeepForumsIndexFTS5(const std::string& dbPath, const std::string& dbKey);
	virtual ~DeepForumsIndexFTS5();

	/**
	 * @brief Search indexed GXS groups and messages using FTS5
	 * @param[in] queryStr Search query string
	 * @param[out] results Vector of search results
	 * @param[in] maxResults Maximum number of results to return (0 = no limit)
	 * @return Error condition if search fails
	 */
	std::error_condition search(
		const std::string& queryStr,
		std::vector<DeepForumsSearchResult>& results,
		uint32_t maxResults = 100
	);

	/**
	 * @brief Index a forum group (metadata only)
	 * @param[in] forum Forum group to index
	 * @return Error condition if indexing fails
	 */
	std::error_condition indexForumGroup(const RsGxsForumGroup& forum);

	/**
	 * @brief Remove a forum group from the index
	 * @param[in] grpId Forum group ID to remove
	 * @return Error condition if removal fails
	 */
	std::error_condition removeForumFromIndex(const RsGxsGroupId& grpId);

	/**
	 * @brief Index a forum post (title + content)
	 * @param[in] post Forum post to index
	 * @return Error condition if indexing fails
	 */
	std::error_condition indexForumPost(const RsGxsForumMsg& post);

	/**
	 * @brief Remove a forum post from the index
	 * @param[in] grpId Forum group ID
	 * @param[in] msgId Message ID to remove
	 * @return Error condition if removal fails
	 */
	std::error_condition removeForumPostFromIndex(
		RsGxsGroupId grpId,
		RsGxsMessageId msgId
	);
    
    /**
     * @brief Clear all entries from the index
     * @return Error condition if operation fails
     */
    std::error_condition clearIndex();
    
    /**
     * @brief Start an SQL transaction
     */
    void beginTransaction();

    /**
     * @brief Commit an SQL transaction
     */
    void commitTransaction();

	/**
	 * @brief Get default database path for FTS5 index
	 * @return Path to encrypted FTS5 database
	 */
	static std::string dbDefaultPath();

private:
	/**
	 * @brief Initialize FTS5 database and create tables if needed
	 * @return Error condition if initialization fails
	 */
	std::error_condition initDatabase();

	/**
	 * @brief Generate unique index ID for a forum group
	 * @param[in] grpId Forum group ID
	 * @return RetroShare URL string
	 */
	static std::string forumIndexId(const RsGxsGroupId& grpId);

	/**
	 * @brief Generate unique index ID for a forum post
	 * @param[in] grpId Forum group ID
	 * @param[in] msgId Message ID
	 * @return RetroShare URL string
	 */
	static std::string postIndexId(
		const RsGxsGroupId& grpId,
		const RsGxsMessageId& msgId
	);

	const std::string mDbPath;
    const std::string mDbKey;
	std::unique_ptr<RetroDb> mDb;
    bool mIsFTS5;
};

/*
 * FTS5 Table Schema (Complete - 11 columns):
 * 
 * CREATE VIRTUAL TABLE forum_index USING fts5(
 *     url UNINDEXED,        -- RetroShare URL (retroshare://forum?id=...)
 *     type UNINDEXED,       -- 'group' or 'post'
 *     group_id UNINDEXED,   -- Forum group ID
 *     msg_id UNINDEXED,     -- Message ID (NULL for groups)
 *     title,                -- Forum/Post title (INDEXED for search)
 *     content,              -- Forum description or post content (INDEXED)
 *     author_id UNINDEXED,  -- GxsId of the author
 *     author_name,          -- Author name (INDEXED for search by author)
 *     forum_name,           -- Forum name (INDEXED for search in forum names)
 *     publish_ts UNINDEXED, -- Publish timestamp
 *     circle_type UNINDEXED -- PUBLIC/PRIVATE/RESTRICTED (for security filtering)
 * );
 * 
 * Indexed columns (FTS5 search): title, content, author_name, forum_name
 * Unindexed columns (metadata): url, type, group_id, msg_id, author_id, publish_ts, circle_type
 * 
 * The database is encrypted using SQLCipher with the same key as GXS databases.
 */
