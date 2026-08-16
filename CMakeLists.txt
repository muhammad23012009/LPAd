cmake_minimum_required(VERSION 3.20)
set(PROJECT_NAME linuxpa)
project(linuxpa)

include(FindPkgConfig)
set(CMAKE_BUILD_TYPE Debug)
set(CMAKE_CXX_STANDARD 20)

set(LPAC_DYNAMIC_LIBEUICC ON)
set(LPAC_BUILD_LIBRARY ON)
add_subdirectory(lpac)

find_package(sdbus-c++ REQUIRED)
pkg_check_modules(GBINDER REQUIRED libgbinder)
pkg_check_modules(GLIB REQUIRED glib-2.0)
pkg_check_modules(CURL REQUIRED libcurl)

add_executable(${PROJECT_NAME} main.cpp lpa.cpp curl.cpp drivers/gbinder/gbinder.cpp)
target_link_libraries(${PROJECT_NAME} PRIVATE SDBusCpp::sdbus-c++ euicc ${GBINDER_LIBRARIES} ${GLIB_LIBRARIES} ${CURL_LIBRARIES})
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/lpac/euicc ${CMAKE_CURRENT_SOURCE_DIR}/include ${GBINDER_INCLUDE_DIRS} ${GLIB_INCLUDE_DIRS} ${CURL_INCLUDE_DIRS})