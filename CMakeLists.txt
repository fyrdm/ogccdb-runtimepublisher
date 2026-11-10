cmake_minimum_required(VERSION 3.20)

option(OGCCDB_RUNTIME_BUILD_TESTS "Build the tests" ON)
if(OGCCDB_RUNTIME_BUILD_TESTS)
  list(APPEND VCPKG_MANIFEST_FEATURES tests)
endif()

project(ogccdb-runtimepublisher VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

include(GNUInstallDirs)

# While ogccdb-core is 0.x, a minor release may change its API: 0.4 accepts 0.4.z only.
find_package(ogccdb-core 0.4 CONFIG REQUIRED)
find_package(Threads REQUIRED)

add_library(ogccdb_runtime
  src/math.cpp
  src/geodesy.cpp
  src/tile_tree.cpp
  src/terrain_mesh.cpp
  src/selection.cpp
  src/terrain.cpp
  src/c_api.cpp
)
add_library(ogccdb::runtime ALIAS ogccdb_runtime)
set_target_properties(ogccdb_runtime PROPERTIES EXPORT_NAME runtime POSITION_INDEPENDENT_CODE ON)
target_include_directories(ogccdb_runtime PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_link_libraries(ogccdb_runtime PUBLIC ogccdb::core Threads::Threads)
target_compile_options(ogccdb_runtime PRIVATE
  $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
  $<$<CXX_COMPILER_ID:MSVC>:/W4>
)

# Install the library, headers and a CMake package:
# find_package(ogccdb-runtimepublisher) gives ogccdb::runtime.
include(CMakePackageConfigHelpers)
set(OGCCDB_RUNTIME_CMAKE_DIR ${CMAKE_INSTALL_LIBDIR}/cmake/ogccdb-runtimepublisher)
install(TARGETS ogccdb_runtime EXPORT ogccdb-runtimepublisher-targets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT ogccdb-runtimepublisher-targets NAMESPACE ogccdb:: DESTINATION ${OGCCDB_RUNTIME_CMAKE_DIR})
configure_package_config_file(cmake/ogccdb-runtimepublisher-config.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/ogccdb-runtimepublisher-config.cmake
  INSTALL_DESTINATION ${OGCCDB_RUNTIME_CMAKE_DIR}
)
write_basic_package_version_file(
  ${CMAKE_CURRENT_BINARY_DIR}/ogccdb-runtimepublisher-config-version.cmake
  COMPATIBILITY SameMinorVersion
)
install(FILES
  ${CMAKE_CURRENT_BINARY_DIR}/ogccdb-runtimepublisher-config.cmake
  ${CMAKE_CURRENT_BINARY_DIR}/ogccdb-runtimepublisher-config-version.cmake
  DESTINATION ${OGCCDB_RUNTIME_CMAKE_DIR}
)

if(OGCCDB_RUNTIME_BUILD_TESTS)
  enable_testing()
  find_package(GTest REQUIRED)
  add_executable(ogccdb_runtime_tests
    tests/test_geodesy.cpp
    tests/test_tile_tree.cpp
    tests/test_terrain_mesh.cpp
    tests/test_selection.cpp
    tests/test_terrain.cpp
    tests/test_c_api.c
    tests/test_c_api.cpp
  )
  target_link_libraries(ogccdb_runtime_tests PRIVATE ogccdb::runtime GTest::gtest_main)
  target_compile_options(ogccdb_runtime_tests PRIVATE
    $<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang>:-Wall -Wextra -Wpedantic>
  )
  gtest_discover_tests(ogccdb_runtime_tests)
endif()
