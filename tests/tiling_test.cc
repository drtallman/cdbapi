#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "cdbapi/tiling/cdb1_global_grid.h"

using cdbapi::tiling::Cdb1GlobalGrid;
using cdbapi::tiling::Geocell;
using cdbapi::tiling::TileAddress;

TEST_CASE("CDB1GlobalGrid LOD range", "[tiling]") {
  Cdb1GlobalGrid grid;
  auto [min_lod, max_lod] = grid.LodRange();
  CHECK(min_lod == -10);
  CHECK(max_lod == 23);
}

TEST_CASE("Tile pixel sizes", "[tiling]") {
  Cdb1GlobalGrid grid;

  SECTION("LOD 0 and above: 1024x1024") {
    CHECK(grid.TilePixelSize(0) == std::pair{1024, 1024});
    CHECK(grid.TilePixelSize(1) == std::pair{1024, 1024});
    CHECK(grid.TilePixelSize(5) == std::pair{1024, 1024});
    CHECK(grid.TilePixelSize(23) == std::pair{1024, 1024});
  }

  SECTION("Negative LODs: halving") {
    CHECK(grid.TilePixelSize(-1) == std::pair{512, 512});
    CHECK(grid.TilePixelSize(-2) == std::pair{256, 256});
    CHECK(grid.TilePixelSize(-3) == std::pair{128, 128});
    CHECK(grid.TilePixelSize(-5) == std::pair{32, 32});
    CHECK(grid.TilePixelSize(-10) == std::pair{1, 1});
  }
}

TEST_CASE("Tile extent at LOD 0", "[tiling]") {
  Cdb1GlobalGrid grid;

  SECTION("Equatorial geocell (no coalescence)") {
    TileAddress addr{0, Geocell{32, -118}, 0, 0};
    auto ext = grid.TileExtent(addr);
    CHECK(ext.south == 32.0);
    CHECK(ext.north == 33.0);
    CHECK(ext.west == -118.0);
    CHECK(ext.east == -117.0);
  }

  SECTION("High-latitude geocell (coalescence=2)") {
    TileAddress addr{0, Geocell{65, -118}, 0, 0};
    auto ext = grid.TileExtent(addr);
    CHECK(ext.south == 65.0);
    CHECK(ext.north == 66.0);
    CHECK(ext.west == -118.0);
    CHECK(ext.east == -116.0);  // 2 degrees wide
  }
}

TEST_CASE("Tile extent at LOD 1 (quad-tree)", "[tiling]") {
  Cdb1GlobalGrid grid;

  TileAddress addr{1, Geocell{32, -118}, 0, 0};
  auto ext = grid.TileExtent(addr);
  CHECK(ext.south == 32.0);
  CHECK(ext.north == 32.5);
  CHECK(ext.west == -118.0);
  CHECK(ext.east == -117.5);
}

TEST_CASE("TilesForBounds at LOD 0", "[tiling]") {
  Cdb1GlobalGrid grid;

  SECTION("Single geocell") {
    auto tiles = grid.TilesForBounds({32.0, -118.0, 33.0, -117.0}, 0);
    REQUIRE(tiles.size() == 1);
    CHECK(tiles[0].geocell.lat == 32);
    CHECK(tiles[0].geocell.lon == -118);
  }

  SECTION("Multiple geocells") {
    auto tiles = grid.TilesForBounds({32.0, -118.0, 34.0, -116.0}, 0);
    CHECK(tiles.size() == 4);
  }
}
