#include "cdbapi/metadata/global_metadata.h"

#include <charconv>
#include <filesystem>
#include <fstream>

#include "cdbapi/crs/crs.h"
#include "cdbapi/metadata/version_info.h"
#include "pugixml.hpp"

namespace cdbapi::metadata {

namespace {

auto WriteDatastoreMetadataXml(const std::filesystem::path& metadata_dir,
                               const DatastoreMetadata& meta)
    -> std::expected<void, Error> {
  pugi::xml_document doc;

  auto decl = doc.append_child(pugi::node_declaration);
  decl.append_attribute("version") = "1.0";
  decl.append_attribute("encoding") = "UTF-8";

  auto root = doc.append_child("DatastoreMetadata");
  root.append_attribute("xmlns") =
      "http://opengis.net/cdb/2.0/DatastoreMetadata";

  root.append_child("Identifier")
      .append_child(pugi::node_pcdata)
      .set_value(meta.id.c_str());
  root.append_child("Title")
      .append_child(pugi::node_pcdata)
      .set_value(meta.title.c_str());
  root.append_child("Description")
      .append_child(pugi::node_pcdata)
      .set_value(meta.description.c_str());
  root.append_child("ContactPoint")
      .append_child(pugi::node_pcdata)
      .set_value(meta.contact_point.c_str());
  root.append_child("Created")
      .append_child(pugi::node_pcdata)
      .set_value(meta.created.c_str());
  root.append_child("Language")
      .append_child(pugi::node_pcdata)
      .set_value(meta.language.c_str());

  auto path = metadata_dir / "Datastore_Metadata.xml";
  if (!doc.save_file(path.string().c_str())) {
    return std::unexpected(
        Error(ErrorCode::kIoError,
              "Failed to write " + path.string()));
  }

  return {};
}

auto WriteCrsWkt(const std::filesystem::path& metadata_dir,
                 const ApplicationProfile& profile)
    -> std::expected<void, Error> {
  crs::Crs crs(profile.epsg_code, profile.vertical_epsg_code);
  std::string wkt = crs.ToWkt2();

  auto path = metadata_dir / "CRS.wkt";
  std::ofstream out(path);
  if (!out.is_open()) {
    return std::unexpected(
        Error(ErrorCode::kIoError,
              "Failed to open " + path.string() + " for writing"));
  }
  out << wkt;
  return {};
}

}  // namespace

auto WriteGlobalMetadata(const std::filesystem::path& metadata_dir,
                         const DatastoreMetadata& meta,
                         const ApplicationProfile& profile)
    -> std::expected<void, Error> {
  std::filesystem::create_directories(metadata_dir);

  // Write version.xml
  auto version_result = WriteVersionXml(metadata_dir, VersionInfo{});
  if (!version_result) return version_result;

  // Write CRS WKT
  auto crs_result = WriteCrsWkt(metadata_dir, profile);
  if (!crs_result) return crs_result;

  // Write datastore metadata
  auto meta_result = WriteDatastoreMetadataXml(metadata_dir, meta);
  if (!meta_result) return meta_result;

  return {};
}

auto ReadDatastoreMetadata(const std::filesystem::path& metadata_dir)
    -> std::expected<DatastoreMetadata, Error> {
  auto path = metadata_dir / "Datastore_Metadata.xml";
  pugi::xml_document doc;
  auto parse_result = doc.load_file(path.string().c_str());
  if (!parse_result) {
    return std::unexpected(
        Error(ErrorCode::kMetadataError,
              "Failed to parse " + path.string() + ": " +
              parse_result.description()));
  }

  auto root = doc.child("DatastoreMetadata");
  if (!root) {
    return std::unexpected(
        Error(ErrorCode::kMetadataError,
              "Missing DatastoreMetadata root element"));
  }

  DatastoreMetadata meta;
  meta.id = root.child_value("Identifier");
  meta.title = root.child_value("Title");
  meta.description = root.child_value("Description");
  meta.contact_point = root.child_value("ContactPoint");
  meta.created = root.child_value("Created");
  meta.language = root.child_value("Language");
  if (meta.language.empty()) meta.language = "en";

  return meta;
}

auto ReadCrsEpsg(const std::filesystem::path& metadata_dir)
    -> std::expected<int, Error> {
  auto path = metadata_dir / "CRS.wkt";
  std::ifstream in(path);
  if (!in.is_open()) {
    return std::unexpected(
        Error(ErrorCode::kIoError,
              "Failed to open " + path.string()));
  }

  std::string wkt((std::istreambuf_iterator<char>(in)),
                  std::istreambuf_iterator<char>());

  // Extract EPSG code from the AUTHORITY or ID at the end of the WKT.
  // Look for ID["EPSG",NNNN] pattern.
  auto pos = wkt.rfind("\"EPSG\"");
  if (pos == std::string::npos) {
    // Fallback: try AUTHORITY["EPSG","NNNN"]
    pos = wkt.rfind("AUTHORITY[\"EPSG\"");
  }
  if (pos == std::string::npos) {
    return 4326;  // Default assumption.
  }

  // Find the number after the EPSG string.
  auto comma = wkt.find(',', pos);
  if (comma == std::string::npos) return 4326;

  int epsg = 0;
  // Skip comma and any whitespace/quotes.
  size_t start = comma + 1;
  while (start < wkt.size() &&
         (wkt[start] == ' ' || wkt[start] == '"')) {
    ++start;
  }
  auto [ptr, ec] = std::from_chars(
      wkt.data() + start,
      wkt.data() + wkt.size(), epsg);
  if (ec != std::errc{}) return 4326;

  return epsg;
}

}  // namespace cdbapi::metadata
