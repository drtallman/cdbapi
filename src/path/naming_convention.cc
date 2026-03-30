#include "cdbapi/path/naming_convention.h"

#include <algorithm>
#include <cctype>

namespace cdbapi::path {

auto ApplyNamingConvention(std::string_view name, NamingConvention convention)
    -> std::string {
  // For now, input is assumed to be in a neutral form (lowercase words
  // separated by underscores or spaces). Convert to the target convention.
  std::string result;

  switch (convention) {
    case NamingConvention::kSnakeCase: {
      for (char c : name) {
        if (c == ' ' || c == '-') {
          result += '_';
        } else {
          result += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
      }
      break;
    }
    case NamingConvention::kKebabCase: {
      for (char c : name) {
        if (c == ' ' || c == '_') {
          result += '-';
        } else {
          result += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
      }
      break;
    }
    case NamingConvention::kCamelCase:
    case NamingConvention::kPascalCase: {
      bool capitalize_next =
          (convention == NamingConvention::kPascalCase);
      for (char c : name) {
        if (c == ' ' || c == '_' || c == '-') {
          capitalize_next = true;
        } else if (capitalize_next) {
          result += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
          capitalize_next = false;
        } else {
          result += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
      }
      break;
    }
  }

  return result;
}

auto IsValidCdbName(std::string_view name) -> bool {
  if (name.empty()) return false;
  for (char c : name) {
    if (static_cast<unsigned char>(c) > 127) return false;  // Non-ASCII
    if (c == ' ') return false;
    // Prohibited: # % & { } \ < > * ? / $ ! ' " : @
    static constexpr std::string_view kProhibited = "#%&{}\\<>*?/$!'\":@";
    if (kProhibited.find(c) != std::string_view::npos) return false;
  }
  return true;
}

}  // namespace cdbapi::path
