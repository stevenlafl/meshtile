cmake_minimum_required(VERSION 3.16)
project(meshtile VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# System dependencies
find_package(CURL REQUIRED)
find_package(ZLIB REQUIRED)
find_package(Threads REQUIRED)

# Third-party: auto-download via FetchContent
include(FetchContent)

FetchContent_Declare(itm
    GIT_REPOSITORY https://github.com/NTIA/itm.git
    GIT_TAG        master
    GIT_SHALLOW    TRUE
)

FetchContent_Declare(httplib
    GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
    GIT_TAG        v0.18.3
    GIT_SHALLOW    TRUE
)

FetchContent_Declare(json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
    GIT_SHALLOW    TRUE
)

FetchContent_Declare(lodepng
    GIT_REPOSITORY https://github.com/lvandeve/lodepng.git
    GIT_TAG        master
    GIT_SHALLOW    TRUE
)

# Fetch all
FetchContent_MakeAvailable(httplib json)

# ITM: no CMakeLists.txt, uses Windows backslash includes — patch them
FetchContent_Populate(itm)

# Fix Windows backslash includes in all source and header files
file(GLOB_RECURSE ITM_ALL_FILES
    ${itm_SOURCE_DIR}/src/*.cpp
    ${itm_SOURCE_DIR}/include/*.h
)
foreach(_f ${ITM_ALL_FILES})
    file(READ ${_f} _content)
    # Replace ..\include\XXX with XXX (any header under include/)
    string(REGEX REPLACE "\"\\.\\.\\\\include\\\\([^\"]+)\"" "\"\\1\"" _content "${_content}")
    file(WRITE ${_f} "${_content}")
endforeach()

# Fix DLLEXPORT for non-Windows: patch itm.h
file(READ ${itm_SOURCE_DIR}/include/itm.h _itm_h)
string(REPLACE
    "#define DLLEXPORT extern \"C\" __declspec(dllexport)"
    "#ifdef _WIN32\n#define DLLEXPORT extern \"C\" __declspec(dllexport)\n#else\n#define DLLEXPORT extern \"C\"\n#endif"
    _itm_h "${_itm_h}")
file(WRITE ${itm_SOURCE_DIR}/include/itm.h "${_itm_h}")

file(GLOB ITM_SRCS ${itm_SOURCE_DIR}/src/*.cpp)
add_library(itm_lib STATIC ${ITM_SRCS})
target_include_directories(itm_lib PUBLIC ${itm_SOURCE_DIR}/include)

# LodePNG: no CMakeLists.txt, build manually
FetchContent_Populate(lodepng)
add_library(lodepng STATIC ${lodepng_SOURCE_DIR}/lodepng.cpp)
target_include_directories(lodepng PUBLIC ${lodepng_SOURCE_DIR})

# Main executable
set(SOURCES
    src/main.cpp
    src/api_client.cpp
    src/signal_cache.cpp
    src/tile_renderer.cpp
    src/node_renderer.cpp
    src/hgt_provider.cpp
    src/disk_cache.cpp
    src/log.cpp
)

add_executable(meshtile ${SOURCES})
target_include_directories(meshtile PRIVATE src)
target_link_libraries(meshtile
    PRIVATE
        itm_lib
        lodepng
        httplib::httplib
        nlohmann_json::nlohmann_json
        CURL::libcurl
        ZLIB::ZLIB
        Threads::Threads
)

# Shared source files (everything except main.cpp)
set(LIB_SOURCES
    src/api_client.cpp
    src/signal_cache.cpp
    src/hgt_provider.cpp
    src/disk_cache.cpp
    src/log.cpp
)

# Validation test
add_executable(validate_itm tests/validate_itm.cpp ${LIB_SOURCES})
target_include_directories(validate_itm PRIVATE src)
target_link_libraries(validate_itm
    PRIVATE
        itm_lib
        nlohmann_json::nlohmann_json
        CURL::libcurl
        ZLIB::ZLIB
        Threads::Threads
)
