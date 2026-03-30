#include <catch2/catch_test_macros.hpp>

#include "cdbapi/tiling/coalescence.h"

using cdbapi::tiling::CoalescenceForLatitude;
using cdbapi::tiling::ZoneForLatitude;

TEST_CASE("Coalescence zone boundaries", "[coalescence]") {
  // Zone 1: [0, 60)
  CHECK(CoalescenceForLatitude(0.0) == 1);
  CHECK(CoalescenceForLatitude(30.0) == 1);
  CHECK(CoalescenceForLatitude(59.99) == 1);

  // Zone 2: [60, 70)
  CHECK(CoalescenceForLatitude(60.0) == 2);
  CHECK(CoalescenceForLatitude(65.0) == 2);
  CHECK(CoalescenceForLatitude(69.99) == 2);

  // Zone 3: [70, 75)
  CHECK(CoalescenceForLatitude(70.0) == 3);
  CHECK(CoalescenceForLatitude(74.99) == 3);

  // Zone 4: [75, 80)
  CHECK(CoalescenceForLatitude(75.0) == 4);
  CHECK(CoalescenceForLatitude(79.99) == 4);

  // Zone 5: [80, 89)
  CHECK(CoalescenceForLatitude(80.0) == 6);
  CHECK(CoalescenceForLatitude(88.99) == 6);

  // Zone 6: [89, 90]
  CHECK(CoalescenceForLatitude(89.0) == 12);
  CHECK(CoalescenceForLatitude(90.0) == 12);
}

TEST_CASE("Zone numbers", "[coalescence]") {
  CHECK(ZoneForLatitude(0.0) == 1);
  CHECK(ZoneForLatitude(59.99) == 1);
  CHECK(ZoneForLatitude(60.0) == 2);
  CHECK(ZoneForLatitude(70.0) == 3);
  CHECK(ZoneForLatitude(75.0) == 4);
  CHECK(ZoneForLatitude(80.0) == 5);
  CHECK(ZoneForLatitude(89.0) == 6);
}
