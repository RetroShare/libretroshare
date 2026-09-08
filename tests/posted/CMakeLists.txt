cmake_minimum_required(VERSION 3.18)
project(posted_regression_tests LANGUAGES CXX)
enable_testing()
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

get_filename_component(RS_ROOT "../.." ABSOLUTE)
set(RAPIDJSON_INCLUDE_DIR "${RS_ROOT}/../supportlibs/rapidjson/include" CACHE PATH "RapidJSON headers")
set(RNP_INCLUDE_DIR "${RS_ROOT}/../supportlibs/librnp/include" CACHE PATH "RNP headers")
find_package(OpenSSL REQUIRED)

add_library(posted_test_support STATIC
    ${RS_ROOT}/src/gxs/gxssecurity.cc
    ${RS_ROOT}/src/gxs/rsgxsdata.cc
    ${RS_ROOT}/src/rsitems/rsnxsitems.cc
    ${RS_ROOT}/src/services/p3gxscommon.cc
    ${RS_ROOT}/src/rsitems/rsposteditems.cc
    ${RS_ROOT}/src/rsitems/rsgxscommentitems.cc
    ${RS_ROOT}/src/rsitems/rsgxsitems.cc
    ${RS_ROOT}/src/serialiser/rsserializable.cc
    ${RS_ROOT}/src/serialiser/rsbaseserial.cc
    ${RS_ROOT}/src/serialiser/rsserial.cc
    ${RS_ROOT}/src/serialiser/rsserializer.cc
    ${RS_ROOT}/src/serialiser/rstypeserializer.cc
    ${RS_ROOT}/src/serialiser/rstlvbase.cc
    ${RS_ROOT}/src/serialiser/rstlvbinary.cc
    ${RS_ROOT}/src/serialiser/rstlvimage.cc
    ${RS_ROOT}/src/serialiser/rstlvitem.cc
    ${RS_ROOT}/src/serialiser/rstlvkeys.cc
    ${RS_ROOT}/src/util/rsdebug.cc
    ${RS_ROOT}/src/util/rsjson.cc
    ${RS_ROOT}/src/util/rsstacktrace.cc
    ${RS_ROOT}/src/util/rsbase64.cc
    ${RS_ROOT}/src/util/rsprint.cc
    ${RS_ROOT}/src/util/rsthreads.cc
    ${RS_ROOT}/src/util/rsrandom.cc
    ${RS_ROOT}/src/util/rsdir.cc
    ${RS_ROOT}/src/util/rstime.cc
    ${RS_ROOT}/src/util/smallobject.cc)
target_include_directories(posted_test_support PUBLIC ${RS_ROOT}/src ${RAPIDJSON_INCLUDE_DIR}
    ${RNP_INCLUDE_DIR} ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(posted_test_support PUBLIC OpenSSL::Crypto)
target_compile_options(posted_test_support PUBLIC
    -Wno-deprecated-declarations -Wno-inconsistent-missing-override -Wno-cpp)
target_compile_options(posted_test_support PRIVATE -ffunction-sections -fdata-sections)

# RNP's public headers include this generated export header, even though
# these tests use only RetroShare's OpenSSL message-signature implementation.
include(GenerateExportHeader)
generate_export_header(posted_test_support BASE_NAME rnp EXPORT_MACRO_NAME RNP_API
    EXPORT_FILE_NAME rnp/rnp_export.h STATIC_DEFINE RNP_STATIC INCLUDE_GUARD_NAME RNP_EXPORT)

add_library(posted_service_compile_check OBJECT
    ${RS_ROOT}/src/services/p3posted.cc ${RS_ROOT}/src/gxs/rsgenexchange.cc)
target_link_libraries(posted_service_compile_check PRIVATE posted_test_support)

add_executable(posted_tests posted_tests.cc)
target_link_libraries(posted_tests PRIVATE posted_test_support)
if(APPLE)
    target_link_options(posted_tests PRIVATE -Wl,-dead_strip)
else()
    target_link_options(posted_tests PRIVATE -Wl,--gc-sections)
endif()
add_test(NAME posted_regressions COMMAND posted_tests)
