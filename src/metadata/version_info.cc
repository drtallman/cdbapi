#include "cdbapi/metadata/version_info.h"

#include <filesystem>

#include "pugixml.hpp"

namespace cdbapi::metadata {

auto WriteVersionXml(const std::filesystem::path& metadata_dir,
                     const VersionInfo& info) -> std::expected<void, Error> {
  pugi::xml_document doc;

  auto decl = doc.append_child(pugi::node_declaration);
  decl.append_attribute("version") = "1.0";
  decl.append_attribute("encoding") = "UTF-8";

  auto root = doc.append_child("Version");
  root.append_attribute("xmlns") =
      "http://opengis.net/cdb/2.0/Version.xsd";

  root.append_child("Specification")
      .append_child(pugi::node_pcdata)
      .set_value(info.specification.c_str());
  root.append_child("SpecificationVersion")
      .append_child(pugi::node_pcdata)
      .set_value(info.specification_version.c_str());
  root.append_child("DatastoreVersion")
      .append_child(pugi::node_pcdata)
      .set_value(std::to_string(info.datastore_version).c_str());

  // Links to sibling global metadata resources (OGC CDB 2.0 Links req class).
  auto links = root.append_child("Links");

  auto self_link = links.append_child("Link");
  self_link.append_attribute("href") = "Version.xml";
  self_link.append_attribute("rel") = "self";
  self_link.append_attribute("type") = "application/xml";
  self_link.append_attribute("title") = "This document";

  auto ds_link = links.append_child("Link");
  ds_link.append_attribute("href") = "Datasets.xml";
  ds_link.append_attribute("rel") = "datasets";
  ds_link.append_attribute("type") = "application/xml";
  ds_link.append_attribute("title") = "Dataset catalog";

  auto meta_link = links.append_child("Link");
  meta_link.append_attribute("href") = "Datastore_Metadata.xml";
  meta_link.append_attribute("rel") = "describedby";
  meta_link.append_attribute("type") = "application/xml";
  meta_link.append_attribute("title") = "Datastore metadata";

  auto crs_link = links.append_child("Link");
  crs_link.append_attribute("href") = "CRS.wkt";
  crs_link.append_attribute("rel") = "crs";
  crs_link.append_attribute("type") = "text/plain";
  crs_link.append_attribute("title") = "Coordinate Reference System (WKT2)";

  auto path = metadata_dir / "Version.xml";
  if (!doc.save_file(path.string().c_str())) {
    return std::unexpected(
        Error(ErrorCode::kIoError,
              "Failed to write " + path.string()));
  }

  return {};
}

}  // namespace cdbapi::metadata
