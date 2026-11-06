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

#include "deep_search/forumsindex.hpp"
#include "deep_search/commonutils.hpp"
#include "retroshare/rsinit.h"
#include "util/retrodb.h"
#include "util/rsdebug.h"
#include "util/rsurl.h"

namespace
{
/* Bump when the table definition changes: the table is then dropped,
 * re-created and filled again from the GXS database.
 * 1: FTS4, unicode61 tokenizer, newest first.
 * 2: whole posts extracted from their HTML, whatever their number of tags. */
constexpr int FORUM_INDEX_SCHEMA_VERSION = 2;

/* Columns, in this order:
 *  0 group_id   forum id
 *  1 msg_id     post id, empty for the row describing the forum itself
 *  2 title      forum name or post title
 *  3 content    forum description or post body, HTML stripped
 *  4 publish_ts publish time, used to sort the results
 * Only title and content are tokenized. */
constexpr int CONTENT_COLUMN = 3;

const char* const CREATE_TABLE_SQL =
        "CREATE VIRTUAL TABLE forum_index USING fts4("
        "group_id, msg_id, title, content, publish_ts, "
        "notindexed=group_id, notindexed=msg_id, notindexed=publish_ts, "
        "tokenize=unicode61 \"remove_diacritics=1\", prefix=\"2,3\");";

/* User input is reduced to plain words before reaching the FTS query parser,
 * so FTS operators and syntax errors can not come from it, not even from the
 * distant peers whose turtle search requests land here. */
constexpr size_t MAX_QUERY_WORDS = 16;
constexpr size_t MAX_WORD_BYTES = 64;

/// RAII wrapper around a prepared statement
struct Statement
{
	Statement(sqlite3* db, const std::string& sql) : mDb(db), mStmt(nullptr)
	{
		if(sqlite3_prepare_v2(db, sql.c_str(), -1, &mStmt, nullptr)
		        != SQLITE_OK)
		{
			RsErr() << "DeepForumsIndex: cannot prepare \"" << sql << "\": "
			        << sqlite3_errmsg(db);
			mStmt = nullptr;
		}
	}
	~Statement() { if(mStmt) sqlite3_finalize(mStmt); }

	Statement(const Statement&) = delete;
	Statement& operator=(const Statement&) = delete;

	bool ok() const { return mStmt != nullptr; }

	void bindText(int i, const std::string& s)
	{ sqlite3_bind_text(mStmt, i, s.c_str(), static_cast<int>(s.size()),
		                SQLITE_TRANSIENT); }
	void bindInt64(int i, int64_t v) { sqlite3_bind_int64(mStmt, i, v); }

	/// @return SQLITE_ROW, SQLITE_DONE or an error code (already logged)
	int step()
	{
		int rc = sqlite3_step(mStmt);
		if(rc != SQLITE_ROW && rc != SQLITE_DONE)
			RsErr() << "DeepForumsIndex: query failed: " << rc << " "
			        << sqlite3_errmsg(mDb);
		return rc;
	}

	std::string columnText(int i)
	{
		auto t = reinterpret_cast<const char*>(sqlite3_column_text(mStmt, i));
		return t ? std::string(t) : std::string();
	}
	int64_t columnInt64(int i) { return sqlite3_column_int64(mStmt, i); }

private:
	sqlite3* mDb;
	sqlite3_stmt* mStmt;
};
}

DeepForumsIndex::DeepForumsIndex(
        const std::string& dbPath, const std::string& dbKey ) :
    mDbPath(dbPath), mDbKey(dbKey), mIndexMtx("DeepForumsIndex"),
    mUsable(false), mNeedsFullReindex(false)
{
	RS_STACK_MUTEX(mIndexMtx);
	openDatabase();
}

DeepForumsIndex::~DeepForumsIndex()
{
	RS_STACK_MUTEX(mIndexMtx);
	if(mDb) mDb->closeDb();
}

