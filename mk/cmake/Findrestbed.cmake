## ------------------------------------------------------------------------- ##
 # mk/cmake/Findrestbed.cmake
 # This file is part of libRetroShare.
 #
 # Copyright (C) 2026      David Bears <dbear4q@gmail.com>
 #
 # SPDX-License-Identifier: AGPL-3.0-or-later
 #
 # This program is free software: you can redistribute it and/or modify
 # it under the terms of the GNU Affero General Public License as
 # published by the Free Software Foundation, either version 3 of the
 # License, or (at your option) any later version.
 #
 # This program is distributed in the hope that it will be useful,
 # but WITHOUT ANY WARRANTY; without even the implied warranty of
 # MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 # GNU Affero General Public License for more details.
 #
 # You should have received a copy of the GNU Affero General Public License
 # along with this program.  If not, see <https://www.gnu.org/licenses/>.
## ------------------------------------------------------------------------- ##

cmake_minimum_required(VERSION 3.0...4.4)

find_library(RESTBED_LIBRARY NAMES restbed)
find_path(RESTBED_INCLUDE NAMES restbed PATH_SUFFIXES corvusoft/restbed source)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(restbed
	REQUIRED_VARS RESTBED_LIBRARY RESTBED_INCLUDE
)

if(restbed_FOUND AND NOT TARGET restbed::restbed)
	add_library(restbed::restbed UNKNOWN IMPORTED)
	set_target_properties(restbed::restbed PROPERTIES
		IMPORTED_LOCATION "${RESTBED_LIBRARY}"
		INTERFACE_INCLUDE_DIRECTORIES "${RESTBED_INCLUDE}"
	)
endif()
