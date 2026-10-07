# ------------------------------------------------------------------------ *\
# mk/cmake/fetch_provider_packages/Fetchudp-discovery-cpp.cmake
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

cmake_minimum_required(VERSION 3.24...4.4)

set(BUILD_TEST FALSE CACHE BOOL "build udp-discovery-cpp tests")

include(FetchContent)
FetchContent_Declare(udp-discovery-cpp
	GIT_REPOSITORY "https://github.com/truvorskameikin/udp-discovery-cpp.git"
	GIT_TAG "origin/master"
	GIT_SHALLOW TRUE
	GIT_PROGRESS TRUE
	TIMEOUT 10
	PATCH_COMMAND sed -i -e "s/^cmake_minimum_required(VERSION 3.0)\$/cmake_minimum_required(VERSION 3.0...4.4)/" CMakeLists.txt
)
FetchContent_MakeAvailable(${FETCH_PROVIDER_PACKAGE_NAME})
set(${FETCH_PROVIDER_PACKAGE_NAME}_FOUND TRUE)

add_library(udp-discovery-cpp::udp-discovery ALIAS udp-discovery)

target_include_directories(udp-discovery PUBLIC
	"${udp-discovery-cpp_SOURCE_DIR}"
)


if(WIN32)
	# Legacy C submodules trip strict GCC >= 15 diagnostics that are now errors by
	# default; downgrade them to warnings for now.
	target_compile_options(udp-discovery PRIVATE
	  -Wno-error=incompatible-pointer-types
	  -Wno-error=int-conversion
	  -Wno-error=implicit-function-declaration
	)
endif()
