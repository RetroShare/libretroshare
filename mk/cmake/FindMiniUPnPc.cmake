## ---------------------------------------------------------------------- ##
 # mk/cmake/FindMiniUPnPc.cmake
 # This file is part of libRetroShare.
 #
 # Copyright (C) 2026      David Bears <dbear4q@gmail.com>
 #
 # This program is free software; you can redistribute it and/or modify
 # it under the terms of the GNU General Public License as published by
 # the Free Software Foundation; either version 2 of the License, or
 # (at your option) any later version.
 #
 # This program is distributed in the hope that it will be useful,
 # but WITHOUT ANY WARRANTY; without even the implied warranty of
 # MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 # GNU General Public License for more details.
 #
 # You should have received a copy of the GNU General Public License along
 # with this program; if not, write to the Free Software Foundation, Inc.,
 # 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
## ---------------------------------------------------------------------- ##

# 3.7 : pkg_check_modules(IMPORTED_TARGET)
# 3.9 : string(REGEX MATCH) ... CMAKE_MATCH_<n>
cmake_minimum_required(VERSION 3.9...4.4)

if(MiniUPnPc_FOUND)
	return()
endif()

find_package(PkgConfig)
include(FindPackageHandleStandardArgs)

if(PkgConfig_FOUND)
	pkg_check_modules(MiniUPnPc IMPORTED_TARGET miniupnpc)
endif()

if(MiniUPnPc_FOUND)
	find_package_handle_standard_args(MiniUPnPc
		REQUIRED_VARS MiniUPnPc_FOUND
		VERSION_VAR MiniUPnPc_VERSION
		HANDLE_VERSION_RANGE
	)

	if(MiniUPnPc_FOUND)
		add_library(miniupnpc::miniupnpc ALIAS PkgConfig::MiniUPnPc)
	endif()

	return()
endif()


# fall back to system introspection

find_library(MiniUPnPc_LIBRARY NAMES miniupnpc)
find_path(MiniUPnPc_INCLUDE NAMES miniupnpc/miniupnpc.h)

if(MiniUPnPc_INCLUDE)
	file(READ ${MiniUPnPc_INCLUDE}/miniupnpc/miniupnpc.h MiniUPnPc_miniupnpc.h)
	string(REGEX MATCH
		"#define MINIUPNPC_VERSION[ \t]+\"([^\"]*)\""
		MiniUPnPc_version_line
		"${MiniUPnPc_miniupnpc.h}"
	)
	set(MiniUPnPc_VERSION ${CMAKE_MATCH_1})
	unset(MiniUPnPc_miniupnpc.h)
	unset(MiniUPnPc_version_line)
endif()

find_package_handle_standard_args(MiniUPnPc
	REQUIRED_VARS MiniUPnPc_LIBRARY MiniUPnPc_INCLUDE
	VERSION_VAR MiniUPnPc_VERSION
	HANDLE_VERSION_RANGE
)

if(MiniUPnPc_FOUND AND NOT TARGET miniupnpc::miniupnpc)
	add_library(miniupnpc::miniupnpc UNKNOWN IMPORTED)
	set_target_properties(miniupnpc::miniupnpc PROPERTIES
		IMPORTED_LOCATION ${MiniUPnPc_LIBRARY}
		INTERFACE_INCLUDE_DIRECTORIES ${MiniUPnPc_INCLUDE}
	)
endif()
