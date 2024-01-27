/*******************************************************************************
 * libretroshare                                                               *
 *                                                                             *
 * Copyright (C) 2016-2024  Gioacchino Mazzurco <gio@retroshare.cc>            *
 * Copyright (C) 2024  Asociación Civil Altermundi <info@altermundi.net>
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
#pragma once

#include <cstdio>


/**
 * @brief Print a backtrace to FILE* out.
 * @param[in] demangle true to demangle C++ symbols requires malloc working,
 *	in some patological cases like a SIGSEGV received during a malloc this
 *	would cause deadlock so pass false if you may be in such situation
 *	(like in a SIGSEGV handler)
 * @param[in] out output file
 * @param[in] maxFrames maximum number of stack frames you want to bu printed
 */
void print_stacktrace(
        bool demangle = true, FILE *out = stderr, unsigned int maxFrames = 63 );

/**
 * @brief CrashStackTrace catch crash signals and print stack trace
 * Place an instance of this in your main file to get stacktraces printed
 * automatically on crash
 */
struct CrashStackTrace
{
	CrashStackTrace();

	[[ noreturn ]]
	static void abortHandler(int signum);
};