bool DeepForumsIndex::exec(const char* sql)
{
	char* err = nullptr;
	if(sqlite3_exec(mDb->getSqlHandle(), sql, nullptr, nullptr, &err)
	        == SQLITE_OK)
		return true;

	RsErr() << "DeepForumsIndex: \"" << sql << "\" failed: "
	        << (err ? err : "unknown error");
	sqlite3_free(err);
	return false;
}

std::error_condition DeepForumsIndex::openDatabase()
{
	mDb = std::make_unique<RetroDb>(
	            mDbPath, RetroDb::OPEN_READWRITE_CREATE, mDbKey );
	if(!mDb->isOpen())
	{
		RsErr() << "DeepForumsIndex: cannot open " << mDbPath
		        << ", forum content search disabled";
		mDb.reset();
		return std::errc::io_error;
	}

	int version = -1;
	{
		Statement st(mDb->getSqlHandle(), "PRAGMA user_version;");
		if(st.ok() && st.step() == SQLITE_ROW)
			version = static_cast<int>(st.columnInt64(0));
	}

	/* The version is written only once a full re-index has completed, so an
	 * interrupted re-index is started again from scratch at next startup. */
	if(version != FORUM_INDEX_SCHEMA_VERSION)
	{
		RsInfo() << "DeepForumsIndex: creating the forum index (schema "
		         << version << " -> " << FORUM_INDEX_SCHEMA_VERSION << ")";

		if( !exec("DROP TABLE IF EXISTS forum_index;") ||
		        !exec(CREATE_TABLE_SQL) )
		{
			RsErr() << "DeepForumsIndex: the SQLite library has no usable "
			        << "FTS4 support, forum content search disabled";
			return std::errc::function_not_supported;
		}
		mNeedsFullReindex = true;
	}

	mUsable = true;
	return std::error_condition();
}

bool DeepForumsIndex::needsFullReindex() const
{
	RS_STACK_MUTEX(mIndexMtx);
	return mUsable && mNeedsFullReindex;
}

void DeepForumsIndex::markFullReindexDone()
{
	RS_STACK_MUTEX(mIndexMtx);
	if(!mUsable) return;

	const std::string sql = "PRAGMA user_version = " +
	        std::to_string(FORUM_INDEX_SCHEMA_VERSION) + ";";
	if(exec(sql.c_str())) mNeedsFullReindex = false;
}

