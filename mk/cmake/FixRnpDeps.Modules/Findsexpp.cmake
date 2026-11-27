## ------------------------------------------------------------------------- ##
 # mk/cmake/FixRnpDeps.Modules/Findsexpp.cmake
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

# 3.7 : pkg_check_modules(IMPORTED_TARGET)
# 3.11: add_library(ALIAS <IMPORTED GLOBAL>)
# 3.13: pkg_check_modules(GLOBAL)
cmake_minimum_required(VERSION 3.13...4.4)

find_package(PkgConfig)
include(FindPackageHandleStandardArgs)

if(PkgConfig_FOUND)
  # GLOBAL so the ALIAS below is legal before 3.18.
	pkg_check_modules(sexpp IMPORTED_TARGET GLOBAL sexpp)
endif()

find_package_handle_standard_args(sexpp
	REQUIRED_VARS sexpp_FOUND
	VERSION_VAR sexpp_VERSION
)

if(sexpp_FOUND AND NOT TARGET sexpp::sexpp)
	add_library(sexpp::sexpp ALIAS PkgConfig::sexpp)
endif()
