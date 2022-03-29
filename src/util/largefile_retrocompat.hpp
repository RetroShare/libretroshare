/*******************************************************************************
 *                                                                             *
 * libretroshare: retroshare core library                                      *
 *                                                                             *
 * Copyright (C) 2016-2022  Gioacchino Mazzurco <gio@retroshare.cc>            *
 * Copyright (C) 2021-2022  Asociación Civil Altermundi <info@altermundi.net>  *
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

#ifdef __ANDROID__

#	include <android/api-level.h>

#	if __ANDROID_API__ < 21
#		include <sys/vfs.h>
#		define statvfs64 statfs
#		warning statvfs64 is not supported with android platform < 21 falling back to statfs that is untested (may misbehave)
#	else // __ANDROID_API__ < 21
#		include <sys/statvfs.h>
#	endif // __ANDROID_API__ < 21

#	if __ANDROID_API__ < 24
#		define fopen64 fopen
#		define fseeko64 fseeko
#		define ftello64 ftello
#	endif // __ANDROID_API__ < 24

#endif // def __ANDROID__

#ifdef __APPLE__
#	define fopen64 fopen
#	define fseeko64 fseeko
#	define ftello64 ftello
#	define stat64 stat
#endif // def __APPLE__
