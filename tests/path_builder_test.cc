#include <catch2/catch_test_macros.hpp>

#include "cdbapi/path/naming_convention.h"
#include "cdbapi/path/path_builder.h"
#include "cdbapi/tiling/tile_address.h"

using cdbapi::path::IsValidCdbName;
using cdbapi::path::PathBuilder;
using cdbapi::tiling::FormatLod;
using cdbapi::tiling::Geocell;
using cdbapi::tiling::TileAddress;

TEST_CASE("LOD formatting", "[path]") {
  CHECK(FormatLod(0) == "lod_00");
  CHECK(FormatLod(1) == "lod_01");
  CHECK(FormatLod(23) == "lod_23");
  CHECK(FormatLod(-1) == "lod_neg01");
  CHECK(FormatLod(-10) == "lod_neg10");
}

TEST_CASE("PathBuilder coverage paths", "[path]") {
  PathBuilder builder("/cdb");

  SECTION("LOD 0: geocell only") {
    TileAddress addr{0, Geocell{32, -118}, 0, 0};
    auto path = builder.CoveragePath("elevation", addr, "tif");
    auto expected = std::filesystem::path("/cdb/coverages/elevation/lod_00/n32_w118.tif");
    CHECK(path == expected);
  }

  SECTION("LOD 1: with row/col") {
    TileAddress addr{1, Geocell{32, -118}, 1, 0};
    auto path = builder.CoveragePath("elevation", addr, "tif");
    auto expected = std::filesystem::path(
        "/cdb/coverages/elevation/lod_01/n32_w118_r1_c0.tif");
    CHECK(path == expected);
  }

  SECTION("Negative LOD") {
    TileAddress addr{-5, Geocell{0, 0}, 0, 0};
    auto path = builder.CoveragePath("imagery", addr, "jp2");
    auto expected = std::filesystem::path(
        "/cdb/coverages/imagery/lod_neg05/n00_e000.jp2");
    CHECK(path == expected);
  }
}

TEST_CASE("Global metadata dir", "[path]") {
  PathBuilder builder("/cdb");
  CHECK(builder.GlobalMetadataDir() == std::filesystem::path("/cdb/global_metadata"));
}

TEST_CASE("Valid CDB names", "[path]") {
  CHECK(IsValidCdbName("elevation"));
  CHECK(IsValidCdbName("my_dataset_01"));
  CHECK(IsValidCdbName("lod_00"));

  CHECK_FALSE(IsValidCdbName(""));
  CHECK_FALSE(IsValidCdbName("has space"));
  CHECK_FALSE(IsValidCdbName("has#hash"));
  CHECK_FALSE(IsValidCdbName("path/sep"));
  CHECK_FALSE(IsValidCdbName("col:on"));
}
