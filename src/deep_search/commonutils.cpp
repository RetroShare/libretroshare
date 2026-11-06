/*******************************************************************************
 * RetroShare full text indexing and search implementation based on Xapian     *
 *                                                                             *
 * Copyright (C) 2018-2021  Gioacchino Mazzurco <gio@retroshare.cc>             *
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

#include <algorithm>
#include <cctype>
#include <cstring>
#include <thread>

#include "deep_search/commonutils.hpp"
#include "util/stacktrace.h"
#include "util/rsthreads.h"
#include "util/rsdebuglevel0.h"

namespace DeepSearch
{

std::string simpleTextHtmlExtract(const std::string& rsHtmlDoc)
{
	if(rsHtmlDoc.empty()) return rsHtmlDoc;

	const bool isPlainMsg =
	        rsHtmlDoc[0] != '<' || rsHtmlDoc[rsHtmlDoc.size() - 1] != '>';
	if(isPlainMsg) return rsHtmlDoc;

	/* One linear pass, no limit on the number of tags: big posts made of
	 * thousands of tags are extracted whole. A tag becomes a space, so words
	 * on both sides of <br> or </p> are not glued together. The content of
	 * <style>, <script> and <head> is dropped. The common entities are
	 * decoded. Nothing is logged: this is private content. */
	const std::string& in = rsHtmlDoc;
	const size_t n = in.size();

	auto startsWithNoCase = [&](size_t pos, const char* what)
	{
		for(size_t k = 0; what[k]; ++k)
			if( pos + k >= n ||
			        std::tolower(static_cast<unsigned char>(in[pos + k])) != what[k] )
				return false;
		return true;
	};

	// "<style" must not match "<styles", nor "<head" match "<header"
	auto isTag = [&](size_t pos, const char* name)
	{
		if(!startsWithNoCase(pos, name)) return false;
		const size_t after = pos + strlen(name);
		return after >= n || std::strchr(" \t\r\n/>", in[after]) != nullptr;
	};

	// Skip to after the closing tag of an element whose content is dropped
	auto skipElement = [&](size_t pos, const char* closing)
	{
		for(size_t p = pos; p < n; ++p)
			if(in[p] == '<' && isTag(p, closing))
			{
				const size_t end = in.find('>', p);
				return end == std::string::npos ? n : end + 1;
			}
		return n;
	};

	static const struct { const char* name; char value; } entities[] =
	{
		{ "&nbsp;", ' ' }, { "&amp;", '&' }, { "&lt;", '<' }, { "&gt;", '>' },
		{ "&quot;", '"' }, { "&apos;", '\'' }, { "&#39;", '\'' }
	};

	std::string out;
	out.reserve(n);
	auto addSpace = [&]() { if(!out.empty() && out.back() != ' ') out += ' '; };

	size_t i = 0;
	while(i < n)
	{
		const char c = in[i];
		if(c == '<')
		{
			if(isTag(i, "<style")) i = skipElement(i, "</style");
			else if(isTag(i, "<script")) i = skipElement(i, "</script");
			else if(isTag(i, "<head")) i = skipElement(i, "</head");
			else
			{
				const size_t end = in.find('>', i);
				i = end == std::string::npos ? n : end + 1;
			}
			addSpace();
		}
		else if(c == '&')
		{
			bool decoded = false;
			for(const auto& e : entities)
				if(startsWithNoCase(i, e.name))
				{
					if(e.value == ' ') addSpace(); else out += e.value;
					i += strlen(e.name);
					decoded = true;
					break;
				}
			if(!decoded) { out += c; ++i; }
		}
		else if(c == '\n' || c == '\r' || c == '\t' || c == ' ')
		{
			addSpace();
			++i;
		}
		else
		{
			out += c;
			++i;
		}
	}

	while(!out.empty() && out.back() == ' ') out.pop_back();
	return out;
}

}

// Xapian-specific code (only for channels/files indexing)
#if defined(RS_DEEP_CHANNEL_INDEX) || defined(RS_DEEP_FILES_INDEX)

#ifndef XAPIAN_AT_LEAST
/// Added in Xapian 1.4.2.
#define XAPIAN_AT_LEAST(A,B,C) \
	(XAPIAN_MAJOR_VERSION > (A) || \
	(XAPIAN_MAJOR_VERSION == (A) && \
	(XAPIAN_MINOR_VERSION > (B) || \
	(XAPIAN_MINOR_VERSION == (B) && XAPIAN_REVISION >= (C)))))
