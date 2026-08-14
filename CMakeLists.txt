cmake_minimum_required(VERSION 3.16)
project(koutendb_cpp VERSION 0.1.3 LANGUAGES CXX)

add_library(koutendb_cpp INTERFACE)
add_library(KoutenDB::cpp ALIAS koutendb_cpp)

target_compile_features(koutendb_cpp INTERFACE cxx_std_17)
target_include_directories(
  koutendb_cpp
  INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

option(KOUTENDB_CPP_BUILD_EXAMPLES "Build KoutenDB C++ examples" ON)

set(KOUTENDB_CORE_DIR "" CACHE PATH "Path to a KoutenDB core checkout containing lib/libkoutendb.so")
set(KOUTENDB_LIBRARY "" CACHE FILEPATH "Path to libkoutendb.so")

if(KOUTENDB_LIBRARY)
  set(KOUTENDB_C_LIBRARY "${KOUTENDB_LIBRARY}")
elseif(KOUTENDB_CORE_DIR)
  set(KOUTENDB_C_LIBRARY "${KOUTENDB_CORE_DIR}/lib/libkoutendb.so")
elseif(DEFINED ENV{KOUTENDB_LIBRARY})
  set(KOUTENDB_C_LIBRARY "$ENV{KOUTENDB_LIBRARY}")
elseif(DEFINED ENV{KOUTENDB_CORE_DIR})
  set(KOUTENDB_C_LIBRARY "$ENV{KOUTENDB_CORE_DIR}/lib/libkoutendb.so")
else()
  find_library(KOUTENDB_C_LIBRARY NAMES koutendb libkoutendb)
endif()

if(KOUTENDB_CPP_BUILD_EXAMPLES)
  if(NOT KOUTENDB_C_LIBRARY)
    message(FATAL_ERROR "libkoutendb.so was not found. Set KOUTENDB_CORE_DIR or KOUTENDB_LIBRARY.")
  endif()

  add_executable(koutendb_cpp_contract_smoke examples/contract_smoke.cpp)
  target_link_libraries(koutendb_cpp_contract_smoke PRIVATE koutendb_cpp "${KOUTENDB_C_LIBRARY}")

  add_executable(koutendb_cpp_cluster_tls examples/cluster_tls.cpp)
  target_link_libraries(koutendb_cpp_cluster_tls PRIVATE koutendb_cpp "${KOUTENDB_C_LIBRARY}")

  add_executable(koutendb_cpp_sec_probe examples/sec_probe.cpp)
  target_link_libraries(koutendb_cpp_sec_probe PRIVATE koutendb_cpp "${KOUTENDB_C_LIBRARY}")

  get_filename_component(KOUTENDB_C_LIBRARY_DIR "${KOUTENDB_C_LIBRARY}" DIRECTORY)
  set_target_properties(
    koutendb_cpp_contract_smoke
    koutendb_cpp_cluster_tls
    koutendb_cpp_sec_probe
    PROPERTIES
      BUILD_RPATH "${KOUTENDB_C_LIBRARY_DIR}"
  )
endif()
