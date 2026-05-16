# RetroShare decentralized communication platform
#
# Copyright (C) 2021-2022  Gioacchino Mazzurco <gio@retroshare.cc>
# Copyright (C) 2021-2022  Asociación Civil Altermundi <info@altermundi.net>
#
# SPDX-License-Identifier: CC0-1.0

set(LIB_NAME retroshare)
# Options are now managed at the root level.
# project(retroshare) is also inherited.


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
	RS_MAJOR_VERSION
	""
	CACHE STRING
	"Specify RetroShare major version" )

set(
	RS_MINOR_VERSION
	""
	CACHE STRING
	"Specify RetroShare minor version" )

set(
	RS_MINI_VERSION
	""
	CACHE STRING
	"Specify RetroShare mini version" )

set(
	RS_EXTRA_VERSION
	""
	CACHE STRING
	"Specify an extra string to append to RetroShare version to make it more descriptive" )

################################################################################

find_package(Git REQUIRED)

################################################################################

if(RS_DEVELOPMENT_BUILD)
	set(CMAKE_VERBOSE_MAKEFILE ON)
	set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
endif(RS_DEVELOPMENT_BUILD)

################################################################################

include(src/CMakeLists.txt)
list(TRANSFORM RS_SOURCES PREPEND src/)
list(TRANSFORM RS_PUBLIC_HEADERS PREPEND src/)

if(RS_LIBRETROSHARE_STATIC)
	add_library(${LIB_NAME} STATIC ${RS_SOURCES})
endif(RS_LIBRETROSHARE_STATIC)

if(RS_LIBRETROSHARE_SHARED)
	add_library(${LIB_NAME} SHARED ${RS_SOURCES})

	## Ensure statically linked libraries such as openpgpsdk are compiled with
	## PIC Which is needed for shared library
	set(CMAKE_POSITION_INDEPENDENT_CODE ON)
endif(RS_LIBRETROSHARE_SHARED)

if(RS_ANDROID)
	# As of today Android NDK libc++ doesn't support std::filesystem properly,
	# in a very confusing manner the headers have the function declarations but
	# then the shared object file doesn't have the implementation.
	# This fools the compiler which doesn't report any error but then at runtime
	# we get a crash with this error:
	# java.lang.UnsatisfiedLinkError: dlopen failed: cannot locate symbol
	# "_ZNSt6__ndk14__fs10filesystem20__create_directoriesERKNS1_4pathEPNS_10error_codeE"
	# referenced by "/data/app/==/lib/arm64/libretroshare.so
	# So set maximum supported C++ version to 14 to workaround this Android bug
	# https://github.com/android/ndk/issues/609
	set_property(TARGET ${LIB_NAME} PROPERTY CXX_STANDARD 14)
else()
	set_property(TARGET ${LIB_NAME} PROPERTY CXX_STANDARD 17)
endif()

