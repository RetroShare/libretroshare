# RetroShare decentralized communication platform
#
# Copyright (C) 2021-2026  Gioacchino Mazzurco <gio@retroshare.cc>
# Copyright (C) 2026       David Bears <dbear4q@gmail.com>
#
# SPDX-License-Identifier: CC0-1.0

cmake_minimum_required(VERSION 3.24...4.4.1)
project(retroshare
	VERSION 0.6.7
	LANGUAGES CXX C
)


################################################################################
### options

include(CMakeDependentOption)

option(
	RS_SQLCIPHER
	"SQLCipher encryption for GXS database"
	ON )

option(
	RS_GXS_SEND_ALL
	"GXS distribute all available messages on request, indipendently from \
	local sync timer"
	ON )

option(
	RS_USE_CALENDAR
	"Build RetroShare with GXS Calendar service support"
	ON )

option(
	RS_BITDHT
	"Use bitdht (BitTorrent DHT own implementation) to look for online peers"
	ON )

cmake_dependent_option(
	RS_BITDHT_STUNNER
	"Use bitdht (BitTorrent DHT own implementation) for NAT type discovery and \
	attempt the STUN (Session Traversal Utilities for NAT)"
	ON
	"RS_BITDHT"
	OFF )

cmake_dependent_option(
	RS_BITDHT_STUNNER_EXT_IP
	"Use bitdht (BitTorrent DHT own implementation) stunner to figure out our \
	external IP. As this purely relying on random DHT peers that answer our \
	request, it can easily be abused. Therefore, it is turned off by default."
	OFF
	"RS_BITDHT_STUNNER"
	OFF )

option(
	RS_RNPLIB
	"Enable use RNP lib for PGP"
	ON )

option(
	RS_JSON_API
	"Use restbed to expose libretroshare as JSON API via HTTP"
	OFF )

option(
	RS_FORUM_DEEP_INDEX
	"Xapian based full text index and search of GXS forums"
	OFF )

option(
	RS_BRODCAST_DISCOVERY
	"Local area network peer discovery via udp-discovery-cpp"
	ON )

option(
	RS_DH_PRIME_INIT_CHECK
	"Check Diffie Hellman prime at each startup. This is not necessary and on \
	all Android mobile phones tested this take at least one minute at startup \
	which is untolerable for most phone users."
	ON )

option(
	RS_MINIUPNPC
	"Forward ports in NAT router via miniupnpc"
	ON )

cmake_dependent_option(
	RS_LIBUPNP
	"Forward ports in NAT router via libupnp (unstable)"
	OFF
	"NOT RS_MINIUPNPC"
	OFF )

option(
	RS_LIBRETROSHARE_STATIC
	"Build RetroShare static library"
	ON )

cmake_dependent_option(
	RS_LIBRETROSHARE_SHARED
	"Build RetroShare shared library"
	OFF
	"NOT RS_LIBRETROSHARE_STATIC"
	OFF )

option(
	RS_WARN_DEPRECATED
	"Print warning about RetroShare deprecated components usage during build"
	ON )

option(
	RS_WARN_LESS
	"Silence a few at the moment very common warnings about RetroShare \
	components during build"
	OFF )

option(
	RS_V07_BREAKING_CHANGES
	"Enable retro-compatibility breaking changes planned for RetroShare 0.7.0"
	OFF )

option(
	RS_GXSWIKIPOS
	"Build the experimental, off-by-default GXS Wiki (WikiPoos) service"
	OFF )

option(
	RS_GXSTHEWIRE
	"Build the experimental, off-by-default GXS 'The Wire' service"
	OFF )

include(GNUInstallDirs)

set(
	RS_DATA_DIR
	"${CMAKE_INSTALL_DATADIR}/${PROJECT_NAME}"
	CACHE PATH
	"Path where to install RetroShare system wide data" )

set(
	RS_INCLUDE_INSTALL_DIR
	"${CMAKE_INSTALL_INCLUDEDIR}/${PROJECT_NAME}"
	CACHE PATH
	"Path where to install libretroshare headers" )

set(
	RS_LIB_INSTALL_DIR
	"${CMAKE_INSTALL_LIBDIR}"
	CACHE PATH
	"Path where to install libretroshare compiled library" )

option(
	RS_EXPORT_JNI_ONLOAD
	"Export libretroshare JNI_OnLoad. See src/rs_android/rsjni.cpp for details"
	ON )

option(
	RS_DEVELOPMENT_BUILD
	"Disable optimization to speed up build, enable verbose build log. \
	 just for development purposes, not suitable for library usage"
	OFF )