/*static*/ std::string DeepForumsIndex::buildMatchExpression(
        const std::string& queryStr, bool contentOnly )
{
	/* A word is a run of ASCII letters and digits or of non ASCII bytes, so
	 * UTF-8 text (accents, cyrillic...) is kept whole and left to the
	 * unicode61 tokenizer. Everything else separates words. Every word is
	 * matched as a prefix; FTS4 ANDs space separated terms. A column filter
	 * only accepts a bare term (content:"word*" matches nothing), bare terms
	 * are safe here as words carry no FTS syntax character. */
	std::vector<std::string> words;
	std::string cur;

	auto flush = [&]()
	{
		if(!cur.empty() && words.size() < MAX_QUERY_WORDS)
			words.push_back(cur.substr(0, MAX_WORD_BYTES));
		cur.clear();
	};

	for(unsigned char c : queryStr)
	{
		if( (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') ||
		        (c >= 'A' && c <= 'Z') || c >= 0x80 )
			cur += static_cast<char>(c);
		else flush();
	}
	flush();

	std::string expr;
	for(const auto& w : words)
	{
		if(!expr.empty()) expr += ' ';
		if(contentOnly) expr += "content:" + w + '*';
		else expr += '"' + w + "*\"";
	}
	return expr;
}

std::error_condition DeepForumsIndex::search(
        const std::string& queryStr,
        std::vector<DeepForumsSearchResult>& results, uint32_t maxResults,
        const RsGxsGroupId& forumId )
{
	results.clear();

	const std::string match =
	        buildMatchExpression(queryStr, !forumId.isNull());
	if(match.empty()) return std::error_condition();

	std::string sql =
	        "SELECT group_id, msg_id, snippet(forum_index, '<b>', '</b>', "
	        "'...', " + std::to_string(CONTENT_COLUMN) + ", 32) "
	        "FROM forum_index WHERE forum_index MATCH ?1";
	if(!forumId.isNull()) sql += " AND group_id = ?2 AND msg_id <> ''";
	sql += " ORDER BY CAST(publish_ts AS INTEGER) DESC LIMIT ?3;";

	RS_STACK_MUTEX(mIndexMtx);
	if(!mUsable) return std::errc::function_not_supported;

	Statement st(mDb->getSqlHandle(), sql);
	if(!st.ok()) return std::errc::io_error;

	st.bindText(1, match);
	if(!forumId.isNull()) st.bindText(2, forumId.toStdString());
	st.bindInt64(3, maxResults ? static_cast<int64_t>(maxResults) : -1);

	int rc;
	while((rc = st.step()) == SQLITE_ROW)
	{
		const RsGxsGroupId grpId(st.columnText(0));
		const std::string msgIdStr = st.columnText(1);

		DeepForumsSearchResult res;
		res.mUrl = msgIdStr.empty() ? forumIndexId(grpId) :
		                              postIndexId(grpId, RsGxsMessageId(msgIdStr));
		res.mWeight = 1.0;
		res.mSnippet = st.columnText(2);
		results.push_back(res);
	}

	if(rc != SQLITE_DONE) return std::errc::io_error;
	return std::error_condition();
}

std::error_condition DeepForumsIndex::indexDocument(
        const RsGxsGroupId& grpId, const RsGxsMessageId& msgId,
        const std::string& title, const std::string& content,
        rstime_t publishTs )
{
	RS_STACK_MUTEX(mIndexMtx);
	if(!mUsable) return std::errc::function_not_supported;

	/* FTS tables have no unique constraint: replace by docid, derived from
	 * the ids, so indexing the same forum or post twice keeps a single row. */
	const int64_t id = docId(grpId, msgId);

	Statement del(mDb->getSqlHandle(),
	              "DELETE FROM forum_index WHERE docid = ?1;");
	if(!del.ok()) return std::errc::io_error;
	del.bindInt64(1, id);
	if(del.step() != SQLITE_DONE) return std::errc::io_error;

	Statement ins(mDb->getSqlHandle(),
	              "INSERT INTO forum_index(docid, group_id, msg_id, title, "
	              "content, publish_ts) VALUES(?1, ?2, ?3, ?4, ?5, ?6);");
	if(!ins.ok()) return std::errc::io_error;
	ins.bindInt64(1, id);
	ins.bindText(2, grpId.toStdString());
	ins.bindText(3, msgId.isNull() ? std::string() : msgId.toStdString());
	ins.bindText(4, title);
	ins.bindText(5, content);
	ins.bindInt64(6, static_cast<int64_t>(publishTs));
	if(ins.step() != SQLITE_DONE) return std::errc::io_error;

	return std::error_condition();
}

std::error_condition DeepForumsIndex::indexForumGroup(
        const RsGxsForumGroup& forum )
{
	if(forum.mMeta.mGroupId.isNull()) return std::errc::invalid_argument;

	return indexDocument(
	            forum.mMeta.mGroupId, RsGxsMessageId(), forum.mMeta.mGroupName,
	            DeepSearch::simpleTextHtmlExtract(forum.mDescription),
	            forum.mMeta.mPublishTs );
}

std::error_condition DeepForumsIndex::indexForumPost(const RsGxsForumMsg& post)
{
	if(post.mMeta.mGroupId.isNull() || post.mMeta.mMsgId.isNull())
		return std::errc::invalid_argument;

	return indexDocument(
	            post.mMeta.mGroupId, post.mMeta.mMsgId, post.mMeta.mMsgName,
	            DeepSearch::simpleTextHtmlExtract(post.mMsg),
	            post.mMeta.mPublishTs );
}

std::error_condition DeepForumsIndex::removeForumFromIndex(
        const RsGxsGroupId& grpId )
{
	if(grpId.isNull()) return std::errc::invalid_argument;

	RS_STACK_MUTEX(mIndexMtx);
	if(!mUsable) return std::errc::function_not_supported;

	Statement st(mDb->getSqlHandle(),
	             "DELETE FROM forum_index WHERE group_id = ?1;");
	if(!st.ok()) return std::errc::io_error;
	st.bindText(1, grpId.toStdString());
	if(st.step() != SQLITE_DONE) return std::errc::io_error;

	return std::error_condition();
}

std::error_condition DeepForumsIndex::removeForumPostFromIndex(
        const RsGxsGroupId& grpId, const RsGxsMessageId& msgId )
{
	if(grpId.isNull() || msgId.isNull()) return std::errc::invalid_argument;

	RS_STACK_MUTEX(mIndexMtx);
	if(!mUsable) return std::errc::function_not_supported;

	Statement st(mDb->getSqlHandle(),
	             "DELETE FROM forum_index WHERE docid = ?1;");
	if(!st.ok()) return std::errc::io_error;
	st.bindInt64(1, docId(grpId, msgId));
	if(st.step() != SQLITE_DONE) return std::errc::io_error;

	return std::error_condition();
}

std::error_condition DeepForumsIndex::clearIndex()
{
	RS_STACK_MUTEX(mIndexMtx);
	if(!mUsable) return std::errc::function_not_supported;

	if(!exec("DELETE FROM forum_index;")) return std::errc::io_error;

	// DELETE keeps the freed pages, VACUUM gives them back to the file system
	exec("VACUUM;");
	return std::error_condition();
}

void DeepForumsIndex::beginTransaction()
{
	RS_STACK_MUTEX(mIndexMtx);
	if(mUsable) exec("BEGIN;");
}

void DeepForumsIndex::commitTransaction()
{
	RS_STACK_MUTEX(mIndexMtx);
	if(mUsable) exec("COMMIT;");
}

/*static*/ int64_t DeepForumsIndex::docId(
        const RsGxsGroupId& grpId, const RsGxsMessageId& msgId )
{
	/* 64 bit FNV-1a: stable across platforms and compilers, which matters as
	 * the value is stored in the database. Kept positive and non zero. */
	uint64_t h = 14695981039346656037ULL;
	auto feed = [&h](const unsigned char* p, size_t n)
	{
		for(size_t i = 0; i < n; ++i) { h ^= p[i]; h *= 1099511628211ULL; }
	};
	feed(grpId.toByteArray(), grpId.SIZE_IN_BYTES);
	if(!msgId.isNull()) feed(msgId.toByteArray(), msgId.SIZE_IN_BYTES);

	h &= 0x7FFFFFFFFFFFFFFFULL;
	return h ? static_cast<int64_t>(h) : 1;
}

/*static*/ std::string DeepForumsIndex::forumIndexId(const RsGxsGroupId& grpId)
{
	RsUrl forumIndexId(RsGxsForums::DEFAULT_FORUM_BASE_URL);
	forumIndexId.setQueryKV(
	            RsGxsForums::FORUM_URL_ID_FIELD, grpId.toStdString() );
	return forumIndexId.toString();
}

/*static*/ std::string DeepForumsIndex::postIndexId(
        const RsGxsGroupId& grpId, const RsGxsMessageId& msgId )
{
	RsUrl postIndexId(RsGxsForums::DEFAULT_FORUM_BASE_URL);
	postIndexId.setQueryKV(RsGxsForums::FORUM_URL_ID_FIELD, grpId.toStdString());
	postIndexId.setQueryKV(RsGxsForums::FORUM_URL_MSG_ID_FIELD, msgId.toStdString());
	return postIndexId.toString();
}

/*static*/ std::string DeepForumsIndex::dbDefaultPath()
{ return RsAccounts::AccountDirectory() + "/deep_forum_index.db"; }
