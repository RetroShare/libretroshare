# ------------------------------------------------------------------------ *\
# mk/cmake/fetch_provider_packages/Fetchrnp.cmake
# This file is part of libRetroShare.
#
# Copyright (C) 2026      David Bears <dbear4q@gmail.com>
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
# ------------------------------------------------------------------------ */

cmake_minimum_required(VERSION 3.0...4.4)

include(FetchContent)
set(RAPIDJSON_BUILD_EXAMPLES OFF CACHE BOOL "Build rapidjson examples.")
set(RAPIDJSON_BUILD_TESTS OFF CACHE BOOL
	"Build rapidjson perftests and unittests."
)
set(CMAKE_EXPORT_NO_PACKAGE_REGISTRY TRUE)

FetchContent_Declare(RapidJSON
	GIT_REPOSITORY "https://github.com/Tencent/rapidjson.git"
	GIT_TAG "origin/master"
	GIT_SHALLOW TRUE
	GIT_PROGRESS TRUE
	TIMEOUT 10
	EXCLUDE_FROM_ALL
)
FetchContent_MakeAvailable(RapidJSON)
set(${FETCH_PROVIDER_PACKAGE_NAME}_FOUND TRUE)

target_include_directories(RapidJSON INTERFACE
  "$<BUILD_INTERFACE:${RapidJSON_SOURCE_DIR}/include>"
)
