cmake_minimum_required(VERSION 3.25)
project(appimagetool VERSION 0.4.0 LANGUAGES C
    DESCRIPTION "Create AppImages from an AppDir using DWARFS compression and uruntime"
)

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

option(APPIMAGETOOL_STATIC "Statically link all dependencies" ON)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Os -fvisibility=hidden)
    if(APPIMAGETOOL_STATIC)
        add_compile_options(-ffunction-sections -fdata-sections)
        add_link_options(-Wl,--gc-sections -Wl,-z,noexecstack)
        if(NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
            set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
        endif()
    endif()
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBCURL REQUIRED libcurl)
pkg_check_modules(ZLIB REQUIRED zlib)

if(APPIMAGETOOL_STATIC)
    set(_saved ${CMAKE_FIND_LIBRARY_SUFFIXES})
    set(CMAKE_FIND_LIBRARY_SUFFIXES .a)
    find_library(LIBCURL_STATIC NAMES curl)
    find_library(ZLIB_STATIC NAMES z)
    set(CMAKE_FIND_LIBRARY_SUFFIXES ${_saved})
endif()

set(APPIMAGETOOL_CURL_LIBS ${LIBCURL_LIBRARIES})
if(APPIMAGETOOL_STATIC AND LIBCURL_STATIC)
    set(APPIMAGETOOL_CURL_LIBS ${LIBCURL_STATIC})
    set(OPENSSL_USE_STATIC_LIBS TRUE)
    find_package(OpenSSL REQUIRED)
    pkg_check_modules(CARES REQUIRED libcares)
endif()

set(APPIMAGETOOL_ZLIB_LIBS ${ZLIB_LIBRARIES})
if(APPIMAGETOOL_STATIC AND ZLIB_STATIC)
    set(APPIMAGETOOL_ZLIB_LIBS ${ZLIB_STATIC})
endif()

set(APPIMAGETOOL_SOURCES
    src/main.c
    src/log.c
    src/config.c
    src/desktop.c
    src/elf.c
    src/util.c
    src/uruntime.c
    src/dwarfs.c
    src/zsync.c
    src/sha1.c
    src/appimage.c
)

add_executable(appimagetool ${APPIMAGETOOL_SOURCES})

if(APPIMAGETOOL_STATIC AND LIBCURL_STATIC)
    target_link_options(appimagetool PRIVATE -static)
endif()

target_link_libraries(appimagetool PRIVATE
    ${APPIMAGETOOL_CURL_LIBS}
    ${APPIMAGETOOL_ZLIB_LIBS}
    m
)

include(CheckLibraryExists)
if(APPIMAGETOOL_STATIC AND LIBCURL_STATIC)
    target_link_libraries(appimagetool PRIVATE
        OpenSSL::SSL
        OpenSSL::Crypto
        ${CARES_LIBRARIES}
        nghttp2
        ${CMAKE_DL_LIBS}
        pthread
    )
    check_library_exists(rt clock_gettime "" HAVE_LIBRT)
    if(HAVE_LIBRT)
        target_link_libraries(appimagetool PRIVATE rt)
    endif()
endif()

target_include_directories(appimagetool PRIVATE
    ${LIBCURL_INCLUDE_DIRS}
    ${ZLIB_INCLUDE_DIRS}
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

target_compile_definitions(appimagetool PRIVATE
    _GNU_SOURCE
    APPIMAGETOOL_NAME="${PROJECT_NAME}"
    APPIMAGETOOL_VERSION="${PROJECT_VERSION}"
    $<$<BOOL:${APPIMAGETOOL_STATIC}>:APPIMAGETOOL_STATIC>
)
