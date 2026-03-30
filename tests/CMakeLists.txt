find_package(Catch2 3 CONFIG REQUIRED)

list(APPEND CMAKE_MODULE_PATH ${Catch2_DIR})
include(Catch)

add_executable(cdbapi_tests
  coalescence_test.cc
  geocell_test.cc
  tiling_test.cc
  path_builder_test.cc
  smoke_test.cc
  coverage_write_test.cc
  format_writer_test.cc
  read_test.cc
)
target_link_libraries(cdbapi_tests
  PRIVATE
    cdbapi
    Catch2::Catch2WithMain
)

include(CTest)
catch_discover_tests(cdbapi_tests)
