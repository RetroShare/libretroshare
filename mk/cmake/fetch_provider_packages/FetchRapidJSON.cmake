## ------------------------------------------------------------------------- ##
 # mk/cmake/fetch_provider_packages/Fetchrnp.cmake
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

# 3.11: FetchContent
# 3.13: CMP0077
# 3.14: FetchContent_MakeAvailable()
cmake_minimum_required(VERSION 3.14...4.4)

set(Fetch${FETCH_PROVIDER_PACKAGE_NAME}_GIT_TAG "" CACHE STRING
	"git tag to use for fetching ${FETCH_PROVIDER_PACKAGE_NAME}"
)
mark_as_advanced(Fetch${FETCH_PROVIDER_PACKAGE_NAME}_GIT_TAG)
if(NOT Fetch${FETCH_PROVIDER_PACKAGE_NAME}_GIT_TAG)
	set(Fetch${FETCH_PROVIDER_PACKAGE_NAME}_GIT_TAG origin/master)
endif()

set(RAPIDJSON_BUILD_EXAMPLES OFF)
set(RAPIDJSON_BUILD_TESTS OFF)
set(CMAKE_EXPORT_NO_PACKAGE_REGISTRY TRUE)

include(FetchContent)
FetchContent_Declare(RapidJSON
	GIT_REPOSITORY "https://github.com/Tencent/rapidjson.git"
	GIT_TAG ${Fetch${FETCH_PROVIDER_PACKAGE_NAME}_GIT_TAG}
	GIT_PROGRESS TRUE
	EXCLUDE_FROM_ALL
)
FetchContent_MakeAvailable(RapidJSON)
set(${FETCH_PROVIDER_PACKAGE_NAME}_FOUND TRUE)

target_include_directories(RapidJSON INTERFACE
  "$<BUILD_INTERFACE:${RapidJSON_SOURCE_DIR}/include>"
)