#endif

namespace DeepSearch
{
std::unique_ptr<Xapian::Database> openReadOnlyDatabase(
        const std::string& path, int flags )
{
	try
	{
#if XAPIAN_AT_LEAST(1,3,2)
		std::unique_ptr<Xapian::Database> dbPtr(
		        new Xapian::Database(path, flags) );
#else
		std::unique_ptr<Xapian::Database> dbPtr(new Xapian::Database(path));
		if(flags)
		{
			RS_WARN( "Xapian DB flags: ", flags, " ignored due to old Xapian "
			         "library version: ", XAPIAN_VERSION, " < 1.3.2" );
		}
#endif
		return dbPtr;
	}
	catch(Xapian::DatabaseOpeningError& e)
	{
		RsWarn() << __PRETTY_FUNCTION__ << " " << e.get_msg()
		          << ", probably nothing has been indexed yet." << std::endl;
	}
	catch(Xapian::DatabaseLockError&)
	{
		RsErr() << __PRETTY_FUNCTION__ << " Failed aquiring Xapian DB lock "
		        << path << std::endl;
		print_stacktrace();
	}
	catch(...)
	{
		RsErr() << __PRETTY_FUNCTION__ << " Xapian DB is apparently corrupted "
		        << "deleting it might help without causing any harm: "
		        << path << std::endl;
		print_stacktrace();
	}

	return nullptr;
}

std::string timetToXapianDate(const rstime_t& time)
{
	char date[] = "YYYYMMDD\0";
	time_t tTime = static_cast<time_t>(time);
	std::strftime(date, 9, "%Y%m%d", std::gmtime(&tTime));
	return date;
}

StubbornWriteOpQueue::~StubbornWriteOpQueue()
{
	auto fErr = flush(0);
	if(fErr)
	{
		RS_FATAL( "Flush failed on destruction ", mOpStore.size(),
		          " operations irreparably lost ", fErr );
		print_stacktrace();
	}
}

void StubbornWriteOpQueue::push(write_op op)
{
	RS_DBG4("");

	{
		std::unique_lock<std::mutex> lock(mQueueMutex);
		mOpStore.push(op);
	}

	flush();
}

std::error_condition StubbornWriteOpQueue::flush(
        rstime_t acceptDelay, rstime_t callTS )
{
	RS_DBG4("");

	{
		// Return without attempt to open the database if the queue is empty
		std::unique_lock<std::mutex> lock(mQueueMutex);
		if(mOpStore.empty()) return std::error_condition();
	}

	std::unique_ptr<Xapian::WritableDatabase> dbPtr;
	try
	{
		dbPtr = std::make_unique<Xapian::WritableDatabase>(
		            mDbPath, Xapian::DB_CREATE_OR_OPEN );
	}
	catch(Xapian::DatabaseLockError)
	{
		if(acceptDelay)
		{
			rstime_t tNow = time(nullptr);
			rstime_t maxRemaining = tNow - (callTS + acceptDelay);
			if(maxRemaining > 0)
			{
				std::chrono::milliseconds interval(
				            std::max(rstime_t(50), maxRemaining*1000/5) );
				RS_DBG3( "Cannot acquire database write lock, retrying in:",
				         interval.count(), "ms" );
				RsThread::async([this, acceptDelay, callTS, interval]()
				{
					std::this_thread::sleep_for(interval);
					flush(acceptDelay, callTS);
				});
				return std::error_condition();
			}
			else
			{
				RS_ERR(std::errc::timed_out, acceptDelay, callTS, tNow);
				return std::errc::timed_out;
			}
		}
		else return std::errc::resource_unavailable_try_again;
	}
	catch(...)
	{
		RS_ERR("Xapian DB ", mDbPath, " is apparently corrupted");
		print_stacktrace();
		return std::errc::io_error;
	}

	std::unique_lock<std::mutex> lock(mQueueMutex);
	while(!mOpStore.empty())
	{
		auto op = mOpStore.front(); mOpStore.pop();
		op(*dbPtr);
	}
	return std::error_condition();
}

}

#endif // RS_DEEP_CHANNEL_INDEX || RS_DEEP_FILES_INDEX
