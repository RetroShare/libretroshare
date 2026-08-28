/*
 * RetroShare Web User Interface
 *
 * Copyright (C) 2019  Cyril Soler <csoler@users.sourceforge.net>
 * Copyright (C) 2022  Gioacchino Mazzurco <gio@retroshare.cc>
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU Affero General Public License as published by the
 * Free Software Foundation, version 3.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.
 * See the GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>
 *
 * SPDX-FileCopyrightText: 2004-2019 RetroShare Team <contact@retroshare.cc>
 * SPDX-License-Identifier: AGPL-3.0-only
 */

#include "p3webui.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <sstream>
#include <thread>
#include <iostream>
#include <fstream>
#include <memory>
#include <chrono>
#include <cstdlib>

#include "util/rsthreads.h"
#include "util/rsdebug.h"
#include "retroshare/rswebui.h"
#include "retroshare/rsinit.h"
#include "rsserver/rsaccounts.h"
#include "retroshare/rsjsonapi.h"
#include "util/rsdir.h"
#include "util/rsbase64.h"

/*extern*/ RsWebUi* rsWebUi = new p3WebUI;

enum MimeTypeIndex
{
	TEXT_HTML,
	TEXT_CSS,
	APPLICATION_JAVASCRIPT,
	TEXT_SVG,
	TEXT_TTF,
	TEXT_WOFF,
	APPLICATION_OCTET_STREAM,
};

static const constexpr char* const mime_types[] =
{
	"text/html",
	"text/css",
	"application/javascript",
	"image/svg+xml",
	"font/ttf",
	"font/woff",
	"application/octet-stream",
};

const std::string RsWebUi::DEFAULT_BASE_DIRECTORY =
        RsAccountsDetail::PathDataDirectory(false) + "/webui/";

static std::string _base_directory = RsWebUi::DEFAULT_BASE_DIRECTORY;

template<MimeTypeIndex MIME_TYPE_INDEX> class handler
{
public:
	static void get_handler( const std::shared_ptr< restbed::Session > session )
	{
		const auto request = session->get_request( );
		const std::string filename = request->get_path_parameter( "filename" );
		std::string directory = request->get_path_parameter( "dir" );

		if(!directory.empty()) directory += "/";

		std::string resource_filename = _base_directory + "/" + directory + filename;
		RsDbg() << "Reading file: \"" << resource_filename << "\"" << std::endl;
		std::ifstream stream( resource_filename, std::ifstream::binary);

		if(stream.is_open())
		{
			const std::vector<uint8_t> body = std::vector<uint8_t>(
			            std::istreambuf_iterator<char>(stream),
			            std::istreambuf_iterator<char>() );

			RsDbg() << __PRETTY_FUNCTION__
					<< " body length=" << body.size() << std::endl;

			const std::multimap<std::string, std::string> headers
			{
				{ "Content-Type", mime_types[MIME_TYPE_INDEX] },
				{ "Content-Length", std::to_string(body.size()) }
			};

			session->close(restbed::OK, body, headers);
		}
		else
		{
			RsErr() << __PRETTY_FUNCTION__ << "Could not open file: "
			        << resource_filename << std::endl;
			session->close(restbed::NOT_FOUND);
		}
	}
};

static std::string jsonEscape(const std::string& value)
{
	std::string escaped;
	for(char c : value)
		switch(c)
		{
		case '\\': escaped += "\\\\"; break;
		case '"': escaped += "\\\""; break;
		case '\n': escaped += "\\n"; break;
		case '\r': escaped += "\\r"; break;
		case '\t': escaped += "\\t"; break;
		default: escaped += c;
		}
	return escaped;
}