option(
	RS_ANDROID
	"Enable when compiling libretroshare for Android"
	OFF )

cmake_dependent_option(
	RS_WEBUI
	"Enable RetroShare Web UI support"
	OFF
	"RS_JSON_API"
	OFF )

# Enable DWARF split debugging symbols generation also in Release mode this
# doesn't make the library fat as the debugging symbols are stored
# separately, enable us to symbolicate crash reports produced by release builds
# @see https://www.productive-cpp.com/improving-cpp-builds-with-split-dwarf/
# https://web.archive.org/web/20231127210015/https://www.productive-cpp.com/improving-cpp-builds-with-split-dwarf/
option(
	RS_SPLIT_DEBUG
	"Generate split debug symbols, useful for debugabble release builds"
	OFF )

option(
	RS_CPPTRACE_STACKTRACE
	"Use Jeremy Rifkin Cpptrace library to print stacktrace instead of \
	our implementation"
	OFF )

set(
	RS_EXTRA_VERSION
	""
	CACHE STRING
	"Specify an extra string to append to RetroShare version to make it more descriptive" )

################################################################################
### dependencies

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/mk/cmake")
set(CMAKE_FIND_PACKAGE_PREFER_CONFIG TRUE)
set(FETCHCONTENT_QUIET OFF)
include(FetchContent)

find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(BZip2 REQUIRED)
find_package(ZLIB REQUIRED)

################################################################################
### build configuration

if(RS_DEVELOPMENT_BUILD)
	set(CMAKE_VERBOSE_MAKEFILE ON)
	set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
endif(RS_DEVELOPMENT_BUILD)


################################################################################
### version

include(GetGitRevisionDescription)
set(LIBRS_MAJOR_VERSION "${PROJECT_VERSION_MAJOR}")
set(LIBRS_MINOR_VERSION "${PROJECT_VERSION_MINOR}")
set(LIBRS_MINI_VERSION "${PROJECT_VERSION_PATCH}")
if(DEFINED RS_EXTRA_VERSION AND NOT (RS_EXTRA_VERSION STREQUAL ""))
	set(LIBRS_EXTRA_VERSION "${RS_EXTRA_VERSION}")
else()
	git_describe_working_tree(V_GIT_DESCRIBE
		--tags --long --always --dirty --match v*.*.*
	)
	set(V_GIT_DESCRIBE_REGEXP "^v([0-9]+).([0-9]+).([0-9]+)(.*)$")
	if(NOT V_GIT_DESCRIBE)
		set(LIBRS_EXTRA_VERSION "-unknown")
	elseif(V_GIT_DESCRIBE MATCHES "${V_GIT_DESCRIBE_REGEXP}")
		string(
			REGEX REPLACE
			"${V_GIT_DESCRIBE_REGEXP}" "\\1;\\2;\\3;\\4"
			V_GIT_DESCRIBE_LIST "${V_GIT_DESCRIBE}" )
		list(GET V_GIT_DESCRIBE_LIST 0 LIBRS_MAJOR_VERSION)
		list(GET V_GIT_DESCRIBE_LIST 1 LIBRS_MINOR_VERSION)
		list(GET V_GIT_DESCRIBE_LIST 2 LIBRS_MINI_VERSION)
		list(GET V_GIT_DESCRIBE_LIST 3 LIBRS_EXTRA_VERSION)
	else()
		set(LIBRS_EXTRA_VERSION "-${V_GIT_DESCRIBE}")
	endif()
endif()
string(CONCAT LIBRS_FULL_VERSION
	${LIBRS_MAJOR_VERSION}
	.${LIBRS_MINOR_VERSION}
	.${LIBRS_MINI_VERSION}
	"${LIBRS_EXTRA_VERSION}"
)
message(STATUS "libretroshare version ${LIBRS_FULL_VERSION}")


################################################################################
### general compilation options

if(RS_LIBRETROSHARE_STATIC AND RS_LIBRETROSHARE_SHARED)
	message(FATAL_ERROR "building both static and shared library is not supported")
elseif(RS_LIBRETROSHARE_STATIC)
	set(BUILD_SHARED_LIBS NO)
elseif(RS_LIBRETROSHARE_SHARED)
	set(BUILD_SHARED_LIBS YES)
else()
	message(FATAL_ERROR "must specify either RS_LIBRETROSHARE_STATIC or RS_LIBRETROSHARE_SHARED")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(WIN32)
	# Force Unicode mappings for Windows API (fixes MoveFileEx and similar errors)
	add_definitions(-DUNICODE -D_UNICODE)
