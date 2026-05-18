cmake_minimum_required(VERSION 3.25)

include(FetchContent)

project(Q2HordeMod LANGUAGES C CXX)

option(VRX_REPRO "Use Quake II Remaster/q2repro game library naming." TRUE)
option(Q2HORDE_FETCH_DEPS "Fetch fmt/jsoncpp if they are not available locally." TRUE)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED TRUE)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED TRUE)
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

set(DEPLOY_DIRECTORY "" CACHE PATH "Directory where the game library and data files should be copied")

if(APPLE)
    foreach(HOMEBREW_PREFIX IN ITEMS /opt/homebrew /usr/local)
        if(EXISTS "${HOMEBREW_PREFIX}")
            list(APPEND CMAKE_PREFIX_PATH "${HOMEBREW_PREFIX}")
        endif()
    endforeach()
endif()

find_path(Q2HORDE_BOOST_INCLUDE_DIR
    NAMES
        boost/container/flat_map.hpp
        boost/unordered/unordered_flat_map.hpp)

if(NOT Q2HORDE_BOOST_INCLUDE_DIR)
    message(FATAL_ERROR "Boost headers were not found. Install Boost or configure with a CMAKE_PREFIX_PATH that contains Boost.")
endif()

find_package(fmt CONFIG QUIET)
if(NOT fmt_FOUND AND Q2HORDE_FETCH_DEPS)
    set(FMT_INSTALL OFF CACHE BOOL "" FORCE)
    set(FMT_TEST OFF CACHE BOOL "" FORCE)
    set(FMT_DOC OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        fmt
        GIT_REPOSITORY https://github.com/fmtlib/fmt.git
        GIT_TAG 12.1.0)
    FetchContent_MakeAvailable(fmt)
endif()

if(TARGET fmt::fmt-header-only)
    set(Q2HORDE_FMT_TARGET fmt::fmt-header-only)
elseif(TARGET fmt::fmt)
    set(Q2HORDE_FMT_TARGET fmt::fmt)
else()
    message(FATAL_ERROR "fmt was not found and could not be made available.")
endif()

find_package(jsoncpp CONFIG QUIET)
if(NOT jsoncpp_FOUND AND Q2HORDE_FETCH_DEPS)
    set(JSONCPP_WITH_TESTS OFF CACHE BOOL "" FORCE)
    set(JSONCPP_WITH_POST_BUILD_UNITTEST OFF CACHE BOOL "" FORCE)
    set(JSONCPP_WITH_WARNING_AS_ERROR OFF CACHE BOOL "" FORCE)
    set(JSONCPP_WITH_PKGCONFIG_SUPPORT OFF CACHE BOOL "" FORCE)
    set(JSONCPP_WITH_CMAKE_PACKAGE OFF CACHE BOOL "" FORCE)
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        jsoncpp
        GIT_REPOSITORY https://github.com/open-source-parsers/jsoncpp.git
        GIT_TAG 1.9.6)
    FetchContent_MakeAvailable(jsoncpp)
endif()

if(TARGET JsonCpp::JsonCpp)
    set(Q2HORDE_JSONCPP_TARGET JsonCpp::JsonCpp)
elseif(TARGET jsoncpp_static)
    set(Q2HORDE_JSONCPP_TARGET jsoncpp_static)
elseif(TARGET jsoncpp_lib)
    set(Q2HORDE_JSONCPP_TARGET jsoncpp_lib)
else()
    message(FATAL_ERROR "jsoncpp was not found and could not be made available.")
endif()

file(GLOB MOD_SOURCES CONFIGURE_DEPENDS
    "*.cpp"
    "bots/*.cpp"
    "ctf/*.cpp"
    "horde/*.cpp"
    "rogue/*.cpp"
    "xatrix/*.cpp")

