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

#include "deep_search/forumsindex_fts5.hpp"
#include "deep_search/commonutils.hpp"
#include "retroshare/rsinit.h"
#include "retroshare/rsgxsforums.h"
#include "util/rsdebuglevel4.h"
#include "util/rsdebug.h"

// Constructor
DeepForumsIndexFTS5::DeepForumsIndexFTS5(const std::string& dbPath, const std::string& dbKey)
    : mDbPath(dbPath), mDbKey(dbKey), mDb(nullptr), mIsFTS5(true)
{
	RsDbg() << "DEEPSEARCH: Initializing FTS5 index at " << dbPath;
	
	// Initialize database immediately
	auto err = initDatabase();
	if(err)
	{
		RsErr() << "DEEPSEARCH: Database initialization failed: " << err.message();
	}
}

// Destructor
DeepForumsIndexFTS5::~DeepForumsIndexFTS5()
{
	RsDbg() << "DEEPSEARCH: Closing FTS5 index";
	if(mDb)
	{
		mDb->closeDb();
	}
	mDb.reset();
}

// Search implementation
std::error_condition DeepForumsIndexFTS5::search(
	const std::string& queryStr,
	std::vector<DeepForumsSearchResult>& results,
	uint32_t maxResults
)
{
    RsDbg() << "DEEPSEARCH: search('" << queryStr << "')";
	RsDbg() << "DEEPSEARCH: Search query '" << queryStr << "' maxResults=" << maxResults;
	results.clear();

	if(queryStr.empty()) return std::error_condition();

	// Initialize database if needed
	if(!mDb)
	{
		auto err = initDatabase();
		if(err) return err;
	}

	// 1. Prepare Columns
	// We want: url, type, group_id, msg_id, title, snippet(content), author_id, publish_ts, forum_name
	std::list<std::string> columns;
	columns.push_back("url");                                            // 0
	columns.push_back("type");                                           // 1
	columns.push_back("group_id");                                       // 2
	columns.push_back("msg_id");                                         // 3
	columns.push_back("title");                                          // 4
	// Column 5 is content. We use snippet function on it.
	// Syntax: snippet(table, col_index, start_match, end_match, ellipsis, max_tokens)
    if(mIsFTS5) {
        columns.push_back("snippet(forum_index, 5, '<b>', '</b>', '...', 64)"); // 5
    } else {
        // FTS4 syntax: snippet(table, start, end, ellip, col, tokens)
        // Wait, FTS4 snippet arg order: snippet(table, start, end, ellip, -1, 64) -> column index is implicit or tricky?
        // Actually, FTS3/4 snippet: snippet(table, start, end, ellip) - column is auto-selected BEST column?
        // Or snippet(table, start, end, ellip, col_index, tokens) provided by FTS4?
        // Let's check docs again. FTS3 snippet: snippet(tbl, start, end, ellip).
        // It allows 6 args. snippet(tbl, start, end, ellip, col, ntok).
        // But in FTS3/4 col=-1 means "any column" or "best snippet".
        // Let's use snippet(forum_index, '<b>', '</b>', '...', -1, 64) which is standard for "match anywhere".
        // Although we want column 5 (content).
        columns.push_back("snippet(forum_index, '<b>', '</b>', '...', 5, 64)");
    }
	columns.push_back("author_id");                                      // 6
	columns.push_back("publish_ts");                                     // 7
	columns.push_back("forum_name");                                     // 8
	
	// 2. Prepare WHERE clause (FTS5 MATCH)
	// Must escape single quotes in the query string
	auto escapeSQL = [](const std::string& input) -> std::string {
		std::string output;
		output.reserve(input.size());
		for(char c : input) {
			if(c == '\'') output += "''";
			else output += c;
		}
		return output;
	};

	// Use prefix matching by adding * to the query if it's long enough
	std::string ftsQuery = escapeSQL(queryStr);
	if (ftsQuery.size() >= 1 && ftsQuery.back() != '*') {
		ftsQuery += "*";
	}
	
	// FTS5 MATCH query: forum_index MATCH 'query*'
    // We use a simple match first as multi-column specifiers {col1 col2} : ... 
    // can sometimes fail if columns are null or have specific tokenizer issues.
	std::string where = "forum_index MATCH '" + ftsQuery + "'";
	
	// Debug: Check total rows and do a LIKE test on BOTH title and content
	std::list<std::string> testCols = {"count(*)", 
                                       "count(case when title like '%" + escapeSQL(queryStr) + "%' then 1 end)",
                                       "count(case when content like '%" + escapeSQL(queryStr) + "%' then 1 end)"};
	RetroCursor* countCursor = mDb->sqlQuery("forum_index", testCols, "", "");
	if (countCursor && countCursor->moveToFirst()) {
		int totalRows = countCursor->getInt32(0);
		int titleMatches = countCursor->getInt32(1);
		int contentMatches = countCursor->getInt32(2);
		RsDbg() << "DEEPSEARCH: Table 'forum_index' total rows: " << totalRows;
        RsDbg() << "DEEPSEARCH: LIKE test for '%" << queryStr << "%' -> Titles: " << titleMatches << " | Content: " << contentMatches;
	}
	delete countCursor;

    // Debug: Dump first 3 rows to see actual content
    RsDbg() << "DEEPSEARCH: --- Database Sample (First 3 rows) ---";
    // We use a raw query because RetroDb::sqlQuery is too rigid for complex LIMIT/selection tests
    std::string sampleSQL = "SELECT rowid, title, content, author_name FROM forum_index LIMIT 3;";
    sqlite3_stmt* sampleStmt = NULL;
    if (sqlite3_prepare_v2(mDb->getSqlHandle(), sampleSQL.c_str(), -1, &sampleStmt, NULL) == SQLITE_OK) {
        while (sqlite3_step(sampleStmt) == SQLITE_ROW) {
            const char* t = (const char*)sqlite3_column_text(sampleStmt, 1);
            const char* c = (const char*)sqlite3_column_text(sampleStmt, 2);
            const char* a = (const char*)sqlite3_column_text(sampleStmt, 3);
            RsDbg() << "DEEPSEARCH: RowID: " << sqlite3_column_int64(sampleStmt, 0) 
                      << " | Title: '" << (t?t:"NULL") << "' | Auth: '" << (a?a:"NULL") 
                      << "' | Content Sample: '" << (c ? std::string(c).substr(0, 50) : "NULL") << "...'";
        }
        sqlite3_finalize(sampleStmt);
    } else {
        RsDbg() << "DEEPSEARCH: Sample query failed: " << sqlite3_errmsg(mDb->getSqlHandle());
    }
    RsDbg() << "DEEPSEARCH: ------------------------------------";

    // Debug: Check table structure
    RsDbg() << "DEEPSEARCH: --- Table Structure ---";
    if (sqlite3_prepare_v2(mDb->getSqlHandle(), "PRAGMA table_info(forum_index);", -1, &sampleStmt, NULL) == SQLITE_OK) {
        while (sqlite3_step(sampleStmt) == SQLITE_ROW) {
            RsDbg() << "DEEPSEARCH: Col: " << sqlite3_column_text(sampleStmt, 1) 
                      << " | Type: " << sqlite3_column_text(sampleStmt, 2);
        }
        sqlite3_finalize(sampleStmt);
    }
    RsDbg() << "DEEPSEARCH: -----------------------";
	
	// 3. Prepare ORDER BY / LIMIT
	// FTS5 sorts by relevance automatically if we order by rank
    // Note: RetroDb::sqlQuery adds its own " ORDER BY " prefix
	std::string orderBy = "rank LIMIT " + std::to_string(maxResults);
	
	// 4. Execute Query
	// Construct the full SQL query string for logging
	std::string sql = "SELECT ";
	bool firstCol = true;
	for (const auto& col : columns) {
		if (!firstCol) sql += ", ";
		sql += col;
		firstCol = false;
	}
	sql += " FROM forum_index WHERE " + where + " ORDER BY " + orderBy + ";";
    RsDbg() << "DEEPSEARCH: Executing SQL: " << sql;
	RsDbg() << "DEEPSEARCH: Executing SQL: " << sql;
	RetroCursor* c = mDb->sqlQuery("forum_index", columns, where, orderBy);
	
	if(!c || !c->moveToFirst())
	{
		delete c;
        RsDbg() << "DEEPSEARCH: MATCH query returned 0 results. Trying LIKE fallback...";
        
        // Fallback to LIKE if MATCH fails (e.g. tokenizer issues)
        std::string likeWhere = "(title LIKE '%" + escapeSQL(queryStr) + "%' OR " +
                                "content LIKE '%" + escapeSQL(queryStr) + "%')";
        
        c = mDb->sqlQuery("forum_index", columns, likeWhere, "");
        if (!c || !c->moveToFirst()) {
            RsDbg() << "DEEPSEARCH: LIKE fallback also failed.";
            delete c;
            c = nullptr;
        } else {
            RsDbg() << "DEEPSEARCH: LIKE fallback succeeded.";
        }
	}

	// 5. Parse Results
    int count = 0;
    // Note: moveToFirst() was already called and succeeded if c is not null.
	bool valid = (c != nullptr); 
	while(valid)
	{
		DeepForumsSearchResult res;
		
		// 0: url -> mUrl
		c->getString(0, res.mUrl);

		// 5: snippet -> mSnippet
		c->getString(5, res.mSnippet);
		
		// mWeight (Relevance)
		res.mWeight = 1.0;
		
		results.push_back(res);
        count++;

		RsDbg() << "DEEPSEARCH: Result " << count << ": URL=" << res.mUrl << " Snippet='" << res.mSnippet << "'";

		valid = c->moveToNext();
	}
	
    RsDbg() << "DEEPSEARCH: Search returned " << count << " results for query '" << queryStr << "'";
    if (count == 0) {
        RsDbg() << "DEEPSEARCH: Last SQL Error: " << sqlite3_errmsg(mDb->getSqlHandle());
    }
    RsDbg() << "DEEPSEARCH: Search returned " << count << " results for query '" << queryStr << "'";
	delete c;
	return std::error_condition();
}

