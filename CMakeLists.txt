cmake_minimum_required(VERSION 3.0.0)
project(Xerxes VERSION 0.1.0 LANGUAGES C CXX)

# Set DRAMPower path
set(DRAMPOWER_SOURCE_DIR
  "${PROJECT_SOURCE_DIR}/drampower/src"
  CACHE PATH
  ""
  FORCE
)

# Check SimpleSSD is valid
if (NOT EXISTS "${PROJECT_SOURCE_DIR}/simplessd/CMakeLists.txt")
  message(FATAL_ERROR "Failed to find SimpleSSD. \
Check you initialized submodules.")
endif ()

# Add subproject
add_subdirectory(${PROJECT_SOURCE_DIR}/simplessd
  ${CMAKE_BINARY_DIR}/simplessd
)

add_subdirectory(DRAMsim3)

include_directories(DRAMsim3/ext/headers)
# Set include directories
include_directories(
  ${PROJECT_SOURCE_DIR}
  ${PROJECT_SOURCE_DIR}/simplessd
  ${DRAMPOWER_SOURCE_DIR}
)

add_executable(Xerxes main.cc xerxes_standalone.cc xerxes_basic.cc)
set(CMAKE_EXPORT_COMPILE_COMMANDS True)
target_compile_options(Xerxes PRIVATE -Wall)
target_link_libraries(Xerxes PRIVATE dramsim3)
target_link_libraries(Xerxes PRIVATE simplessd)

set(CPACK_PROJECT_NAME ${PROJECT_NAME})
set(CPACK_PROJECT_VERSION ${PROJECT_VERSION})

include(CPack)
