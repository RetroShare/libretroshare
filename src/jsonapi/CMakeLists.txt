# ------------------------------------------------------------------------ *\
# src/jsonapi/CMakeLists.txt
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

find_package(Doxygen REQUIRED)
find_package(Python3 REQUIRED)

set(
  JSONAPI_GENERATOR_EXECUTABLE
  "${CMAKE_CURRENT_SOURCE_DIR}/jsonapi-generator.py"
)

get_target_property(RS_PUBLIC_HEADERS ${PROJECT_NAME} HEADER_SET)

configure_file(
  "jsonapi-generator-doxygen.conf.in"
  "jsonapi-generator-doxygen.conf"
  @ONLY
)

add_custom_command(
  OUTPUT "jsonapi-includes.inl" "jsonapi-wrappers.inl"
  COMMAND ${DOXYGEN_EXECUTABLE} "jsonapi-generator-doxygen.conf"
  COMMAND ${Python3_EXECUTABLE} ${JSONAPI_GENERATOR_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR}
  MAIN_DEPENDENCY "${JSONAPI_GENERATOR_EXECUTABLE}"
  DEPENDS "jsonapi-generator-doxygen.conf" ${RS_PUBLIC_HEADERS}
)
add_custom_target(jsonapi_headers SOURCES
  "${CMAKE_CURRENT_BINARY_DIR}/jsonapi-includes.inl"
  "${CMAKE_CURRENT_BINARY_DIR}/jsonapi-wrappers.inl"
)
add_dependencies(${PROJECT_NAME} jsonapi_headers)

target_sources(${PROJECT_NAME}
  PRIVATE FILE_SET private_HEADERS TYPE HEADERS
  FILES
  "${CMAKE_CURRENT_BINARY_DIR}/jsonapi-includes.inl"
  "${CMAKE_CURRENT_BINARY_DIR}/jsonapi-wrappers.inl"
)

target_include_directories(${PROJECT_NAME}
  PRIVATE
  "${JSONAPI_GENERATOR_OUTPUT_DIR}"
)

find_package(restbed REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE restbed::restbed)

target_compile_definitions(${PROJECT_NAME} PUBLIC RS_JSONAPI)