// Index forum group
std::error_condition DeepForumsIndexFTS5::indexForumGroup(const RsGxsForumGroup& forum)
{
	if(forum.mMeta.mGroupId.isNull())
	{
		RsErr() << "DEEPSEARCH: Cannot index forum group with null ID";
		return std::errc::invalid_argument;
	}

	// Initialize database if needed
	if(!mDb)
	{
		auto err = initDatabase();
		if(err) return err;
	}

	RsDbg() << "DEEPSEARCH: Indexing forum group " << forum.mMeta.mGroupId;
	
	// Prepare data
	std::string cleanDesc = DeepSearch::simpleTextHtmlExtract(forum.mDescription);
	std::string url = forumIndexId(forum.mMeta.mGroupId);
	
	// Escape SQL string helper
	auto escapeSQL = [](const std::string& input) -> std::string {
		std::string output;
		output.reserve(input.size());
		for(char c : input) {
			if(c == '\'') output += "''";
			else output += c;
		}
		return output;
	};

	// SQL Query
	// INSERT OR REPLACE INTO forum_index VALUES(...)
	// Order: url, type, group_id, msg_id, title, content, author_id, author_name, forum_name, publish_ts, circle_type
	std::string q = "INSERT OR REPLACE INTO forum_index VALUES(";
	q += "'" + escapeSQL(url) + "', ";                   // url
	q += "'group', ";                                    // type
	q += "'" + escapeSQL(forum.mMeta.mGroupId.toStdString()) + "', "; // group_id
	q += "'', ";                                         // msg_id (empty for group)
	q += "'" + escapeSQL(forum.mMeta.mGroupName) + "', "; // title
	q += "'" + escapeSQL(cleanDesc) + "', ";             // content
	q += "'" + escapeSQL(forum.mMeta.mAuthorId.toStdString()) + "', "; // author_id
	q += "'', ";                                         // author_name (TODO: fetch)
	q += "'" + escapeSQL(forum.mMeta.mGroupName) + "', ";// forum_name (same as title)
	q += std::to_string(forum.mMeta.mPublishTs) + ", ";  // publish_ts
	q += std::to_string(forum.mMeta.mCircleType);        // circle_type
	q += ");";
	
	if(!mDb->execSQL(q))
	{
		RsErr() << "DEEPSEARCH: Failed to execute INSERT for forum " << forum.mMeta.mGroupId;
		return std::errc::io_error;
	}
	
    RsDbg() << "DEEPSEARCH: Inserted forum " << forum.mMeta.mGroupId << " into FTS";
	return std::error_condition();
}

