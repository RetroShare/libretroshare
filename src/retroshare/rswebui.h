/*
 * RetroShare Web User Interface public header
 *
 * Copyright (C) 2019  Cyril Soler <csoler@users.sourceforge.net>
 * Copyright (C) 2022  Gioacchino Mazzurco <gio@eigenlab.org>
 * Copyright (C) 2022  Asociación Civil Altermundi <info@altermundi.net>
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License for more
 * details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>
 *
 * SPDX-FileCopyrightText: 2004-2019 RetroShare Team <contact@retroshare.cc>
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */

#pragma once

#include <string>
#include <system_error>

class RsWebUi;

/**
 * Pointer to global instance of RsWebUi service implementation
 * jsonapi_temporarly_disabled{development} because it breaks compilation when
 * webui is disabled
 */
extern RsWebUi* rsWebUi;

class RsWebUi
{
public:
	static const std::string DEFAULT_BASE_DIRECTORY;

	/**
	 * @brief Restart WebUI
	 * @jsonapi{development}
	 */
	virtual std::error_condition restart() = 0;

	/**
	 * @brief Stop WebUI
	 * @jsonapi{development}
	 */
	virtual std::error_condition stop() = 0;

	/**
	 * @brief Set WebUI static files directory, need restart to apply
	 * @param[in] htmlDir directory path
	 * @jsonapi{development}
	 */
	virtual std::error_condition setHtmlFilesDirectory(
	        const std::string& htmlDir ) = 0;

    /**
     * @brief Get WebUI static files directory
     * @return html files directory path
     * @jsonapi{development}
     */
    virtual std::string htmlFilesDirectory() const=0;

	/**
	 * @brief Set WebUI user password
	 * @param[in] password new password for WebUI
	 * @return if some error occurred return details about it
	 * @jsonapi{development}
	 */
	virtual std::error_condition setUserPassword(
	        const std::string& password ) = 0;

	/**
	 * @brief check if WebUI is running
	 * @jsonapi{development}
	 * @return true if running, false otherwise
	 */
	virtual bool isRunning() const = 0;

	virtual ~RsWebUi() = default;
};
