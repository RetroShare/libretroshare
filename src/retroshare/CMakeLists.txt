## ------------------------------------------------------------------------- ##
 # src/retroshare/CMakeLists.txt
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

cmake_minimum_required(VERSION 3.0...4.4)

function(cdefine macro)
  cmake_parse_arguments(PARSE_ARGN cdefine
    "VALID_0;STRING"
    "DEFAULT"
    ""
  )
  set(value ${cdefine_UNPARSED_ARGUMENTS})
  if(STRING)
    set(q "\"")
  else()
    set(q )
  endif()
  if(value OR VALID_0 AND value EQUAL 0)
    set(DEFINE_${macro} "#define ${macro} ${q}${value}${q}")
  elseif(DEFINED DEFAULT)
    set(DEFINE_${macro} "#define ${macro} ${q}${DEFAULT}${q}")
  else()
    set(DEFINE_${macro} "/* #undef ${macro} ${q}${value}${q} */")
  endif()
  set(DEFINE_${macro} ${DEFINE_${macro}} PARENT_SCOPE)
endfunction()

set(default DEFAULT "version not available")
string(REGEX REPLACE "^\\*" "" public ${PROJECT_VERSION})

set(numeric 0)
set(numeric2 0)
foreach(comp MAJOR MINOR PATCH TWEAK)
  if(PROJECT_VERSION_${comp})
    math(EXPR numeric  "${numeric}  + ${PROJECT_VERSION_${comp}}")
    math(EXPR numeric2 "${numeric2} + ${PROJECT_VERSION_${comp}}")
  endif()
  math(EXPR numeric  "${numeric} * 100")
  math(EXPR numeric2 "${numeric2} << 8")
endforeach()


cdefine(RS_VERSION ${PROJECT_VERSION} STRING ${default})
cdefine(RS_VERSION_FULL ${PROJECT_VERSION_FULL} STRING ${default})
cdefine(RS_VERSION_PUBLIC ${public} STRING ${default})
cdefine(RS_VERSION_MAJOR ${PROJECT_VERSION_MAJOR} DEFAULT 0)
cdefine(RS_VERSION_MINOR ${PROJECT_VERSION_MINOR} DEFAULT 0)
cdefine(RS_VERSION_PATCH ${PROJECT_VERSION_PATCH} DEFAULT 0)
cdefine(RS_VERSION_TWEAK ${PROJECT_VERSION_TWEAK})
cdefine(RS_VERSION_NUMERIC ${numeric} VALID_0)

configure_file(rsversion.h.in rsversion-cmake.h)

target_sources(${PROJECT_NAME}
  ${PUBLIC_FILESET_HEADERS_FILES}
  ${CMAKE_CURRENT_SOURCE_DIR}/rsversion.h
  ${CMAKE_CURRENT_BINARY_DIR}/rsversion-cmake.h
)