// Index forum post
std::error_condition DeepForumsIndexFTS5::indexForumPost(const RsGxsForumMsg& post)
{
	if(post.mMeta.mGroupId.isNull() || post.mMeta.mMsgId.isNull())
	{
		RsErr() << "DEEPSEARCH: Cannot index post with null ID";
		return std::errc::invalid_argument;
	}

	// Initialize database if needed
	if(!mDb)
	{
		auto err = initDatabase();
		if(err) return err;
	}

	RsDbg() << "DEEPSEARCH: Indexing post " << post.mMeta.mMsgId 
	        << " in forum " << post.mMeta.mGroupId;
	
	// Prepare data
	std::string cleanContent = DeepSearch::simpleTextHtmlExtract(post.mMsg);
	std::string url = postIndexId(post.mMeta.mGroupId, post.mMeta.mMsgId);
	
	RsDbg() << "DEEPSEARCH: Indexing post " << post.mMeta.mMsgId << " (Title: '" << post.mMeta.mMsgName 
	          << "', Content Size: " << cleanContent.size() << " bytes)";
    if (cleanContent.size() > 0) {
        RsDbg() << "DEEPSEARCH: Content sample: " << cleanContent.substr(0, 100) << "...";
    }
	
	// escapeSQL helper
	auto escapeSQL = [](const std::string& input) -> std::string {
		std::string output;
		output.reserve(input.size());
		for(char c : input) {
			if(c == '\'') output += "''";
			else output += c;
		}
		return output;
	};
	
	// SQL Query
	// INSERT OR REPLACE INTO forum_index VALUES(...)
	std::string q = "INSERT OR REPLACE INTO forum_index VALUES(";
	q += "'" + escapeSQL(url) + "', ";                   // url
	q += "'post', ";                                     // type
	q += "'" + escapeSQL(post.mMeta.mGroupId.toStdString()) + "', "; // group_id
	q += "'" + escapeSQL(post.mMeta.mMsgId.toStdString()) + "', ";   // msg_id
	q += "'" + escapeSQL(post.mMeta.mMsgName) + "', ";   // title
	q += "'" + escapeSQL(cleanContent) + "', ";          // content
	q += "'" + escapeSQL(post.mMeta.mAuthorId.toStdString()) + "', "; // author_id
	q += "'', ";                                         // author_name (TODO: fetch)
	q += "'', ";                                         // forum_name (TODO: fetch)
	q += std::to_string(post.mMeta.mPublishTs) + ", ";   // publish_ts
	q += "0";                                            // circle_type (TODO: fetch)
	q += ");";
	
	if(!mDb->execSQL(q))
	{
		RsErr() << "DEEPSEARCH: Failed to execute INSERT for post " << post.mMeta.mMsgId;
		return std::errc::io_error;
	}
	
	return std::error_condition();
}