endif()

################################################################################
### targets

add_library(${PROJECT_NAME})

set_property(TARGET ${PROJECT_NAME} PROPERTY CXX_STANDARD 17)

target_link_libraries(${PROJECT_NAME}
	## TODO: I have a suspicion some of these need to be public
	PRIVATE
	${CMAKE_DL_LIBS}
	Threads::Threads
	OpenSSL::SSL
	OpenSSL::Crypto
	BZip2::BZip2
	ZLIB::ZLIB
)

target_compile_definitions(${PROJECT_NAME}
	PUBLIC
	"LIBRS_FULL_VERSION=${LIBRS_FULL_VERSION}"
	"LIBRS_MAJOR_VERSION=${LIBRS_MAJOR_VERSION}"
	"LIBRS_MINOR_VERSION=${LIBRS_MINOR_VERSION}"
	"LIBRS_MINI_VERSION=${LIBRS_MINI_VERSION}"
	"LIBRS_EXTRA_VERSION=${LIBRS_EXTRA_VERSION}"
	PRIVATE
	SQLITE_HAS_CODEC
	RS_ENABLE_GXS
	GXS_ENABLE_SYNC_MSGS
	RS_USE_GXS_DISTANT_SYNC
	RS_GXS_TRANS
	V07_NON_BACKWARD_COMPATIBLE_CHANGE_001
	V07_NON_BACKWARD_COMPATIBLE_CHANGE_002
	V07_NON_BACKWARD_COMPATIBLE_CHANGE_003
)

if(RS_DEVELOPMENT_BUILD)
	target_compile_options(${PROJECT_NAME} PRIVATE "-O0")
endif(RS_DEVELOPMENT_BUILD)

if(RS_SPLIT_DEBUG)
	target_compile_options(${PROJECT_NAME} PRIVATE "-g")
	target_compile_options(${PROJECT_NAME} PRIVATE "-gsplit-dwarf")
endif(RS_SPLIT_DEBUG)

if(CMAKE_BUILD_TYPE STREQUAL "Release" AND NOT RS_NO_LTO)
	set_property(TARGET ${PROJECT_NAME} PROPERTY INTERPROCEDURAL_OPTIMIZATION ON)
endif()

if(RS_USE_CALENDAR)
	target_compile_definitions(${PROJECT_NAME} PUBLIC RS_USE_CALENDAR)
endif(RS_USE_CALENDAR)

if(RS_GXSWIKIPOS)
	target_compile_definitions(${PROJECT_NAME} PUBLIC RS_USE_WIKI)
endif(RS_GXSWIKIPOS)

if(RS_GXSTHEWIRE)
	target_compile_definitions(${PROJECT_NAME} PUBLIC RS_USE_WIRE)
endif(RS_GXSTHEWIRE)

if(RS_FORUM_DEEP_INDEX)
	find_package(Xapian REQUIRED)

	target_link_libraries(${PROJECT_NAME} PRIVATE ${XAPIAN_LIBRARIES})
	target_compile_definitions(${PROJECT_NAME} PUBLIC RS_DEEP_FORUMS_INDEX)
endif(RS_FORUM_DEEP_INDEX)

if(RS_GXS_SEND_ALL)
	target_compile_definitions(
		${PROJECT_NAME} PUBLIC RS_GXS_SEND_ALL )
endif(RS_GXS_SEND_ALL)

if(RS_V07_BREAKING_CHANGES)
	target_compile_definitions(${PROJECT_NAME} PRIVATE
		V07_NON_BACKWARD_COMPATIBLE_CHANGE_004
		V07_NON_BACKWARD_COMPATIBLE_CHANGE_UNNAMED
	)
endif()

if(NOT RS_DH_PRIME_INIT_CHECK)
	target_compile_definitions(${PROJECT_NAME} PRIVATE
		RS_DISABLE_DIFFIE_HELLMAN_INIT_CHECK
	)
endif(NOT RS_DH_PRIME_INIT_CHECK)

if(NOT RS_WARN_DEPRECATED)
	target_compile_definitions(${PROJECT_NAME} PRIVATE RS_NO_WARN_DEPRECATED)
	target_compile_options(${PROJECT_NAME} PRIVATE
		-Wno-deprecated
		-Wno-deprecated-declarations
	)
endif(NOT RS_WARN_DEPRECATED)

if(RS_WARN_LESS)
	target_compile_definitions(${PROJECT_NAME} PRIVATE RS_NO_WARN_CPP)

	target_compile_options(${PROJECT_NAME} PRIVATE
		-Wno-cpp
		-Wno-inconsistent-missing-override
	)
