#pragma once

#include <optional>
#include <string>

namespace cdbapi::link {

// A CDB link, per the OGC CDB 2.0 Core Links requirements class.
struct Link {
  std::string href;                    // URL (mandatory)
  std::string rel;                     // Relation type (mandatory)
  std::optional<std::string> type;     // Media type hint
  std::optional<std::string> title;    // Human-readable title
};

}  // namespace cdbapi::link