## As of today libretroshare doesn't hide implementation details properly so it
## is necessary to flag all implementation headers as public
target_include_directories(
	${LIB_NAME}
	PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src"
	PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../supportlibs/libsam3/src/libsam3" )

set_target_properties(
	${LIB_NAME}
	PROPERTIES PUBLIC_HEADER "${RS_PUBLIC_HEADERS}" )

if(RS_LIBRETROSHARE_STANDALONE_INSTALL)
	install(
		TARGETS ${LIB_NAME}
		LIBRARY DESTINATION "${RS_LIB_INSTALL_DIR}"
		PUBLIC_HEADER DESTINATION "${RS_INCLUDE_INSTALL_DIR}" )

	## As of today libretroshare doesn't hide implementation details properly so it
	## is necessary to install private headers too
	foreach(P_HEADER ${RS_IMPLEMENTATION_HEADERS})
		get_filename_component(P_DIR "${P_HEADER}" DIRECTORY)
		install(
			FILES "${CMAKE_CURRENT_SOURCE_DIR}/src/${P_HEADER}"
			DESTINATION "${RS_INCLUDE_INSTALL_DIR}/${P_DIR}" )
	endforeach()
endif(RS_LIBRETROSHARE_STANDALONE_INSTALL)

if(RS_SPLIT_DEBUG)
	target_compile_options(${LIB_NAME} PRIVATE "-g")
	target_compile_options(${LIB_NAME} PRIVATE "-gsplit-dwarf")
endif(RS_SPLIT_DEBUG)

if(RS_DEVELOPMENT_BUILD)
	target_compile_options(${LIB_NAME} PRIVATE "-O0")
endif(RS_DEVELOPMENT_BUILD)

if(RS_ANDROID)
	target_link_libraries(${LIB_NAME} PRIVATE log)
endif(RS_ANDROID)


target_link_libraries(${LIB_NAME} PRIVATE ${CMAKE_DL_LIBS})

find_package(Threads REQUIRED)
target_link_libraries(${LIB_NAME} PRIVATE Threads::Threads)

find_package(OpenSSL REQUIRED)
target_include_directories(${LIB_NAME} PRIVATE ${OPENSSL_INCLUDE_DIR})
target_link_libraries(${LIB_NAME} PRIVATE OpenSSL::SSL OpenSSL::Crypto)
if(RS_RNPLIB)
	target_link_libraries(${LIB_NAME} PRIVATE librnp)
endif()

################################################################################

# Dependencies are managed at the root level.
target_link_libraries(${LIB_NAME} PRIVATE openpgpsdk)

if(RS_BITDHT)
    target_compile_definitions(${LIB_NAME} PUBLIC RS_USE_BITDHT)
    target_link_libraries(${LIB_NAME} PUBLIC bitdht)
    if(RS_BITDHT_STUNNER)
        target_compile_definitions(${LIB_NAME} PUBLIC RS_USE_DHT_STUNNER)
    endif()
endif()

# RapidJSON (header only)
target_include_directories(${LIB_NAME} PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/../supportlibs/rapidjson/include")

# Restbed (JSON API)
if(RS_JSON_API)
    set(RESTBED_DEVEL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../supportlibs/restbed/")
    if(EXISTS "${RESTBED_DEVEL_DIR}/CMakeLists.txt")
        # restbed is now added in the root CMakeLists.txt to avoid duplication
        target_include_directories(${LIB_NAME} PUBLIC "${RESTBED_DEVEL_DIR}/source/")
        target_link_libraries(${LIB_NAME} PUBLIC restbed-static)
        target_compile_definitions(${LIB_NAME} PUBLIC RS_JSONAPI)
    endif()
endif()


################################################################################

if(RS_JSON_API)
	find_package(Doxygen REQUIRED)
	find_package(Python3 REQUIRED)

	set(
		JSON_API_GENERATOR_WORK_DIR
		"${CMAKE_BINARY_DIR}/jsonapi-generator.workdir/" )

	set(
		JSON_API_GENERATOR_DOXYFILE
		"${JSON_API_GENERATOR_WORK_DIR}/jsonapi-generator-doxygen.conf" )

	set(
		JSONAPI_GENERATOR_OUTPUT_DIR
		"${JSON_API_GENERATOR_WORK_DIR}/src/" )

	set(
		JSONAPI_GENERATOR_SOURCE_DIR
		"${CMAKE_CURRENT_SOURCE_DIR}/src/jsonapi/" )
		
	set(
		JSONAPI_GENERATOR_EXECUTABLE
		"${JSONAPI_GENERATOR_SOURCE_DIR}/jsonapi-generator.py" )

	file(
		COPY "src/jsonapi/jsonapi-generator-doxygen.conf"
		DESTINATION "${JSON_API_GENERATOR_WORK_DIR}" )

	file(
		APPEND
		"${JSON_API_GENERATOR_DOXYFILE}"
		"OUTPUT_DIRECTORY=${JSONAPI_GENERATOR_OUTPUT_DIR}\n"
		"INPUT=${CMAKE_CURRENT_SOURCE_DIR}/src/\n"
		"EXCLUDE="
		"	${CMAKE_CURRENT_SOURCE_DIR}/src/tests"
		"	${CMAKE_CURRENT_SOURCE_DIR}/src/unused"
		"	${CMAKE_CURRENT_SOURCE_DIR}/src/unfinished\n" )

	add_custom_command(
		OUTPUT
			"${JSONAPI_GENERATOR_OUTPUT_DIR}/jsonapi-includes.inl"
			"${JSONAPI_GENERATOR_OUTPUT_DIR}/jsonapi-wrappers.inl"
		COMMAND ${DOXYGEN_EXECUTABLE} ${JSON_API_GENERATOR_DOXYFILE}
		COMMAND
			${Python3_EXECUTABLE} ${JSONAPI_GENERATOR_EXECUTABLE}
			${JSONAPI_GENERATOR_SOURCE_DIR} ${JSONAPI_GENERATOR_OUTPUT_DIR}
		MAIN_DEPENDENCY "${JSONAPI_GENERATOR_EXECUTABLE}"
		DEPENDS ${JSON_API_GENERATOR_DOXYFILE} ${RS_PUBLIC_HEADERS} )

	target_sources(
		${LIB_NAME} PRIVATE
		"${JSONAPI_GENERATOR_OUTPUT_DIR}/jsonapi-includes.inl"
		"${JSONAPI_GENERATOR_OUTPUT_DIR}/jsonapi-wrappers.inl" )

	target_include_directories(
		${LIB_NAME} PRIVATE "${JSONAPI_GENERATOR_OUTPUT_DIR}" )

	# Restbed is now handled above.


	target_compile_definitions(${LIB_NAME} PUBLIC RS_JSONAPI)
endif(RS_JSON_API)

################################################################################

if(RS_FORUM_DEEP_INDEX)
	find_package(Xapian REQUIRED)
	target_link_libraries(${LIB_NAME} PRIVATE ${XAPIAN_LIBRARIES})

	target_compile_definitions(${LIB_NAME} PUBLIC RS_DEEP_FORUMS_INDEX)
endif(RS_FORUM_DEEP_INDEX)

################################################################################

## TODO: Check if https://github.com/rbock/sqlpp11 or
## https://github.com/rbock/sqlpp17 may improve GXS code
if(RS_SQLCIPHER)
	find_library(RS_SQL_LIB "sqlcipher" REQUIRED)
	find_path(
		RS_SQL_LIB_INCLUDE "sqlcipher/sqlite3.h"
		PATH_SUFFIXES "include" "includes"
		REQUIRED )
	target_include_directories(
		${LIB_NAME}
		PRIVATE "${RS_SQL_LIB_INCLUDE}/sqlcipher" )
	target_link_libraries(${LIB_NAME} PRIVATE ${RS_SQL_LIB})
else()
	# TODO: Refactor this define to use proper prefix and then flag as
	# public
	target_compile_definitions(${LIB_NAME} PRIVATE NO_SQLCIPHER)
	find_package(SQLite3 REQUIRED)
	target_link_libraries(${LIB_NAME} PRIVATE SQLite::SQLite3)
endif()

target_compile_definitions(
	${LIB_NAME} PRIVATE
	SQLITE_HAS_CODEC
	RS_RELEASE_VERSION="${RS_VERSION}"
	RS_LIB_VERSION_HASH="${RS_LIB_VERSION}"
	RS_ENABLE_GXS
	GXS_ENABLE_SYNC_MSGS
	RS_USE_GXS_DISTANT_SYNC
	RS_GXS_TRANS
	V07_NON_BACKWARD_COMPATIBLE_CHANGE_001
	V07_NON_BACKWARD_COMPATIBLE_CHANGE_002
	V07_NON_BACKWARD_COMPATIBLE_CHANGE_003 )


if(RS_V07_BREAKING_CHANGES)
	target_compile_definitions(
		${LIB_NAME} PRIVATE
		V07_NON_BACKWARD_COMPATIBLE_CHANGE_004
		V07_NON_BACKWARD_COMPATIBLE_CHANGE_UNNAMED )
endif()

if(NOT RS_DH_PRIME_INIT_CHECK)
	target_compile_definitions(
		${LIB_NAME} PRIVATE RS_DISABLE_DIFFIE_HELLMAN_INIT_CHECK )
endif(NOT RS_DH_PRIME_INIT_CHECK)

if(RS_MINIUPNPC)
	target_compile_definitions(
		${LIB_NAME} PUBLIC RS_USE_LIBMINIUPNPC )
	target_link_libraries(${LIB_NAME} PRIVATE miniupnpc)
endif(RS_MINIUPNPC)

if(RS_LIBUPNP)
	message(FATAL_ERROR "UPnP support via libupnp is currently not supported")
	#target_compile_definitions(${LIB_NAME} PUBLIC RS_USE_LIBUPNP)
endif(RS_LIBUPNP)

if(RS_GXS_SEND_ALL)
	target_compile_definitions(
		${LIB_NAME} PUBLIC RS_GXS_SEND_ALL )
endif(RS_GXS_SEND_ALL)

if(RS_BROADCAST_DISCOVERY)
	## TODO: upstream option to disable tests building
	set(BUILD_EXAMPLE OFF CACHE BOOL "Do not build udp-discovery-cpp examples")
	set(BUILD_TOOL OFF CACHE BOOL "Do not build udp-discovery-tool application")

	set(UDP_DISCOVERY_DEVEL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../supportlibs/udp-discovery-cpp/")
	if(EXISTS "${UDP_DISCOVERY_DEVEL_DIR}/CMakeLists.txt")
		message( STATUS
		         "UDP discovery source found at ${UDP_DISCOVERY_DEVEL_DIR} using it" )
		add_subdirectory(${UDP_DISCOVERY_DEVEL_DIR} ${CMAKE_BINARY_DIR}/udp-discovery-cpp/)

		## TODO: Temporary work around target_include_directories PUBLIC should
		## be added upstream
		target_include_directories(
			${LIB_NAME} PRIVATE "${UDP_DISCOVERY_DEVEL_DIR}" )
	else()
		FetchContent_Declare(
			udp-discovery-cpp
			GIT_REPOSITORY "https://github.com/truvorskameikin/udp-discovery-cpp.git"
			GIT_TAG "origin/master"
			GIT_SHALLOW TRUE
			GIT_PROGRESS TRUE
			TIMEOUT 10
		)
		FetchContent_MakeAvailable(udp-discovery-cpp)

		## TODO: Temporary work around target_include_directories PUBLIC should
		## be added upstream
		target_include_directories(
			${LIB_NAME} PRIVATE "${udp-discovery-cpp_SOURCE_DIR}" )
	endif()

	target_link_libraries(${LIB_NAME} PRIVATE udp-discovery)

	target_compile_definitions(${LIB_NAME} PUBLIC RS_BROADCAST_DISCOVERY)
endif(RS_BROADCAST_DISCOVERY)

if(NOT RS_WARN_DEPRECATED)
	target_compile_definitions(${LIB_NAME} PUBLIC RS_NO_WARN_DEPRECATED)
	target_compile_options(
		${LIB_NAME} PUBLIC
		-Wno-deprecated -Wno-deprecated-declarations )
endif(NOT RS_WARN_DEPRECATED)

if(RS_WARN_LESS)
	target_compile_definitions(${LIB_NAME} PRIVATE RS_NO_WARN_CPP)
	
	target_compile_options(
		${LIB_NAME} PRIVATE
		-Wno-cpp -Wno-inconsistent-missing-override )
endif(RS_WARN_LESS)

if(RS_WEBUI)
	target_compile_definitions(${LIB_NAME} PUBLIC RS_WEBUI)
endif(RS_WEBUI)

################################################################################

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
	target_compile_definitions(
		${LIB_NAME} PUBLIC RS_DATA_DIR="${RS_DATA_DIR}" )
endif(CMAKE_SYSTEM_NAME STREQUAL "Linux")

################################################################################

if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
	target_link_libraries(${LIB_NAME} PRIVATE "-framework Cocoa")
endif()

################################################################################

if(RS_EXPORT_JNI_ONLOAD)
	target_compile_definitions(
		${LIB_NAME} PUBLIC RS_LIBRETROSHARE_EXPORT_JNI_ONLOAD )
endif(RS_EXPORT_JNI_ONLOAD)

################################################################################

if(RS_ANDROID)
	FetchContent_Declare(
		jni-hpp
		GIT_REPOSITORY "https://github.com/RetroShare/jni-hpp.git"
		GIT_TAG "origin/master"
		GIT_SHALLOW TRUE
		GIT_PROGRESS TRUE
		TIMEOUT 10
	)
	FetchContent_MakeAvailable(jni-hpp)

	target_include_directories(
		${LIB_NAME} PRIVATE "${jni-hpp_SOURCE_DIR}/include" )
endif(RS_ANDROID)

################################################################################

set( CMAKE_CXX_FLAGS "-Wno-deprecated-declarations" )
target_compile_definitions(${LIB_NAME} PUBLIC RS_NO_WARN_DEPRECATED )

set(V_VERSION_SET OFF)

if( NOT RS_MAJOR_VERSION STREQUAL "" AND
    NOT RS_MINOR_VERSION STREQUAL "" AND
    NOT RS_MINI_VERSION  STREQUAL "" )
	message(STATUS "RetroShare version ${RS_MAJOR_VERSION}.${RS_MINOR_VERSION}.${RS_MINI_VERSION}${RS_EXTRA_VERSION} defined in command line")
	set(V_VERSION_SET ON)
else()
	set(V_GIT_DESCRIBE_OUTPUT "")
	set(V_GIT_DESCRIBE_REGEXP "^v([0-9]+).([0-9]+).([0-9]+)(.*)\n$")

	execute_process(
		COMMAND ${GIT_EXECUTABLE} describe --tags --long --match v*.*.*
		WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
		OUTPUT_VARIABLE V_GIT_DESCRIBE_OUTPUT )

	if(V_GIT_DESCRIBE_OUTPUT MATCHES "${V_GIT_DESCRIBE_REGEXP}")
		string(
			REGEX REPLACE
			"${V_GIT_DESCRIBE_REGEXP}" "\\1;\\2;\\3;\\4"
			V_GIT_DESCRIBE_LIST ${V_GIT_DESCRIBE_OUTPUT} )
		list(GET V_GIT_DESCRIBE_LIST 0 RS_MAJOR_VERSION)
		list(GET V_GIT_DESCRIBE_LIST 1 RS_MINOR_VERSION)
		list(GET V_GIT_DESCRIBE_LIST 2 RS_MINI_VERSION)
		list(GET V_GIT_DESCRIBE_LIST 3 RS_EXTRA_VERSION)

		message("RetroShare version ${RS_MAJOR_VERSION}.${RS_MINOR_VERSION}.${RS_MINI_VERSION}${RS_EXTRA_VERSION} determined via git")
		set(V_VERSION_SET ON)
	else()
		message(WARNING "Determining RetroShare version via git failed plese specify it through CMake command line arguments!")
	endif()
endif()

if(V_VERSION_SET)
	target_compile_definitions(
		${LIB_NAME}
		PUBLIC RS_MAJOR_VERSION=${RS_MAJOR_VERSION}
		PUBLIC RS_MINOR_VERSION=${RS_MINOR_VERSION}
		PUBLIC RS_MINI_VERSION=${RS_MINI_VERSION}
		PUBLIC RS_EXTRA_VERSION="${RS_EXTRA_VERSION}" )
endif(V_VERSION_SET)

################################################################################

# TODO: Use generator expressions instead of CMAKE_BUILD_TYPE see
# https://cmake.org/cmake/help/latest/manual/cmake-buildsystem.7.html#build-configurations
# https://cmake.org/cmake/help/latest/manual/cmake-generator-expressions.7.html#manual:cmake-generator-expressions(7)

## On Android all symbols except for the few ones used called via JNI can be
## kept hidden allowing for more optimization, and reduced binary size
if(RS_ANDROID AND CMAKE_BUILD_TYPE STREQUAL "Release")
	set_property(TARGET ${LIB_NAME} PROPERTY C_VISIBILITY_PRESET hidden)
	set_property(TARGET ${LIB_NAME} PROPERTY CXX_VISIBILITY_PRESET hidden)
	set_property(TARGET ${LIB_NAME} PROPERTY VISIBILITY_INLINES_HIDDEN ON)
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Release")
	set_property(TARGET ${LIB_NAME} PROPERTY INTERPROCEDURAL_OPTIMIZATION ON)
endif()

################################################################################

if(RS_CPPTRACE_STACKTRACE)
    FetchContent_Declare(
      cpptrace
      GIT_REPOSITORY https://github.com/jeremy-rifkin/cpptrace.git
      GIT_TAG v0.3.1
      GIT_SHALLOW TRUE
      GIT_PROGRESS TRUE
      TIMEOUT 10
    )
    FetchContent_MakeAvailable(cpptrace)
    target_link_libraries(${LIB_NAME} PRIVATE cpptrace::cpptrace)
    target_compile_definitions(
        ${LIBRARY_NAME} PRIVATE RS_JEREMY_RIFKIN_CPPTRACE )
endif(RS_CPPTRACE_STACKTRACE)