static const char* stickerMime(const std::filesystem::path& path)
{
	std::string ext = path.extension().string();
	std::transform(ext.begin(), ext.end(), ext.begin(),
	               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	if(ext == ".png") return "image/png";
	if(ext == ".jpg" || ext == ".jpeg") return "image/jpeg";
	if(ext == ".gif") return "image/gif";
	if(ext == ".webp") return "image/webp";
	return nullptr;
}

static void stickers_handler(const std::shared_ptr<restbed::Session> session)
{
	namespace fs = std::filesystem;
	const std::vector<fs::path> roots = {
		fs::path(RsAccounts::AccountDirectory()) / "stickers",
		fs::path(RsAccounts::ConfigDirectory()) / "stickers",
		fs::path(RsAccounts::systemDataDirectory()) / "stickers"
	};
	std::ostringstream json;
	json << "{\"groups\":[";
	bool firstGroup = true;
	for(const fs::path& root : roots)
	{
		std::error_code ec;
		if(!fs::is_directory(root, ec)) continue;
		std::vector<fs::path> directories = { root };
		for(fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end;
		    it != end; it.increment(ec))
			if(!ec && it->is_directory()) directories.push_back(it->path());
		for(const fs::path& directory : directories)
		{
			std::vector<fs::path> images;
			for(fs::directory_iterator it(directory, fs::directory_options::skip_permission_denied, ec), end;
			    it != end; it.increment(ec))
				if(!ec && it->is_regular_file() && stickerMime(it->path())
				        && it->path().filename() != "groupicon.png") images.push_back(it->path());
			if(images.empty()) continue;
			std::sort(images.begin(), images.end());
			if(!firstGroup) json << ',';
			firstGroup = false;
			json << "{\"name\":\"" << jsonEscape(directory.filename().string()) << "\",\"stickers\":[";
			bool firstSticker = true;
			for(const fs::path& image : images)
			{
				std::ifstream stream(image, std::ios::binary);
				if(!stream) continue;
				const std::vector<uint8_t> bytes {
				        std::istreambuf_iterator<char>(stream),
				        std::istreambuf_iterator<char>() };
				std::string encoded;
				RsBase64::encode(bytes.data(), bytes.size(), encoded, true, false);
				if(!firstSticker) json << ',';
				firstSticker = false;
				json << "{\"name\":\"" << jsonEscape(image.filename().string())
				     << "\",\"src\":\"data:" << stickerMime(image) << ";base64," << encoded << "\"}";
			}
			json << "]}";
		}
	}
	json << "]}";
	const std::string bodyString = json.str();
	const std::vector<uint8_t> body(bodyString.begin(), bodyString.end());
	const std::multimap<std::string, std::string> headers {
		{ "Content-Type", "application/json" },
		{ "Content-Length", std::to_string(body.size()) },
		{ "Cache-Control", "no-store" }
	};
	session->close(restbed::OK, body, headers);
}

std::vector< std::shared_ptr<restbed::Resource> > p3WebUI::getResources() const
{
	static std::vector< std::shared_ptr<restbed::Resource> > rtab;

	if(rtab.empty())
	{
		/* TODO: better not hardcode file names here so the C++ code can remain
		 * the same and WebUI modified indipendently */

		auto resource1 = std::make_shared< restbed::Resource >();
		resource1->set_paths( {
		                          "/{filename: index.html}",
		                      } );
		resource1->set_method_handler( "GET", handler<TEXT_HTML>::get_handler );

		auto resource2 = std::make_shared< restbed::Resource >();
		resource2->set_paths( {
		                          "/{filename: app.js}",
		                      } );
		resource2->set_method_handler( "GET", handler<APPLICATION_JAVASCRIPT>::get_handler );

		auto resource3 = std::make_shared< restbed::Resource >();
		resource3->set_paths( {
		                          "/{filename: styles.css}",
		                      } );
		resource3->set_method_handler( "GET", handler<TEXT_CSS>::get_handler );

		auto resource4 = std::make_shared< restbed::Resource >();
		resource4->set_paths( {
		                          "/{dir: images}/{filename: retroshare.svg}",
		                          "/{dir: webfonts}/{filename: fa-solid-900.svg}",
		                      } );
		resource4->set_method_handler( "GET", handler<TEXT_SVG>::get_handler );

		auto resource5 = std::make_shared< restbed::Resource >();
		resource5->set_paths( {
		                          "/{dir: webfonts}/{filename: fa-solid-900.ttf}",
		                          "/{dir: webfonts}/{filename: Roboto-Regular.ttf}",
		                          "/{dir: webfonts}/{filename: Roboto-Italic.ttf}",
		                          "/{dir: webfonts}/{filename: Roboto-Light.ttf}",
		                          "/{dir: webfonts}/{filename: Roboto-LightItalic.ttf}",
		                          "/{dir: webfonts}/{filename: Roboto-Medium.ttf}",
		                          "/{dir: webfonts}/{filename: Roboto-MediumItalic.ttf}",
		                          "/{dir: webfonts}/{filename: Roboto-Bold.ttf}",
		                          "/{dir: webfonts}/{filename: Roboto-BoldItalic.ttf}",
		                      } );
		resource5->set_method_handler( "GET", handler<TEXT_TTF>::get_handler );

		auto resource6 = std::make_shared< restbed::Resource >();
		resource6->set_paths( {
		                          "/{dir: webfonts}/{filename: fa-solid-900.woff}",
		                          "/{dir: webfonts}/{filename: fa-solid-900.woff2}",
		                          "/{dir: webfonts}/{filename: Roboto-Regular.woff}",
		                          "/{dir: webfonts}/{filename: Roboto-Regular.woff2}",
		                          "/{dir: webfonts}/{filename: Roboto-Italic.woff}",
		                          "/{dir: webfonts}/{filename: Roboto-Italic.woff2}",
		                          "/{dir: webfonts}/{filename: Roboto-Light.woff}",
		                          "/{dir: webfonts}/{filename: Roboto-Light.woff2}",
		                          "/{dir: webfonts}/{filename: Roboto-LightItalic.woff}",
		                          "/{dir: webfonts}/{filename: Roboto-LightItalic.woff2}",
		                          "/{dir: webfonts}/{filename: Roboto-Medium.woff}",
		                          "/{dir: webfonts}/{filename: Roboto-Medium.woff2}",
		                          "/{dir: webfonts}/{filename: Roboto-MediumItalic.woff}",
		                          "/{dir: webfonts}/{filename: Roboto-MediumItalic.woff2}",
		                          "/{dir: webfonts}/{filename: Roboto-Bold.woff}",
		                          "/{dir: webfonts}/{filename: Roboto-Bold.woff2}",
		                          "/{dir: webfonts}/{filename: Roboto-BoldItalic.woff}",
		                          "/{dir: webfonts}/{filename: Roboto-BoldItalic.woff2}",
		                      } );
		resource6->set_method_handler( "GET", handler<TEXT_WOFF>::get_handler );

		auto resource7 = std::make_shared< restbed::Resource >();
		resource7->set_paths( {
		                          "/{dir: webfonts}/{filename: fa-solid-900.eot}",
		                      } );
		resource7->set_method_handler( "GET", handler<APPLICATION_OCTET_STREAM>::get_handler );

		auto stickersResource = std::make_shared<restbed::Resource>();
		stickersResource->set_paths( { "/{dir: stickers}/{filename: index.json}" } );
		stickersResource->set_method_handler("GET", stickers_handler);

		rtab.push_back(resource1);
		rtab.push_back(resource2);
		rtab.push_back(resource3);
		rtab.push_back(resource4);
		rtab.push_back(resource5);
		rtab.push_back(resource6);
		rtab.push_back(resource7);
		rtab.push_back(stickersResource);
	}

	return rtab;
}

std::string p3WebUI::htmlFilesDirectory() const
{
    return _base_directory;
}

std::error_condition p3WebUI::setHtmlFilesDirectory(const std::string& html_dir)
{
#ifdef DEBUG
	RS_DBG("html_dir: ", html_dir);
#endif

	if(!RsDirUtil::checkDirectory(html_dir))
	{
		RS_ERR(html_dir, " is not a directory");
		return std::errc::not_a_directory;
	}

	if(!RsDirUtil::fileExists(html_dir+"/index.html"))
	{
		RS_ERR(html_dir, " doesn't seems a valid webui dir lacks index.html");
		return std::errc::no_such_file_or_directory;
	}

	_base_directory = html_dir;
	return std::error_condition();
}

bool p3WebUI::isRunning() const
{ return rsJsonApi->isRunning() && rsJsonApi->hasResourceProvider(*this); }

std::error_condition p3WebUI::setUserPassword(const std::string& passwd)
{
	RS_DBG("Updating webui token with new password");

	auto err = rsJsonApi->authorizeUser("webui", passwd);
	if(err)
		RS_ERR("Cannot register webui token, authorizeUser(...) return ", err);
	return err;
}

std::error_condition p3WebUI::restart()
{
    if(!rsJsonApi->hasResourceProvider(*this))
        rsJsonApi->registerResourceProvider(*this);

    return rsJsonApi->restart(true);
}

std::error_condition p3WebUI::stop()
{
	rsJsonApi->unregisterResourceProvider(*this);
    return rsJsonApi->restart(true);
}
