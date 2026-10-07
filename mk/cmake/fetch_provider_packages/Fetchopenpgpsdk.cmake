# ------------------------------------------------------------------------ *\
# mk/cmake/fetch_provider_packages/Fetchopenpgpsdk.cmake
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
FetchContent_Declare(openpgpsdk
	GIT_REPOSITORY "https://github.com/RetroShare/OpenPGP-SDK.git"
	GIT_TAG "origin/master"
	GIT_SHALLOW TRUE
	GIT_PROGRESS TRUE
	TIMEOUT 10
	EXCLUDE_FROM_ALL
)
FetchContent_MakeAvailable(openpgpsdk)
set(${FETCH_PROVIDER_PACKAGE_NAME}_FOUND TRUE)

add_library(openpgpsdk::openpgpsdk ALIAS openpgpsdk)
