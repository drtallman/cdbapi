#include <catch2/catch_test_macros.hpp>

#include "cdbapi/cdbapi.h"

TEST_CASE("Library headers compile", "[smoke]") {
  // Verify that the umbrella header compiles and key types are usable.
  cdbapi::ApplicationProfile profile = cdbapi::DefaultProfile();
  REQUIRE(profile.epsg_code == 4326);
  REQUIRE(profile.name == "cdbapi_default_v1");
  REQUIRE(profile.tiling == cdbapi::TilingSchemeType::kCdb1GlobalGrid);
  REQUIRE(profile.naming == cdbapi::NamingConvention::kSnakeCase);
  REQUIRE(profile.uom == cdbapi::UnitOfMeasure::kMeters);
}
