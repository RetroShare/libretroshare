# ------------------------------------------------------------------------ *\
# mk/cmake/fetch_provider_packages/Fetchrestbed-extra.cmake
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
find_package(Git REQUIRED)
find_package(Patch REQUIRED)

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
		"${CMAKE_CURRENT_LIST_DIR}/restbed.patch"
)
FetchContent_MakeAvailable(${FETCH_PROVIDER_PACKAGE_NAME})
set(${FETCH_PROVIDER_PACKAGE_NAME}_FOUND TRUE)

if(BUILD_SHARED_LIBS)
  set(RESTBED_TARGETS restbed-shared restbed-static)
else()
  set(RESTBED_TARGETS restbed-static restbed-shared)
endif()

foreach(RESTBED_TARGET ${RESTBED_TARGETS})
  if(TARGET ${RESTBED_TARGET})
    if(NOT TARGET restbed::${RESTBED_TARGET})
      add_library(restbed::${RESTBED_TARGET} ALIAS ${RESTBED_TARGET})
      if(WIN32)
        target_link_libraries(${RESTBED_TARGET} PRIVATE ws2_32 wsock32)
      endif()
    endif()
    if(NOT TARGET restbed::restbed)
      add_library(restbed::restbed ALIAS ${RESTBED_TARGET})
    endif()
  endif()
endforeach()
