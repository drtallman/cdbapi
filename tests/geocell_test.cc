#include <catch2/catch_test_macros.hpp>

#include "cdbapi/tiling/geocell.h"

using cdbapi::tiling::FormatGeocell;
using cdbapi::tiling::GeocellFromCoordinate;
using cdbapi::tiling::Geocell;
using cdbapi::tiling::IsValid;

TEST_CASE("Geocell from coordinate", "[geocell]") {
  SECTION("Positive lat/lon") {
    auto cell = GeocellFromCoordinate(32.7, -117.2);
    CHECK(cell.lat == 32);
    CHECK(cell.lon == -118);
  }

  SECTION("Negative lat/lon") {
    auto cell = GeocellFromCoordinate(-33.9, 18.4);
    CHECK(cell.lat == -34);
    CHECK(cell.lon == 18);
  }

  SECTION("Exact integer coordinates") {
    auto cell = GeocellFromCoordinate(45.0, -90.0);
    CHECK(cell.lat == 45);
    CHECK(cell.lon == -90);
  }

  SECTION("Edge: north pole clamped") {
    auto cell = GeocellFromCoordinate(90.0, 0.0);
    CHECK(cell.lat == 89);
  }

  SECTION("Edge: south pole") {
    auto cell = GeocellFromCoordinate(-90.0, 0.0);
    CHECK(cell.lat == -90);
  }

  SECTION("Edge: antimeridian") {
    auto cell = GeocellFromCoordinate(0.0, 180.0);
    CHECK(cell.lon == 179);

    auto cell2 = GeocellFromCoordinate(0.0, -180.0);
    CHECK(cell2.lon == -180);
  }
}

TEST_CASE("Geocell formatting", "[geocell]") {
  CHECK(FormatGeocell(Geocell{32, -118}) == "n32_w118");
  CHECK(FormatGeocell(Geocell{-1, 5}) == "s01_e005");
  CHECK(FormatGeocell(Geocell{0, 0}) == "n00_e000");
  CHECK(FormatGeocell(Geocell{89, 179}) == "n89_e179");
  CHECK(FormatGeocell(Geocell{-90, -180}) == "s90_w180");
}

TEST_CASE("Geocell validity", "[geocell]") {
  CHECK(IsValid(Geocell{0, 0}));
  CHECK(IsValid(Geocell{89, 179}));
  CHECK(IsValid(Geocell{-90, -180}));
  CHECK_FALSE(IsValid(Geocell{90, 0}));
  CHECK_FALSE(IsValid(Geocell{0, 180}));
  CHECK_FALSE(IsValid(Geocell{-91, 0}));
}