// Remove forum from index
std::error_condition DeepForumsIndexFTS5::removeForumFromIndex(const RsGxsGroupId& grpId)
{
	if(grpId.isNull()) return std::errc::invalid_argument;

	// Initialize database if needed
	if(!mDb)
	{
		auto err = initDatabase();
		if(err) return err;
	}

	RsDbg() << "DEEPSEARCH: Removing forum " << grpId << " and all its posts from index";
	
	// Delete everything related to this group (the group entry itself + all posts)
	// escapeSQL helper
	auto escapeSQL = [](const std::string& input) -> std::string {
		std::string output;
		output.reserve(input.size());
		for(char c : input) {
			if(c == '\'') output += "''";
			else output += c;
		}
		return output;
	};

	std::string q = "DELETE FROM forum_index WHERE group_id='" + escapeSQL(grpId.toStdString()) + "';";
	
	if(!mDb->execSQL(q))
	{
		RsErr() << "DEEPSEARCH: Failed to remove forum " << grpId;
		return std::errc::io_error;
	}
	
	return std::error_condition();
}

// Remove post from index
std::error_condition DeepForumsIndexFTS5::removeForumPostFromIndex(
	RsGxsGroupId grpId,
	RsGxsMessageId msgId
)
{
	if(grpId.isNull() || msgId.isNull()) return std::errc::invalid_argument;

	// Initialize database if needed
	if(!mDb)
	{
		auto err = initDatabase();
		if(err) return err;
	}

	RsDbg() << "DEEPSEARCH: Removing post " << msgId 
	        << " from forum " << grpId;
	
	std::string url = postIndexId(grpId, msgId);
	
	// escapeSQL helper
	auto escapeSQL = [](const std::string& input) -> std::string {
		std::string output;
		output.reserve(input.size());
		for(char c : input) {
			if(c == '\'') output += "''";
			else output += c;
		}
		return output;
	};
	
	std::string q = "DELETE FROM forum_index WHERE url='" + escapeSQL(url) + "';";
	
	if(!mDb->execSQL(q))
	{
		RsErr() << "DEEPSEARCH: Failed to remove post " << msgId;
		return std::errc::io_error;
	}
	
	return std::error_condition();
}

