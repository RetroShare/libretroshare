/*******************************************************************************
 * libretroshare/src/retroshare/util/rskbdinput.h                              *
 *                                                                             *
 * Copyright (C) 2019  Cyril Soler <csoler@users.sourceforge.net>              *
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

#include <string>

namespace RsUtil {

	/**
	 * @brief Read a line from the console, optionally without echoing it.
	 * @param prompt      text written before reading, may be empty
	 * @param no_echo     when true the typed characters are not shown
	 * @param eof         optional, set to true when the input stream ended
	 *                    before a complete line could be read
	 * @return the line read, or an empty string when the input ended
	 *
	 * When the input stream is closed -- a service started from systemd or
	 * docker with no terminal attached, stdin redirected from /dev/null, an
	 * ssh session dropping mid-prompt -- there is no line to return and *eof
	 * is set. Callers looping on this function must stop on that condition,
	 * otherwise they spin on an input that will never come.
	 */
	std::string rs_getpass( const std::string& prompt, bool no_echo = true,
	                        bool* eof = nullptr );

}
