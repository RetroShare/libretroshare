# ------------------------------------------------------------------------ *\
# mk/cmake/fetchcontent_provider_declare.cmake
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

list(APPEND CMAKE_MODULE_PATH
	"${PROJECT_SOURCE_DIR}/mk/cmake/fetch_provider_packages"
)
include(FetchContent)
include(ExternalProject)
find_package(Patch REQUIRED)
find_package(Git REQUIRED)

if(CMAKE_VERSION VERSION_GREATER_EQUAL 4.2)
	function(EnvironmentModification)
	endfunction()
elseif(CMAKE_VERSION VERSION_GREATER_EQUAL 3.25)
	function(EnvironmentModification outvar)
		list(TRANSFORM ARGN PREPEND --modify\;)
		set(${outvar} ${CMAKE_COMMAND} -E env ${ARGN} -- PARENT_SCOPE)
	endfunction(EnvironmentModification)
else()
	function(EnvironmentModification outvar)
		list(TRANSFORM ARGN REPLACE "^([^=]+)=set:(.*)\$" "\\1=\\2")
		list(TRANSFORM ARGN REPLACE "^([^=]+)=unset:(.*)\$" "--unset=\\1")
		set(${outvar} ${CMAKE_COMMAND} -E env ${ARGN} -- PARENT_SCOPE)
	endfunction(EnvironmentModification)
endif()

################################################################################
### asio

FetchContent_Declare(asio
	GIT_REPOSITORY "https://github.com/chriskohlhoff/asio.git"
	GIT_TAG "origin/master"
	GIT_SHALLOW TRUE
	GIT_PROGRESS TRUE
	TIMEOUT 10
	EXCLUDE_FROM_ALL
)

################################################################################
### BitDHT

FetchContent_Declare(BitDHT
	GIT_REPOSITORY "https://github.com/dbear496/RetroShare_BitDHT.git"
	GIT_TAG af783b6b37b1f140f97e066e980182ab5af60772
	# GIT_TAG "origin/cmake-refactor"
	# GIT_SHALLOW TRUE
	GIT_PROGRESS TRUE
	TIMEOUT 10
	EXCLUDE_FROM_ALL
)

################################################################################
### cpptrace

FetchContent_Declare(cpptrace
	GIT_REPOSITORY https://github.com/jeremy-rifkin/cpptrace.git
	GIT_TAG v0.3.1
	GIT_SHALLOW TRUE
	GIT_PROGRESS TRUE
	TIMEOUT 10
)

################################################################################
### jni.hpp

FetchContent_Declare(jni.hpp
	GIT_REPOSITORY "https://github.com/RetroShare/jni.hpp.git"
	GIT_TAG "origin/master"
	GIT_SHALLOW TRUE
	GIT_PROGRESS TRUE
	TIMEOUT 10
)

################################################################################
### openpgpsdk

FetchContent_Declare(openpgpsdk
	GIT_REPOSITORY "https://github.com/RetroShare/OpenPGP-SDK.git"
	GIT_TAG "origin/master"
	GIT_SHALLOW TRUE
	GIT_PROGRESS TRUE
	TIMEOUT 10
	EXCLUDE_FROM_ALL
)

################################################################################
### rapidjson

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

################################################################################
### restbed

set(BUILD_TESTS OFF CACHE BOOL "build restbed tests")
set(BUILD_SSL OFF CACHE BOOL "enable restbed SSL support")
FetchContent_Declare(restbed
	GIT_REPOSITORY "https://github.com/Corvusoft/restbed.git"
	GIT_TAG 6001a322809b5005b8bcccdf593fdda6f0173691
	# GIT_SUBMODULES dependency/asio dependency/catch
	# GIT_SHALLOW TRUE
	GIT_PROGRESS TRUE
	TIMEOUT 10
	PATCH_COMMAND ${GIT_EXECUTABLE} reset --hard
	COMMAND ${Patch_EXECUTABLE} -tNp1 -i
		"${PROJECT_SOURCE_DIR}/mk/cmake/fetch_provider_packages/restbed.patch"
)

################################################################################
### rnp

FetchContent_Declare(
	rnp
	GIT_REPOSITORY "https://github.com/rnpgp/rnp.git"
	GIT_TAG "origin/main"
	GIT_SHALLOW TRUE
	GIT_PROGRESS TRUE
	TIMEOUT 10
	EXCLUDE_FROM_ALL
)

################################################################################
### sam3

if(NOT TARGET sam3_external)
set(sam3_external_SOURCE_DIR "sam3_external-prefix/src/sam3_external")
set(sam3_external_BINARY_DIR ${sam3_external_SOURCE_DIR})
set(sam3_external_LDFLAGS "-fPIC")
if(WIN32)
  list(APPEND sam3_external_LDFLAGS
    "-lmingw32" "-lws2_32" "-lwsock32" "-mwindows"
  )
endif()
list(JOIN sam3_external_LDFLAGS " " sam3_external_LDFLAGS)
set(sam3_external_BUILD_ENVIRONMENT
  "CC=set:${CMAKE_C_COMPILER}"
	"AR=set:${CMAKE_AR}"
	"LDFLAGS=set:${sam3_external_LDFLAGS}"
)
EnvironmentModification(envmod_build ${sam3_external_BUILD_ENVIRONMENT})
ExternalProject_Add(sam3_external
	GIT_REPOSITORY "https://github.com/i2p/libsam3.git"
	GIT_TAG "origin/master"
	GIT_SHALLOW TRUE
	GIT_PROGRESS TRUE
	TIMEOUT 10
	CONFIGURE_COMMAND ""
  BUILD_COMMAND ${envmod_build} make build
	PATCH_COMMAND ${GIT_EXECUTABLE} reset --hard
	COMMAND ${Patch_EXECUTABLE} -tNp1 -i
		"${PROJECT_SOURCE_DIR}/mk/cmake/fetch_provider_packages/sam3.patch"
  INSTALL_COMMAND ""
  BUILD_IN_SOURCE TRUE
	BUILD_ALWAYS TRUE
	EXCLUDE_FROM_ALL YES
  BUILD_BYPRODUCTS "${sam3_external_BINARY_DIR}/libsam3.a"
  BUILD_ENVIRONMENT_MODIFICATION ${sam3_external_BUILD_ENVIRONMENT}
)
endif()

################################################################################
### udp-discovery

set(BUILD_TEST FALSE CACHE BOOL "build udp-discovery-cpp tests")
FetchContent_Declare(udp-discovery-cpp
	GIT_REPOSITORY "https://github.com/truvorskameikin/udp-discovery-cpp.git"
	GIT_TAG "origin/master"
	GIT_SHALLOW TRUE
	GIT_PROGRESS TRUE
	TIMEOUT 10
	PATCH_COMMAND sed -i -e "s/^cmake_minimum_required(VERSION 3.0)\$/cmake_minimum_required(VERSION 3.0...4.4)/" CMakeLists.txt
)
