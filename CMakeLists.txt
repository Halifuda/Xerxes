cmake_minimum_required(VERSION 3.0.0)
project(Xerxes VERSION 0.1.0 LANGUAGES C CXX)


add_subdirectory(DRAMsim3)

include_directories(DRAMsim3/ext/headers)

add_executable(Xerxes main.cc xerxes_standalone.cc xerxes_basic.cc)
set(CMAKE_EXPORT_COMPILE_COMMANDS True)
target_compile_options(Xerxes PRIVATE -Wall)
target_link_libraries(Xerxes PRIVATE dramsim3)

set(CPACK_PROJECT_NAME ${PROJECT_NAME})
set(CPACK_PROJECT_VERSION ${PROJECT_VERSION})
include(CPack)
