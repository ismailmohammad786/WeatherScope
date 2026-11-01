cmake_minimum_required(VERSION 3.20)

project(WeatherScopeBackend LANGUAGES CXX)

include(FetchContent)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CROW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CROW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(CROW_ENABLE_SSL OFF CACHE BOOL "" FORCE)
set(CROW_ENABLE_COMPRESSION OFF CACHE BOOL "" FORCE)
set(JSON_BuildTests OFF CACHE BOOL "" FORCE)

# Crow uses standalone Asio for networking. Fetch its headers first so Crow's
# CMake package can find them when it configures.
FetchContent_Declare(
    asio
    GIT_REPOSITORY https://github.com/chriskohlhoff/asio.git
    GIT_TAG asio-1-30-2
    GIT_SHALLOW TRUE
)
FetchContent_GetProperties(asio)
if(NOT asio_POPULATED)
    FetchContent_Populate(asio)
endif()
set(ASIO_INCLUDE_DIR "${asio_SOURCE_DIR}/asio/include" CACHE PATH "Standalone Asio headers" FORCE)

FetchContent_Declare(
    Crow
    GIT_REPOSITORY https://github.com/CrowCpp/Crow.git
    GIT_TAG v1.3.4
    GIT_SHALLOW TRUE
)

FetchContent_Declare(
    nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
)

FetchContent_MakeAvailable(Crow nlohmann_json)

add_executable(weather_backend src/main.cpp)
add_executable(weatherscope_desktop WIN32 src/desktop.cpp)
set_target_properties(weatherscope_desktop PROPERTIES OUTPUT_NAME WeatherScope)
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/web")
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/web/index.html"
    "${CMAKE_CURRENT_BINARY_DIR}/web/index.html" COPYONLY)

target_include_directories(weather_backend PRIVATE src)
target_link_libraries(
    weather_backend
    PRIVATE
        Crow::Crow
        nlohmann_json::nlohmann_json
)

if(WIN32)
    target_link_libraries(weather_backend PRIVATE ws2_32 mswsock winhttp)
    target_link_libraries(weatherscope_desktop PRIVATE winhttp)
endif()

if(MINGW)
    target_link_options(weather_backend PRIVATE -static -static-libgcc -static-libstdc++)
    target_link_options(weatherscope_desktop PRIVATE -municode -static -static-libgcc -static-libstdc++)
endif()

