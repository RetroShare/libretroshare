## ------------------------------------------------------------------------- ##
 # mk/cmake/FixRnpDeps.cmake
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

# 3.12: list(JOIN)
cmake_minimum_required(VERSION 3.12...4.4)

if(NOT TARGET rnp::librnp)
	message(WARNING
		"rnp::librnp target must be defined before its dependencies can be fixed."
	)
	return()
endif()

set(rnp_dep_package_Botan::Botan Botan 2.14.0)
set(rnp_dep_package_BZip2::BZip2 BZip2)
set(rnp_dep_package_JSON-C::JSON-C JSON-C 0.11)
set(rnp_dep_package_OpenSSL::Crypto OpenSSL 1.1.1)
set(rnp_dep_package_rnp::sexpp sexpp 0.8.7)
set(rnp_dep_package_ZLIB::ZLIB ZLIB)

get_target_property(rnp_INTERFACE_LINK_LIBRARIES
	rnp::librnp INTERFACE_LINK_LIBRARIES
)
set(rnp_missing_deps)
foreach(rnp_dep_target
	Botan::Botan
	BZip2::BZip2
	JSON-C::JSON-C
	OpenSSL::Crypto
	rnp::sexpp
	ZLIB::ZLIB
)
	if(
		"${rnp_INTERFACE_LINK_LIBRARIES}" MATCHES "${rnp_dep_target}" AND
		NOT TARGET ${rnp_dep_target}
	)
		list(APPEND rnp_missing_deps ${rnp_dep_target})
	endif()
endforeach()

if(rnp_missing_deps)
	list(JOIN rnp_missing_deps ", " rnp_missing_deps_str)
	message(WARNING
		"The rnp CMake config is unusable because it declares the following "
		"target dependencies that nothing defines: ${rnp_missing_deps_str}. "
		"RetroShare will attempt to work around the issue by looking for packages "
		"that might provide the missing targets.\n"
		"If rnp is from your distribution's package repository, please file a bug "
		"report with your distribution so the fix can be backported from rnp "
		"commit 1a597a67.\n"
	)

	list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_LIST_DIR}/FixRnpDeps.Modules)
	foreach(rnp_missing_target ${rnp_missing_deps})
		find_package(${rnp_dep_package_${rnp_missing_target}})
	endforeach()
endif()