endif(RS_WARN_LESS)

if(RS_WEBUI)
	target_compile_definitions(${PROJECT_NAME} PUBLIC RS_WEBUI)
endif(RS_WEBUI)

if(RS_EXPORT_JNI_ONLOAD)
	target_compile_definitions(${PROJECT_NAME} PUBLIC
		RS_LIBRETROSHARE_EXPORT_JNI_ONLOAD
	)
endif(RS_EXPORT_JNI_ONLOAD)


################################################################################
### libsam3

if(RS_USE_I2P_SAM3)
	find_package(libsam3 REQUIRED)
	target_link_libraries(${PROJECT_NAME}
		PRIVATE
		libsam3::libsam3
	)
	target_compile_definitions(${PROJECT_NAME}
		PRIVATE
		RS_USE_I2P_SAM3
		RS_USE_I2P_SAM3_LIBSAM3
	)
endif()

################################################################################
### rnp / openpgpsdk

if(RS_RNPLIB)
	find_package(rnp REQUIRED)

	target_link_libraries(${PROJECT_NAME} PUBLIC rnp::librnp)
else()
	find_package(openpgpsdk REQUIRED)

	target_link_libraries(${PROJECT_NAME} PUBLIC openpgpsdk::openpgpsdk)

	target_compile_definitions(${PROJECT_NAME} PUBLIC USE_OPENPGPSDK)
endif(RS_RNPLIB)

################################################################################
### bitdht

if(RS_BITDHT)
	find_package(bitdht REQUIRED)

	target_compile_definitions(${PROJECT_NAME} PUBLIC RS_USE_BITDHT)
	target_link_libraries(${PROJECT_NAME} PRIVATE bitdht::bitdht)

	install(FILES "${BD_BOOT_FILE}" DESTINATION "${RS_DATA_DIR}")

	if(RS_BITDHT_STUNNER)
		target_compile_definitions(${PROJECT_NAME} PUBLIC RS_USE_DHT_STUNNER)

		if(RS_BITDHT_STUNNER_EXT_IP)
			# TODO: Refactor this define to use proper prefix and then flag as public
			target_compile_definitions(${PROJECT_NAME} PRIVATE ALLOW_DHT_STUNNER)
		endif(RS_BITDHT_STUNNER_EXT_IP)
	endif(RS_BITDHT_STUNNER)
endif(RS_BITDHT)

################################################################################
### RapidJSON

find_package(RapidJSON REQUIRED)
target_link_libraries(${PROJECT_NAME} PUBLIC RapidJSON)

################################################################################
### SQLite3 / SQLCipher

## TODO: Check if https://github.com/rbock/sqlpp11 or
## https://github.com/rbock/sqlpp17 may improve GXS code
if(RS_SQLCIPHER)
	find_package(SQLCipher REQUIRED)
	target_link_libraries(${PROJECT_NAME} PRIVATE SQLCipher::SQLCipher)
else()
	# TODO: Refactor this define to use proper prefix and then flag as
	# public
	target_compile_definitions(${PROJECT_NAME} PRIVATE NO_SQLCIPHER)
	find_package(SQLite3 REQUIRED)
	target_link_libraries(${PROJECT_NAME} PRIVATE SQLite::SQLite3)
endif()

################################################################################
### UPnP

if(RS_MINIUPNPC)
	find_package(MiniUPnPc REQUIRED)
	target_compile_definitions(${PROJECT_NAME} PUBLIC RS_USE_LIBMINIUPNPC)
	target_link_libraries(${PROJECT_NAME} PRIVATE MiniUPnPc::MiniUPnPc)
endif(RS_MINIUPNPC)

if(RS_LIBUPNP)
	message(FATAL_ERROR "UPnP support via libupnp is currently not supported")
	#target_compile_definitions(${PROJECT_NAME} PUBLIC RS_USE_LIBUPNP)
endif(RS_LIBUPNP)

################################################################################
### UDP-Discovery

if(RS_BRODCAST_DISCOVERY)
	find_package(udp-discovery-cpp REQUIRED)
	target_link_libraries(${PROJECT_NAME} PRIVATE
		udp-discovery-cpp::udp-discovery
	)
	target_compile_definitions(${PROJECT_NAME} PUBLIC RS_BROADCAST_DISCOVERY)
endif(RS_BRODCAST_DISCOVERY)

################################################################################
### cpptrace

if(RS_CPPTRACE_STACKTRACE)
	find_package(cpptrace REQUIRED)
	target_link_libraries(${PROJECT_NAME} PRIVATE cpptrace::cpptrace)
	target_compile_definitions(${PROJECT_NAME} PRIVATE RS_JEREMY_RIFKIN_CPPTRACE)