std::error_condition DeepForumsIndexFTS5::clearIndex()
{
    if(!mDb) initDatabase();
    if(!mDb || !mDb->isOpen()) return std::errc::io_error;

    RsDbg() << "DEEPSEARCH: Clearing all forum index entries...";
    if(!mDb->execSQL("DELETE FROM forum_index;"))
    {
        RsErr() << "DEEPSEARCH: Failed to clear forum index";
        return std::errc::io_error;
    }
    
    // SQLite DELETE doesn't reclaim disk space by default.
    // VACUUM re-packs the database and actually reduces the file size.
    RsDbg() << "DEEPSEARCH: Vacuuming database to reclaim space...";
    mDb->execSQL("VACUUM;");
    
    return std::error_condition();
}

// Initialize database (to be implemented in Step 3)
std::error_condition DeepForumsIndexFTS5::initDatabase()
{
	RsDbg() << "DEEPSEARCH: Initializing FTS5 database at " << mDbPath;
	
	// Open database with RetroDb (SQLCipher enabled)
	try
	{
		mDb = std::make_unique<RetroDb>(
			mDbPath,
			RetroDb::OPEN_READWRITE_CREATE,
			mDbKey
		);
	}
	catch(const std::exception& e)
	{
		RsErr() << "DEEPSEARCH: Exception opening database: " << e.what();
		return std::errc::io_error;
	}
	
	if(!mDb || !mDb->isOpen())
	{
		RsErr() << "DEEPSEARCH: Failed to open database";
		return std::errc::io_error;
	}
	
	RsDbg() << "DEEPSEARCH: Database opened successfully";
	
	// Create FTS5 table if it doesn't exist
	// Full schema with author, forum metadata, and security fields
	const char* createTableSQL = 
		"CREATE VIRTUAL TABLE IF NOT EXISTS forum_index USING fts5("
		"    url UNINDEXED,"          // RetroShare URL (retroshare://forum?id=...)
		"    type UNINDEXED,"         // 'group' or 'post'
		"    group_id UNINDEXED,"     // Forum group ID
		"    msg_id UNINDEXED,"       // Message ID (NULL for groups)
		"    title,"                  // Forum/Post title (indexed for search)
		"    content,"                // Forum description or post content (indexed)
		"    author_id UNINDEXED,"    // GxsId of the author
		"    author_name,"            // Author name (indexed for search by author)
		"    forum_name,"             // Forum name (indexed for search in forum names)
		"    publish_ts UNINDEXED,"   // Publish timestamp
		"    circle_type UNINDEXED"   // PUBLIC/PRIVATE/RESTRICTED (for security filtering)
		");";
	
    if(!mDb->execSQL(createTableSQL))
    {
        RsErr() << "DEEPSEARCH: Failed to create FTS5 table. Trying fallback to FTS4...";
        
        // Fallback to FTS4
        const char* createTableSQL_FTS4 = 
        "CREATE VIRTUAL TABLE IF NOT EXISTS forum_index USING fts4("
        "    url UNINDEXED,"          // RetroShare URL (retroshare://forum?id=...)
        "    type UNINDEXED,"         // 'group' or 'post'
        "    group_id UNINDEXED,"     // Forum group ID
        "    msg_id UNINDEXED,"       // Message ID (NULL for groups)
        "    title,"                  // Forum/Post title (indexed for search)
        "    content,"                // Forum description or post content (indexed)
        "    author_id UNINDEXED,"    // GxsId of the author
        "    author_name,"            // Author name (indexed for search by author)
        "    forum_name,"             // Forum name (indexed for search in forum names)
        "    publish_ts UNINDEXED,"   // Publish timestamp
        "    circle_type UNINDEXED"   // PUBLIC/PRIVATE/RESTRICTED (for security filtering)
        ");";
        
        if(!mDb->execSQL(createTableSQL_FTS4))
        {
            RsErr() << "DEEPSEARCH: Failed to create FTS4 table as well.";
            return std::errc::io_error;
        }
        RsDbg() << "DEEPSEARCH: FTS4 table 'forum_index' created (fallback).";
        mIsFTS5 = false;
    }
    else
    {
         RsDbg() << "DEEPSEARCH: FTS5 table 'forum_index' created successfully.";
    }
    
    RsDbg() << "DEEPSEARCH: FTS5 table 'forum_index' ready (isFTS5=" << mIsFTS5 << ")";
    return std::error_condition();
}

