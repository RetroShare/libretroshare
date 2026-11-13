## ------------------------------------------------------------------------- ##
 # tests/consumer/CMakeLists.txt
 # This file is part of libRetroShare.
 #
 # Copyright (C) 2026      Olivier Marty <omarty@neurontech.fr>
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

project(libretroshare-consumer-test LANGUAGES CXX)

find_package(RetroShare REQUIRED)

add_executable(rs-consumer main.cc)
target_link_libraries(rs-consumer PRIVATE retroshare::retroshare)
