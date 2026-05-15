cmake_minimum_required(VERSION 3.15)
if(POLICY CMP0091)
    cmake_policy(SET CMP0091 NEW)
endif()
project(Q2HordeMod LANGUAGES C CXX)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(DEPLOY_DIRECTORY "" CACHE PATH "Directory where the game library and data files should be copied")

find_package(fmt CONFIG REQUIRED)
find_package(jsoncpp CONFIG REQUIRED)

file(GLOB MOD_SOURCES CONFIGURE_DEPENDS
     "*.cpp" "bots/*.cpp" "ctf/*.cpp" "horde/*.cpp" "rogue/*.cpp" "xatrix/*.cpp")

set(VENDOR_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/vendor/lua.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/vendor/sqlite3.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/vendor/twister.c")

add_library(Q2HordeModDLL SHARED ${MOD_SOURCES} ${VENDOR_SOURCES})
set_property(TARGET Q2HordeModDLL PROPERTY
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

target_include_directories(Q2HordeModDLL PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/vendor")

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
else()
    target_compile_options(Q2HordeModDLL PRIVATE
        $<$<COMPILE_LANGUAGE:CXX>:-Wnull-dereference>
        -Wno-unused-parameter
        -Wno-error
        $<$<COMPILE_LANGUAGE:CXX>:-Wno-deprecated-enum-enum-conversion>
        $<$<COMPILE_LANGUAGE:CXX>:-Wno-template-id-cdtor>
        $<$<CONFIG:Debug>:-g>
        $<$<CONFIG:Debug>:-O0>
        $<$<CONFIG:RelWithDebInfo>:-g>
        $<$<CONFIG:RelWithDebInfo>:-O2>
        $<$<CONFIG:Release>:-O3>)

    target_link_options(Q2HordeModDLL PRIVATE $<$<CONFIG:Release>:-s>)
endif()

target_link_libraries(Q2HordeModDLL PRIVATE
    fmt::fmt
    JsonCpp::JsonCpp)

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

if(WIN32)
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(GAME_OUTPUT_NAME "game_x64")
    else()
        set(GAME_OUTPUT_NAME "game_x86")
    endif()
    set(GAME_SUFFIX ".dll")
elseif(APPLE)
    execute_process(COMMAND uname -m OUTPUT_VARIABLE GAME_ARCH OUTPUT_STRIP_TRAILING_WHITESPACE)
    set(GAME_OUTPUT_NAME "game_${GAME_ARCH}")
    set(GAME_SUFFIX ".dylib")
else()
    execute_process(COMMAND uname -m OUTPUT_VARIABLE GAME_ARCH OUTPUT_STRIP_TRAILING_WHITESPACE)
    string(REGEX REPLACE "^i.86$" "i386" GAME_ARCH "${GAME_ARCH}")
    set(GAME_OUTPUT_NAME "game_${GAME_ARCH}")
    set(GAME_SUFFIX ".so")
endif()

set_target_properties(Q2HordeModDLL PROPERTIES
    OUTPUT_NAME "${GAME_OUTPUT_NAME}"
    PREFIX ""
    SUFFIX "${GAME_SUFFIX}"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

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
