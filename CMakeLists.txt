cmake_minimum_required(VERSION 3.25)
project(cdbapi VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Dependencies via vcpkg
find_package(GDAL CONFIG REQUIRED)
find_package(pugixml CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

# Library target
add_library(cdbapi)
target_include_directories(cdbapi
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_link_libraries(cdbapi
  PUBLIC
    GDAL::GDAL
  PRIVATE
    pugixml::pugixml
    nlohmann_json::nlohmann_json
)

# Sources
target_sources(cdbapi PRIVATE
  src/datastore.cc
  src/application_profile.cc
  src/tiling/cdb1_global_grid.cc
  src/tiling/geocell.cc
  src/tiling/coalescence.cc
  src/tiling/tile_address.cc
  src/crs/crs.cc
  src/path/path_builder.cc
  src/path/naming_convention.cc
  src/metadata/global_metadata.cc
  src/metadata/version_info.cc
  src/metadata/dataset_metadata.cc
  src/format/geotiff_writer.cc
  src/format/geopackage_writer.cc
  src/format/jpeg2000_writer.cc
  src/format/png_writer.cc
)

# Examples
option(CDBAPI_BUILD_EXAMPLES "Build examples" ON)
if(CDBAPI_BUILD_EXAMPLES)
  add_executable(create_cdb examples/create_cdb.cc)
  target_link_libraries(create_cdb PRIVATE cdbapi GDAL::GDAL)
  add_executable(gdal_test examples/gdal_test.cc)
  target_link_libraries(gdal_test PRIVATE cdbapi GDAL::GDAL)
  add_executable(roundtrip examples/roundtrip.cc)
  target_link_libraries(roundtrip PRIVATE cdbapi GDAL::GDAL)
endif()

# Tests
option(CDBAPI_BUILD_TESTS "Build tests" ON)
if(CDBAPI_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
