cmake_minimum_required(VERSION 3.16)
project(rochedb_cpp VERSION 0.1.0 LANGUAGES CXX)

add_library(rochedb_cpp INTERFACE)
add_library(RocheDB::cpp ALIAS rochedb_cpp)

target_compile_features(rochedb_cpp INTERFACE cxx_std_17)
target_include_directories(
  rochedb_cpp
  INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

option(ROCHEDB_CPP_BUILD_EXAMPLES "Build RocheDB C++ examples" ON)

set(ROCHEDB_CORE_DIR "" CACHE PATH "Path to a RocheDB core checkout containing lib/librochedb.so")
set(ROCHEDB_LIBRARY "" CACHE FILEPATH "Path to librochedb.so")

if(ROCHEDB_LIBRARY)
  set(ROCHEDB_C_LIBRARY "${ROCHEDB_LIBRARY}")
elseif(ROCHEDB_CORE_DIR)
  set(ROCHEDB_C_LIBRARY "${ROCHEDB_CORE_DIR}/lib/librochedb.so")
elseif(DEFINED ENV{ROCHEDB_LIBRARY})
  set(ROCHEDB_C_LIBRARY "$ENV{ROCHEDB_LIBRARY}")
elseif(DEFINED ENV{ROCHEDB_CORE_DIR})
  set(ROCHEDB_C_LIBRARY "$ENV{ROCHEDB_CORE_DIR}/lib/librochedb.so")
else()
  find_library(ROCHEDB_C_LIBRARY NAMES rochedb librochedb)
endif()

if(ROCHEDB_CPP_BUILD_EXAMPLES)
  if(NOT ROCHEDB_C_LIBRARY)
    message(FATAL_ERROR "librochedb.so was not found. Set ROCHEDB_CORE_DIR or ROCHEDB_LIBRARY.")
  endif()

  add_executable(rochedb_cpp_contract_smoke examples/contract_smoke.cpp)
  target_link_libraries(rochedb_cpp_contract_smoke PRIVATE rochedb_cpp "${ROCHEDB_C_LIBRARY}")

  get_filename_component(ROCHEDB_C_LIBRARY_DIR "${ROCHEDB_C_LIBRARY}" DIRECTORY)
  set_target_properties(
    rochedb_cpp_contract_smoke
    PROPERTIES
      BUILD_RPATH "${ROCHEDB_C_LIBRARY_DIR}"
  )
endif()