endif(RS_CPPTRACE_STACKTRACE)

################################################################################
### Platforms

### Linux
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
	target_compile_definitions(${PROJECT_NAME} PUBLIC
		"RS_DATA_DIR=\"${CMAKE_INSTALL_PREFIX}/${RS_DATA_DIR}\""
	)
endif(CMAKE_SYSTEM_NAME STREQUAL "Linux")

### Windows
if(WIN32)
	target_compile_definitions(
		${PROJECT_NAME} PUBLIC
		WINDOWS_SYS
		# Forces the Windows API to use wide-char variants (e.g. MoveFileExW)
		UNICODE
		_UNICODE
		# Prevents <windows.h> from pulling in <wingdi.h> which #defines ERROR
		NOGDI
		# Prevents <windows.h> from defining min/max macros that break std::min/max
		NOMINMAX
		# Prevents <windows.h> from pulling in <wincrypt.h> which shadows OpenSSL X509 types
		NOCRYPT
		# Prevents <windows.h> from including extra headers and polluting the namespace
		WIN32_LEAN_AND_MEAN
		WIN_DLL_EXPORT
	)
	target_link_libraries(${PROJECT_NAME} PUBLIC ws2_32 winmm iphlpapi)

	if(BUILD_SHARED_LIBS)
		set_target_properties(${PROJECT_NAME} PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
		if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
			# We must use an explicit list of heavy 3rd-party archives instead of "ALL" for --exclude-libs.
			# Using "ALL" also hides symbols from CMake's internal objects.a archive (created by WINDOWS_EXPORT_ALL_SYMBOLS),
			# which prevents libretroshare from exporting its own core C++ symbols, causing massive linkage errors downstream.
			target_link_options(${PROJECT_NAME} PRIVATE "-Wl,--export-all-symbols,--exclude-libs,libbitdht.a:librnp.a:libsexpp.a:libudp-discovery.a:librestbed.a:libsqlcipher.a:libbotan-2.a:libbotan-3.a:libz.a:libbz2.a")
		endif()
	endif()

	# Disabling LTO (Interprocedural Optimization) on Windows is necessary for RetroShare
	# because enabling it causes the MinGW linker (ld) to run out of memory or exhaust the maximum
	# number of exported symbols, or emit "multiple definition" errors during LTO code generation.
	# While this makes the binary fatter and slower, it prevents fatal build failures.
	set_property(TARGET ${PROJECT_NAME} PROPERTY INTERPROCEDURAL_OPTIMIZATION OFF)
endif()

### Mac
if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
	target_link_libraries(${PROJECT_NAME} PRIVATE "-framework Cocoa")
endif()

### Android
if(RS_ANDROID)
	target_link_libraries(${PROJECT_NAME} PRIVATE log)
	if(BUILD_SHARED_LIBS)
		target_link_options(${PROJECT_NAME} PRIVATE LINKER:--no-undefined)
	endif()

	## On Android all symbols except for the few ones used called via JNI can be
	## kept hidden allowing for more optimization, and reduced binary size
	# TODO: Use generator expressions instead of CMAKE_BUILD_TYPE see
	# https://cmake.org/cmake/help/latest/manual/cmake-buildsystem.7.html#build-configurations
	# https://cmake.org/cmake/help/latest/manual/cmake-generator-expressions.7.html#manual:cmake-generator-expressions(7)
	if(CMAKE_BUILD_TYPE STREQUAL "Release")
		set_property(TARGET ${PROJECT_NAME} PROPERTY C_VISIBILITY_PRESET hidden)
		set_property(TARGET ${PROJECT_NAME} PROPERTY CXX_VISIBILITY_PRESET hidden)
		set_property(TARGET ${PROJECT_NAME} PROPERTY VISIBILITY_INLINES_HIDDEN ON)
	endif()

	find_package(jni.hpp REQUIRED)
	target_link_libraries(${PROJECT_NAME} PRIVATE jni.hpp::jni.hpp)
endif()

################################################################################
### subdirectories

add_subdirectory(src)

################################################################################
### install

install(
	TARGETS ${PROJECT_NAME}
	COMPONENT libretroshare
	EXCLUDE_FROM_ALL
	LIBRARY DESTINATION "${RS_LIB_INSTALL_DIR}"
	FILE_SET HEADERS DESTINATION "${RS_INCLUDE_INSTALL_DIR}"
)

## TODO: export targets and create config packages for install and build trees
