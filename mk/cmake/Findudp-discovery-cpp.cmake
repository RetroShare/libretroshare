## ---------------------------------------------------------------------- ##
 # mk/cmake/Findudp-discovery-cpp.cmake
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

find_library(udp-discovery_LIBRARY NAMES udp-discovery)
find_path(udp-discovery_INCLUDE NAMES udp_discovery_peer.hpp)
find_program(udp-discovery_TOOL NAMES udp-discovery-tool)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(udp-discovery-cpp
  REQUIRED_VARS udp-discovery_LIBRARY udp-discovery_INCLUDE
)

if(udp-discovery-cpp_FOUND AND NOT TARGET udp-discovery-cpp::udp-discovery)
  add_library(udp-discovery-cpp::udp-discovery STATIC IMPORTED)
  set_target_properties(udp-discovery-cpp::udp-discovery PROPERTIES
    IMPORTED_LOCATION "${udp-discovery_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${udp-discovery_INCLUDE}"
  )
endif()

if(udp-discovery_TOOL AND NOT TARGET udp-discovery-tool)
  add_executable(udp-discovery-cpp::udp-discovery-tool IMPORTED)
  set_target_properties(udp-discovery-cpp::udp-discovery-tool PROPERTIES
    IMPORTED_LOCATION "${udp-discovery_TOOL}"
  )
endif()
