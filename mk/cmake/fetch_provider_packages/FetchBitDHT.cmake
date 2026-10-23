## ------------------------------------------------------------------------- ##
 # mk/cmake/fetch_provider_packages/FetchBitDHT.cmake
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
# 3.14: FetchContent_MakeAvailable()
cmake_minimum_required(VERSION 3.14...4.4)

set(FETCH_BitDHT_GIT_TAG "" CACHE STRING
	"git tag to use for fetching BitDHT"
)
mark_as_advanced(FETCH_BitDHT_GIT_TAG)
if(NOT FETCH_BitDHT_GIT_TAG)
	# TODO: track official BitDHT master once #3 is merged
	set(FETCH_BitDHT_GIT_TAG
		52df60f390d4cd4c1a2d5e25563396255aae6399
	)
endif()

include(FetchContent)
FetchContent_Declare(BitDHT
	GIT_REPOSITORY "https://github.com/dbear496/RetroShare_BitDHT.git"
	GIT_TAG ${FETCH_BitDHT_GIT_TAG}
	GIT_PROGRESS TRUE
	EXCLUDE_FROM_ALL
)
FetchContent_MakeAvailable(BitDHT)
set(BitDHT_FOUND TRUE)

set(BD_BOOT_FILE "${bitdht_SOURCE_DIR}/src/bitdht/bdboot.txt")
