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

#include "content_search/htmltext.hpp"
#include "util/stacktrace.h"
#include "util/rsthreads.h"
#include "util/rsdebuglevel0.h"

namespace ContentSearch
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