set(VENDOR_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/vendor/lua.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/vendor/sqlite3.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/vendor/twister.c")

add_library(Q2HordeModDLL SHARED ${MOD_SOURCES} ${VENDOR_SOURCES})
set_property(TARGET Q2HordeModDLL PROPERTY
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

target_include_directories(Q2HordeModDLL PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/vendor"
    "${Q2HORDE_BOOST_INCLUDE_DIR}")

target_compile_definitions(Q2HordeModDLL PRIVATE
    FMT_HEADER_ONLY
    KEX_Q2_GAME
    KEX_Q2GAME_EXPORTS
    KEX_Q2GAME_DYNAMIC
    _CRT_SECURE_NO_WARNINGS
    SQLITE_THREADSAFE=1
    SQLITE_OMIT_LOAD_EXTENSION)

if(WIN32)
    target_compile_definitions(Q2HordeModDLL PRIVATE
        _WINDOWS
        WIN32
        _CONSOLE
        NOMINMAX
        WIN32_LEAN_AND_MEAN)
endif()

if(MSVC)
    target_compile_options(Q2HordeModDLL PRIVATE /permissive- /bigobj)
    set_source_files_properties("${CMAKE_CURRENT_SOURCE_DIR}/vendor/lua.c"
        PROPERTIES COMPILE_OPTIONS "/wd4090")
else()
    target_compile_options(Q2HordeModDLL PRIVATE
        $<$<COMPILE_LANGUAGE:CXX>:-Wnull-dereference>
        -Wno-unused-parameter
        -Wno-error
        $<$<COMPILE_LANG_AND_ID:CXX,AppleClang,Clang>:-Wno-unknown-warning-option>
        $<$<COMPILE_LANGUAGE:CXX>:-Wno-deprecated-enum-enum-conversion>
        $<$<COMPILE_LANGUAGE:CXX>:-Wno-deprecated-anon-enum-enum-conversion>
        $<$<COMPILE_LANGUAGE:CXX>:-Wno-invalid-offsetof>
        $<$<COMPILE_LANG_AND_ID:CXX,GNU>:-Wno-template-id-cdtor>
        $<$<COMPILE_LANGUAGE:CXX>:-Wno-ignored-attributes>
        $<$<COMPILE_LANG_AND_ID:CXX,AppleClang,Clang>:-fdeclspec>
        $<$<CONFIG:Debug>:-g>
        $<$<CONFIG:Debug>:-O0>
        $<$<CONFIG:RelWithDebInfo>:-g>
        $<$<CONFIG:RelWithDebInfo>:-O2>
        $<$<CONFIG:Release>:-O3>)

    target_link_options(Q2HordeModDLL PRIVATE $<$<CONFIG:Release>:-s>)
endif()

target_link_libraries(Q2HordeModDLL PRIVATE
    ${Q2HORDE_FMT_TARGET}
    ${Q2HORDE_JSONCPP_TARGET})

if(WIN32)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND MINGW)
        execute_process(
            COMMAND x86_64-w64-mingw32-gcc -print-libgcc-file-name
            OUTPUT_VARIABLE LIBGCC_FILE
            OUTPUT_STRIP_TRAILING_WHITESPACE)
        get_filename_component(GCC_LIB_DIR "${LIBGCC_FILE}" DIRECTORY)
        target_link_directories(Q2HordeModDLL PRIVATE "${GCC_LIB_DIR}")
        target_link_options(Q2HordeModDLL PRIVATE
            -fuse-ld=lld
            -L${GCC_LIB_DIR}
            -static-libstdc++
            -static-libgcc)
        target_link_libraries(Q2HordeModDLL PRIVATE
            "${GCC_LIB_DIR}/libgcc.a"
            "${GCC_LIB_DIR}/libgcc_eh.a")
    endif()

    target_link_libraries(Q2HordeModDLL PRIVATE
        ws2_32 winmm user32 gdi32 ole32 uuid kernel32 advapi32)
elseif(UNIX)
    find_package(Threads REQUIRED)
    target_link_libraries(Q2HordeModDLL PRIVATE Threads::Threads m)
    if(NOT APPLE)
        target_link_libraries(Q2HordeModDLL PRIVATE dl)
    endif()
endif()

if(NOT VRX_REPRO)
    set(GAME_NAME game)
else()
    set(GAME_NAME game_)
endif()

if(WIN32)
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        if(VRX_REPRO)
            set(GAME_OUTPUT_NAME "${GAME_NAME}x64")
        else()
            set(GAME_OUTPUT_NAME "${GAME_NAME}x86_64")
        endif()
    else()
        set(GAME_OUTPUT_NAME "${GAME_NAME}x86")
    endif()
    set(GAME_SUFFIX ".dll")
elseif(UNIX)
    execute_process(
        COMMAND uname -m
        COMMAND sed s/i.86/i386/
        COMMAND tr -d "\n"
        OUTPUT_VARIABLE GAME_ARCH)
    set(GAME_OUTPUT_NAME "${GAME_NAME}${GAME_ARCH}")
    if(APPLE)
        set(GAME_SUFFIX ".dylib")
    else()
        set(GAME_SUFFIX ".so")
    endif()
else()
    message(FATAL_ERROR "Unknown platform")
endif()

set_target_properties(Q2HordeModDLL PROPERTIES
    OUTPUT_NAME "${GAME_OUTPUT_NAME}"
    PREFIX ""
    SUFFIX "${GAME_SUFFIX}"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

if(DEPLOY_DIRECTORY AND IS_DIRECTORY "${DEPLOY_DIRECTORY}")
    install(TARGETS Q2HordeModDLL
        RUNTIME DESTINATION "${DEPLOY_DIRECTORY}"
        LIBRARY DESTINATION "${DEPLOY_DIRECTORY}")

    if(EXISTS "${CMAKE_SOURCE_DIR}/deploy/bots")
        install(DIRECTORY "${CMAKE_SOURCE_DIR}/deploy/bots"
            DESTINATION "${DEPLOY_DIRECTORY}")
    endif()

    if(EXISTS "${CMAKE_SOURCE_DIR}/deploy/ents")
        install(DIRECTORY "${CMAKE_SOURCE_DIR}/deploy/ents"
            DESTINATION "${DEPLOY_DIRECTORY}")
    endif()

    if(EXISTS "${CMAKE_SOURCE_DIR}/deploy/config")
        install(DIRECTORY "${CMAKE_SOURCE_DIR}/deploy/config/"
            DESTINATION "${DEPLOY_DIRECTORY}/config"
            FILES_MATCHING PATTERN "*.lua")
    endif()
endif()
