cmake_minimum_required(VERSION 3.16)

project(xmuduo VERSION 0.1.0 LANGUAGES CXX)

option(XMUDUO_ENABLE_DEBUG_LOG "Enable debug logging" OFF)
find_package(Threads REQUIRED)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

file(GLOB XMUDUO_ROOT_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cxx"
)
file(GLOB_RECURSE XMUDUO_SRC_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cxx"
)

set(XMUDUO_SOURCES ${XMUDUO_ROOT_SOURCES} ${XMUDUO_SRC_SOURCES})
if(NOT XMUDUO_SOURCES)
    message(FATAL_ERROR "No C++ sources found. Add .cc, .cpp, or .cxx files to the project root or src/.")
endif()

add_library(xmuduo STATIC ${XMUDUO_SOURCES})
target_compile_features(xmuduo PUBLIC cxx_std_20)
target_link_libraries(xmuduo PUBLIC Threads::Threads)
target_compile_definitions(xmuduo PUBLIC
    XMUDUO_ENABLE_DEBUG_LOG=$<BOOL:${XMUDUO_ENABLE_DEBUG_LOG}>
)
target_include_directories(xmuduo PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