void DeepForumsIndexFTS5::beginTransaction()
{
    if(!mDb) initDatabase();
    if(mDb) mDb->execSQL("BEGIN;");
}

void DeepForumsIndexFTS5::commitTransaction()
{
    if(!mDb) initDatabase();
    if(mDb) mDb->execSQL("COMMIT;");
}

// Generate forum index ID
/*static*/ std::string DeepForumsIndexFTS5::forumIndexId(const RsGxsGroupId& grpId)
{
	RsUrl forumIndexId(RsGxsForums::DEFAULT_FORUM_BASE_URL);
	forumIndexId.setQueryKV(
		RsGxsForums::FORUM_URL_ID_FIELD, grpId.toStdString()
	);
	return forumIndexId.toString();
}

// Generate post index ID
/*static*/ std::string DeepForumsIndexFTS5::postIndexId(
	const RsGxsGroupId& grpId,
	const RsGxsMessageId& msgId
)
{
	RsUrl postIndexId(RsGxsForums::DEFAULT_FORUM_BASE_URL);
	postIndexId.setQueryKV(RsGxsForums::FORUM_URL_ID_FIELD, grpId.toStdString());
	postIndexId.setQueryKV(RsGxsForums::FORUM_URL_MSG_ID_FIELD, msgId.toStdString());
	return postIndexId.toString();
}

// Get default database path
/*static*/ std::string DeepForumsIndexFTS5::dbDefaultPath()
{
	return RsAccounts::AccountDirectory() + "/deep_forum_index_fts5.db";
}
