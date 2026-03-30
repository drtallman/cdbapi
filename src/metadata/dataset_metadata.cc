#include "cdbapi/metadata/dataset_metadata.h"

#include "pugixml.hpp"

namespace cdbapi::metadata {

auto WriteDatasetsMetadata(const std::filesystem::path& metadata_dir,
                           const std::vector<DatasetInfo>& datasets)
    -> std::expected<void, Error> {
  pugi::xml_document doc;

  auto decl = doc.append_child(pugi::node_declaration);
  decl.append_attribute("version") = "1.0";
  decl.append_attribute("encoding") = "UTF-8";

  auto root = doc.append_child("Datasets");
  root.append_attribute("xmlns") =
      "http://opengis.net/cdb/2.0/Datasets";

  for (const auto& ds : datasets) {
    auto node = root.append_child("Dataset");
    node.append_child("Identifier")
        .append_child(pugi::node_pcdata)
        .set_value(ds.id.c_str());
    node.append_attribute("type") = "dataset";
    node.append_child("Title")
        .append_child(pugi::node_pcdata)
        .set_value(ds.title.c_str());
    node.append_child("Description")
        .append_child(pugi::node_pcdata)
        .set_value(ds.description.c_str());

    if (!ds.keywords.empty()) {
      auto kw_node = node.append_child("Keywords");
      for (const auto& kw : ds.keywords) {
        kw_node.append_child("Keyword")
            .append_child(pugi::node_pcdata)
            .set_value(kw.c_str());
      }
    }

    if (ds.created.has_value()) {
      node.append_child("Created")
          .append_child(pugi::node_pcdata)
          .set_value(ds.created.value().c_str());
    }

    if (ds.extent.has_value()) {
      auto ext = node.append_child("Extent");
      auto& b = ds.extent.value();
      ext.append_child("South")
          .append_child(pugi::node_pcdata)
          .set_value(std::to_string(b.south).c_str());
      ext.append_child("West")
          .append_child(pugi::node_pcdata)
          .set_value(std::to_string(b.west).c_str());
      ext.append_child("North")
          .append_child(pugi::node_pcdata)
          .set_value(std::to_string(b.north).c_str());
      ext.append_child("East")
          .append_child(pugi::node_pcdata)
          .set_value(std::to_string(b.east).c_str());
    }

    // Link to this dataset's coverage directory.
    auto links = node.append_child("Links");
    auto data_link = links.append_child("Link");
    std::string coverage_href = "../coverages/" + ds.id;
    data_link.append_attribute("href") = coverage_href.c_str();
    data_link.append_attribute("rel") = "data";
    data_link.append_attribute("type") = "application/octet-stream";
    data_link.append_attribute("title") =
        ("Coverage tiles for " + ds.id).c_str();
  }

  auto path = metadata_dir / "Datasets.xml";
  if (!doc.save_file(path.string().c_str())) {
    return std::unexpected(
        Error(ErrorCode::kIoError,
              "Failed to write " + path.string()));
  }

  return {};
}

auto ReadDatasetsMetadata(const std::filesystem::path& metadata_dir)
    -> std::expected<std::vector<DatasetInfo>, Error> {
  auto path = metadata_dir / "Datasets.xml";
  pugi::xml_document doc;
  auto parse_result = doc.load_file(path.string().c_str());
  if (!parse_result) {
    return std::unexpected(
        Error(ErrorCode::kMetadataError,
              "Failed to parse " + path.string() + ": " +
              parse_result.description()));
  }

  auto root = doc.child("Datasets");
  if (!root) {
    return std::unexpected(
        Error(ErrorCode::kMetadataError,
              "Missing Datasets root element"));
  }

  std::vector<DatasetInfo> datasets;
  for (auto node = root.child("Dataset"); node;
       node = node.next_sibling("Dataset")) {
    DatasetInfo ds;
    ds.id = node.child_value("Identifier");
    ds.title = node.child_value("Title");
    ds.description = node.child_value("Description");

    auto created = node.child("Created");
    if (created) {
      ds.created = created.child_value();
    }

    auto kw_node = node.child("Keywords");
    if (kw_node) {
      for (auto kw = kw_node.child("Keyword"); kw;
           kw = kw.next_sibling("Keyword")) {
        ds.keywords.push_back(kw.child_value());
      }
    }

    auto ext_node = node.child("Extent");
    if (ext_node) {
      tiling::GeoBounds bounds{};
      bounds.south = ext_node.child("South").text().as_double();
      bounds.west = ext_node.child("West").text().as_double();
      bounds.north = ext_node.child("North").text().as_double();
      bounds.east = ext_node.child("East").text().as_double();
      ds.extent = bounds;
    }

    datasets.push_back(std::move(ds));
  }

  return datasets;
}

}  // namespace cdbapi::metadata
