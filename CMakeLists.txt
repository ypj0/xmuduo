cmake_minimum_required(VERSION 3.16)
project(xmuduo VERSION 0.1.0 LANGUAGES CXX)

option(XMUDUO_ENABLE_DEBUG_LOG "Enable debug logging" OFF)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Threads REQUIRED)
include(CheckCXXSourceCompiles)
# Some libraries implement time zones without advertising all C++20 chrono
# features in __cpp_lib_chrono. Test the actual API and link support instead.
check_cxx_source_compiles("
    #include <chrono>
    int main() {
        const auto* zone = std::chrono::locate_zone(\"UTC\");
        std::chrono::zoned_time<std::chrono::microseconds> time{
            zone, std::chrono::sys_time<std::chrono::microseconds>{}};
        return time.get_local_time().time_since_epoch().count() != 0;
    }" XMUDUO_HAS_CHRONO_TIME_ZONES)
if(NOT XMUDUO_HAS_CHRONO_TIME_ZONES)
    message(FATAL_ERROR
        "xmuduo requires C++20 chrono time-zone support (e.g. GCC 13+ with libstdc++).")
endif()

add_library(xmuduo STATIC
    src/AsyncLogSink.cc
    src/FileSink.cc
    src/RollingFileSink.cc
    src/Logger.cc
    src/Timestamp.cc
    src/TimeZone.cc
)
target_compile_features(xmuduo PUBLIC cxx_std_20)
target_link_libraries(xmuduo PUBLIC Threads::Threads)
target_compile_definitions(xmuduo PUBLIC
    XMUDUO_ENABLE_DEBUG_LOG=$<BOOL:${XMUDUO_ENABLE_DEBUG_LOG}>
)
target_include_directories(xmuduo PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")

include(CTest)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
