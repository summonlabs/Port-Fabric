cmake_minimum_required(VERSION 3.25)

# Independent downstream consumer of the installed Port Fabric package.
#
# This project must never reference the Port Fabric source tree: it uses only the
# installed headers, the installed library and the installed CMake package files.
project(portfabric_consumer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(PortFabric CONFIG REQUIRED)

add_executable(portfabric_consumer main.cpp)
target_link_libraries(portfabric_consumer PRIVATE SummonSoftwareLabs::PortFabric)

if(MSVC)
  target_compile_options(portfabric_consumer PRIVATE /W4 /WX /permissive- /utf-8)
endif()
