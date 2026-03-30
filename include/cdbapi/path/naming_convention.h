#pragma once

#include <string>
#include <string_view>

#include "cdbapi/application_profile.h"

namespace cdbapi::path {

// Converts a name to the given naming convention.
auto ApplyNamingConvention(std::string_view name, NamingConvention convention)
    -> std::string;

// Returns true if the string contains only valid CDB path characters
// (ASCII, no spaces, no prohibited characters).
auto IsValidCdbName(std::string_view name) -> bool;

}  // namespace cdbapi::path
