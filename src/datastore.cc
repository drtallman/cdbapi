#include "cdbapi/datastore.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <format>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <utility>

#include "cdbapi/crs/crs.h"
#include "cdbapi/format/geopackage_writer.h"
#include "cdbapi/format/geotiff_writer.h"
#include "cdbapi/format/jpeg2000_writer.h"
#include "cdbapi/format/png_writer.h"
#include "cdbapi/metadata/global_metadata.h"
#include "cdbapi/path/path_builder.h"
#include "cdbapi/tiling/cdb1_global_grid.h"

#include "gdal.h"

namespace cdbapi {

struct CdbDatastore::Impl {
  std::filesystem::path root;
  ApplicationProfile profile;
  metadata::DatastoreMetadata meta;
  path::PathBuilder path_builder;
  std::vector<metadata::DatasetInfo> datasets;

  Impl(std::filesystem::path r, ApplicationProfile p,
       metadata::DatastoreMetadata m)
      : root(std::move(r)),
        profile(std::move(p)),
        meta(std::move(m)),
        path_builder(root, profile.naming) {}
};

CdbDatastore::CdbDatastore() = default;
CdbDatastore::~CdbDatastore() = default;
CdbDatastore::CdbDatastore(CdbDatastore&&) noexcept = default;
auto CdbDatastore::operator=(CdbDatastore&&) noexcept
    -> CdbDatastore& = default;

auto CdbDatastore::root() const -> const std::filesystem::path& {
  return impl_->root;
}

auto CdbDatastore::profile() const -> const ApplicationProfile& {
  return impl_->profile;
}

auto CdbDatastore::Create(const std::filesystem::path& root,
                          const ApplicationProfile& profile,
                          const metadata::DatastoreMetadata& meta)
    -> std::expected<CdbDatastore, Error> {
  if (root.empty()) {
    return std::unexpected(
        Error(ErrorCode::kInvalidPath, "Root path cannot be empty"));
  }

  // Create directory structure.
  std::error_code ec;
  std::filesystem::create_directories(root, ec);
  if (ec) {
    return std::unexpected(
        Error(ErrorCode::kIoError,
              "Failed to create root directory: " + ec.message()));
  }

  CdbDatastore ds;
  ds.impl_ = std::make_unique<Impl>(root, profile, meta);

  // Write global metadata.
  auto metadata_dir = ds.impl_->path_builder.GlobalMetadataDir();
  auto result = metadata::WriteGlobalMetadata(metadata_dir, meta, profile);
  if (!result) {
    return std::unexpected(result.error());
  }

  return ds;
}

auto CdbDatastore::Open(const std::filesystem::path& root)
    -> std::expected<CdbDatastore, Error> {
  if (root.empty() || !std::filesystem::exists(root)) {
    return std::unexpected(
        Error(ErrorCode::kInvalidPath,
              "Datastore root does not exist: " + root.string()));
  }

  auto metadata_dir = root / "global_metadata";
  if (!std::filesystem::exists(metadata_dir)) {
    return std::unexpected(
        Error(ErrorCode::kNotFound,
              "Missing global_metadata directory"));
  }

  // Read datastore metadata.
  auto meta_result = metadata::ReadDatastoreMetadata(metadata_dir);
  if (!meta_result) return std::unexpected(meta_result.error());

  // Read CRS EPSG code.
  auto epsg_result = metadata::ReadCrsEpsg(metadata_dir);
  if (!epsg_result) return std::unexpected(epsg_result.error());

  // Read datasets.
  auto ds_result = metadata::ReadDatasetsMetadata(metadata_dir);
  if (!ds_result) return std::unexpected(ds_result.error());

  // Reconstruct a default profile with the recovered EPSG code.
  ApplicationProfile profile = DefaultProfile();
  profile.epsg_code = *epsg_result;

  CdbDatastore ds;
  ds.impl_ = std::make_unique<Impl>(root, profile, *meta_result);
  ds.impl_->datasets = std::move(*ds_result);

  return ds;
}

namespace {

// Extracts the pixel sub-rectangle from `src` that corresponds to
// `tile_bounds`, given that `src` covers `src_bounds`.
auto ExtractTilePixels(
    const coverage::CoverageData& src,
    const tiling::GeoBounds& src_bounds,
    const tiling::GeoBounds& tile_bounds,
    int tile_width, int tile_height) -> std::vector<std::byte> {
  int bps = coverage::BytesPerSample(src.pixel_type);
  int bpp = bps * src.band_count;

  double src_pixel_w = (src_bounds.east - src_bounds.west) / src.width;
  double src_pixel_h = (src_bounds.north - src_bounds.south) / src.height;

  int src_col = static_cast<int>(
      std::round((tile_bounds.west - src_bounds.west) / src_pixel_w));
  int src_row = static_cast<int>(
      std::round((src_bounds.north - tile_bounds.north) / src_pixel_h));

  std::vector<std::byte> out(
      static_cast<size_t>(tile_width) * tile_height * bpp);

  for (int r = 0; r < tile_height; ++r) {
    auto src_offset =
        static_cast<size_t>((src_row + r) * src.width + src_col) * bpp;
    auto dst_offset = static_cast<size_t>(r) * tile_width * bpp;
    std::memcpy(out.data() + dst_offset,
                src.pixels.data() + src_offset,
                static_cast<size_t>(tile_width) * bpp);
  }
  return out;
}

// --- LOD Pyramid Helpers ---

// Pixel data read back from a GeoTIFF tile.
struct TilePixels {
  int width;
  int height;
  int band_count;
  coverage::PixelType pixel_type;
  double nodata;
  std::vector<std::byte> pixels;  // BIP layout
};

auto ToPixelType(GDALDataType type) -> std::optional<coverage::PixelType> {
  switch (type) {
    case GDT_Float32: return coverage::PixelType::kFloat32;
    case GDT_Float64: return coverage::PixelType::kFloat64;
    case GDT_Int16:   return coverage::PixelType::kInt16;
    case GDT_Int32:   return coverage::PixelType::kInt32;
    case GDT_Byte:    return coverage::PixelType::kUint8;
    case GDT_UInt16:  return coverage::PixelType::kUint16;
    default: return std::nullopt;
  }
}

// Reads a GeoTIFF tile into a BIP pixel buffer.
auto ReadGeoTiff(const std::filesystem::path& path)
    -> std::expected<TilePixels, Error> {
  GDALAllRegister();
  GDALDatasetH ds = GDALOpen(path.string().c_str(), GA_ReadOnly);
  if (!ds) {
    return std::unexpected(Error(ErrorCode::kGdalError,
        std::format("Failed to open: {}", path.string())));
  }

  int w = GDALGetRasterXSize(ds);
  int h = GDALGetRasterYSize(ds);
  int bands = GDALGetRasterCount(ds);

  GDALRasterBandH band1 = GDALGetRasterBand(ds, 1);
  GDALDataType gdal_type = GDALGetRasterDataType(band1);
  auto pt = ToPixelType(gdal_type);
  if (!pt) {
    GDALClose(ds);
    return std::unexpected(Error(ErrorCode::kGdalError,
        std::format("Unsupported pixel type in: {}", path.string())));
  }

  int has_nodata = 0;
  double nodata = GDALGetRasterNoDataValue(band1, &has_nodata);
  if (!has_nodata) nodata = -32767.0;

  int bps = coverage::BytesPerSample(*pt);
  std::vector<std::byte> pixels(
      static_cast<size_t>(w) * h * bands * bps);

  // Read each band into BIP layout using stride parameters.
  int pixel_stride = bands * bps;
  int line_stride = w * bands * bps;

  for (int b = 0; b < bands; ++b) {
    GDALRasterBandH rband = GDALGetRasterBand(ds, b + 1);
    auto* buf = static_cast<void*>(pixels.data() + b * bps);
    CPLErr err = GDALRasterIO(rband, GF_Read,
        0, 0, w, h, buf, w, h, gdal_type,
        pixel_stride, line_stride);
    if (err != CE_None) {
      GDALClose(ds);
      return std::unexpected(Error(ErrorCode::kGdalError,
          std::format("Failed to read band {} from: {}",
                      b + 1, path.string())));
    }
  }

  GDALClose(ds);
  return TilePixels{w, h, bands, *pt, nodata, std::move(pixels)};
}

// Box-filter downsample 2:1 with nodata-aware averaging.
template <typename T>
void BoxDownsampleTyped(
    const std::byte* src, int src_w, int src_h,
    std::byte* dst, int dst_w, int dst_h,
    int band_count, double nodata) {
  T nodata_val = static_cast<T>(nodata);
  const T* sp = reinterpret_cast<const T*>(src);
  T* dp = reinterpret_cast<T*>(dst);
  for (int r = 0; r < dst_h; ++r) {
    for (int c = 0; c < dst_w; ++c) {
      for (int b = 0; b < band_count; ++b) {
        double sum = 0.0;
        int count = 0;
        for (int dr = 0; dr < 2; ++dr) {
          for (int dc = 0; dc < 2; ++dc) {
            int si =
                ((r * 2 + dr) * src_w + (c * 2 + dc)) * band_count + b;
            T val = sp[si];
            if (val != nodata_val) {
              sum += static_cast<double>(val);
              ++count;
            }
          }
        }
        int di = (r * dst_w + c) * band_count + b;
        dp[di] = (count > 0)
            ? static_cast<T>(sum / count)
            : nodata_val;
      }
    }
  }
}

auto DownsampleBox(
    const std::byte* src, int src_w, int src_h,
    int dst_w, int dst_h,
    int band_count, coverage::PixelType pt, double nodata)
    -> std::vector<std::byte> {
  int bps = coverage::BytesPerSample(pt);
  std::vector<std::byte> dst(
      static_cast<size_t>(dst_w) * dst_h * band_count * bps);
  switch (pt) {
    case coverage::PixelType::kFloat32:
      BoxDownsampleTyped<float>(src, src_w, src_h, dst.data(),
          dst_w, dst_h, band_count, nodata); break;
    case coverage::PixelType::kFloat64:
      BoxDownsampleTyped<double>(src, src_w, src_h, dst.data(),
          dst_w, dst_h, band_count, nodata); break;
    case coverage::PixelType::kInt16:
      BoxDownsampleTyped<int16_t>(src, src_w, src_h, dst.data(),
          dst_w, dst_h, band_count, nodata); break;
    case coverage::PixelType::kInt32:
      BoxDownsampleTyped<int32_t>(src, src_w, src_h, dst.data(),
          dst_w, dst_h, band_count, nodata); break;
    case coverage::PixelType::kUint8:
      BoxDownsampleTyped<uint8_t>(src, src_w, src_h, dst.data(),
          dst_w, dst_h, band_count, nodata); break;
    case coverage::PixelType::kUint16:
      BoxDownsampleTyped<uint16_t>(src, src_w, src_h, dst.data(),
          dst_w, dst_h, band_count, nodata); break;
  }
  return dst;
}

// Fills a BIP buffer with a nodata value for every sample.
template <typename T>
void FillNodataTyped(std::byte* buf, size_t total_samples, double nodata) {
  T val = static_cast<T>(nodata);
  T* p = reinterpret_cast<T*>(buf);
  std::fill(p, p + total_samples, val);
}

void FillNodata(std::byte* buf, size_t pixel_count, int band_count,
                coverage::PixelType pt, double nodata) {
  size_t total = pixel_count * static_cast<size_t>(band_count);
  switch (pt) {
    case coverage::PixelType::kFloat32:
      FillNodataTyped<float>(buf, total, nodata); break;
    case coverage::PixelType::kFloat64:
      FillNodataTyped<double>(buf, total, nodata); break;
    case coverage::PixelType::kInt16:
      FillNodataTyped<int16_t>(buf, total, nodata); break;
    case coverage::PixelType::kInt32:
      FillNodataTyped<int32_t>(buf, total, nodata); break;
    case coverage::PixelType::kUint8:
      FillNodataTyped<uint8_t>(buf, total, nodata); break;
    case coverage::PixelType::kUint16:
      FillNodataTyped<uint16_t>(buf, total, nodata); break;
  }
}

// Assembles up to 4 child tiles into a 2W x 2H buffer.
// children[0]=SW, [1]=SE, [2]=NW, [3]=NE.
// In pixel space (row 0 = north): NW is top-left, NE is top-right,
// SW is bottom-left, SE is bottom-right.
// Missing children (nullptr) are filled with nodata.
auto AssembleChildTiles(
    const TilePixels* children[4],
    int tile_w, int tile_h,
    int band_count, coverage::PixelType pt, double nodata)
    -> std::vector<std::byte> {
  int out_w = tile_w * 2;
  int out_h = tile_h * 2;
  int bps = coverage::BytesPerSample(pt);
  int bpp = bps * band_count;

  std::vector<std::byte> out(
      static_cast<size_t>(out_w) * out_h * bpp);
  FillNodata(out.data(), static_cast<size_t>(out_w) * out_h,
             band_count, pt, nodata);

  // Pixel-space offsets for each quadrant (row_mult, col_mult).
  struct Quad { int row_mult; int col_mult; };
  static constexpr Quad kQuads[4] = {
    {1, 0},  // [0] SW -> bottom-left
    {1, 1},  // [1] SE -> bottom-right
    {0, 0},  // [2] NW -> top-left
    {0, 1},  // [3] NE -> top-right
  };

  for (int i = 0; i < 4; ++i) {
    if (!children[i]) continue;
    int row_off = kQuads[i].row_mult * tile_h;
    int col_off = kQuads[i].col_mult * tile_w;
    const auto& src = children[i]->pixels;
    for (int r = 0; r < tile_h; ++r) {
      auto src_off = static_cast<size_t>(r) * tile_w * bpp;
      auto dst_off =
          static_cast<size_t>(row_off + r) * out_w * bpp +
          static_cast<size_t>(col_off) * bpp;
      std::memcpy(out.data() + dst_off, src.data() + src_off,
                  static_cast<size_t>(tile_w) * bpp);
    }
  }

  return out;
}

// Returns true if the extension is a supported raster format.
auto IsSupportedRasterExt(const std::string& ext) -> bool {
  return ext == ".tif" || ext == ".tiff" || ext == ".gpkg" ||
         ext == ".jp2" || ext == ".png";
}

// Collects unique geocells from raster filenames in a LOD directory.
auto CollectGeocells(const std::filesystem::path& dir)
    -> std::set<tiling::Geocell> {
  std::set<tiling::Geocell> geocells;
  if (!std::filesystem::exists(dir)) return geocells;

  for (const auto& entry : std::filesystem::directory_iterator(dir)) {
    if (!entry.is_regular_file()) continue;
    if (!IsSupportedRasterExt(entry.path().extension().string()))
      continue;

    auto stem = entry.path().stem().string();
    if (stem.size() < 8) continue;

    char lat_ch = stem[0];
    char lon_ch = stem[4];
    if ((lat_ch != 'n' && lat_ch != 's') ||
        (lon_ch != 'e' && lon_ch != 'w') ||
        stem[3] != '_') continue;

    int abs_lat = (stem[1] - '0') * 10 + (stem[2] - '0');
    int abs_lon =
        (stem[5] - '0') * 100 + (stem[6] - '0') * 10 + (stem[7] - '0');

    int lat = (lat_ch == 's') ? -abs_lat : abs_lat;
    int lon = (lon_ch == 'w') ? -abs_lon : abs_lon;
    geocells.insert(tiling::Geocell{lat, lon});
  }

  return geocells;
}

// Parses a TileAddress from a filename stem (e.g. "n32_w118_r0_c1").
auto ParseTileAddress(const std::string& stem, int lod)
    -> std::optional<tiling::TileAddress> {
  if (stem.size() < 8) return std::nullopt;

  char lat_ch = stem[0];
  char lon_ch = stem[4];
  if ((lat_ch != 'n' && lat_ch != 's') ||
      (lon_ch != 'e' && lon_ch != 'w') ||
      stem[3] != '_') return std::nullopt;

  int abs_lat = (stem[1] - '0') * 10 + (stem[2] - '0');
  int abs_lon =
      (stem[5] - '0') * 100 + (stem[6] - '0') * 10 + (stem[7] - '0');

  int lat = (lat_ch == 's') ? -abs_lat : abs_lat;
  int lon = (lon_ch == 'w') ? -abs_lon : abs_lon;
  tiling::Geocell geocell{lat, lon};

  if (lod <= 0) {
    return tiling::TileAddress{lod, geocell, 0, 0};
  }

  // Parse _rN_cN suffix.
  if (stem.size() < 12 || stem[8] != '_' || stem[9] != 'r')
    return std::nullopt;

  auto c_pos = stem.find("_c", 10);
  if (c_pos == std::string::npos) return std::nullopt;

  int row = 0, col = 0;
  auto [p1, e1] = std::from_chars(
      stem.data() + 10, stem.data() + c_pos, row);
  if (e1 != std::errc{}) return std::nullopt;
  auto [p2, e2] = std::from_chars(
      stem.data() + c_pos + 2, stem.data() + stem.size(), col);
  if (e2 != std::errc{}) return std::nullopt;

  return tiling::TileAddress{lod, geocell, row, col};
}

// Returns a writer and file extension for the given coverage encoding.
auto MakeWriter(CoverageEncoding encoding)
    -> std::pair<std::unique_ptr<coverage::CoverageWriter>, std::string> {
  switch (encoding) {
    case CoverageEncoding::kGeoTiff:
      return {std::make_unique<format::GeoTiffWriter>(), "tif"};
    case CoverageEncoding::kGeoPackage:
      return {std::make_unique<format::GeoPackageWriter>(), "gpkg"};
    case CoverageEncoding::kJpeg2000:
      return {std::make_unique<format::Jpeg2000Writer>(), "jp2"};
    case CoverageEncoding::kPng:
      return {std::make_unique<format::PngWriter>(), "png"};
    default:
      return {std::make_unique<format::GeoTiffWriter>(), "tif"};
  }
}

}  // namespace

auto CdbDatastore::WriteCoverage(
    const coverage::CoverageDescriptor& desc,
    const coverage::CoverageData& data) -> std::expected<void, Error> {
  // Validate inputs.
  if (desc.dataset_name.empty()) {
    return std::unexpected(
        Error(ErrorCode::kInvalidArgument, "Dataset name cannot be empty"));
  }
  if (desc.bounds.south >= desc.bounds.north ||
      desc.bounds.west >= desc.bounds.east) {
    return std::unexpected(
        Error(ErrorCode::kInvalidArgument, "Invalid geographic bounds"));
  }
  if (data.width <= 0 || data.height <= 0) {
    return std::unexpected(
        Error(ErrorCode::kInvalidArgument, "Invalid pixel dimensions"));
  }
  int bps = coverage::BytesPerSample(data.pixel_type);
  auto expected_size =
      static_cast<size_t>(data.width) * data.height * data.band_count * bps;
  if (data.pixels.size() != expected_size) {
    return std::unexpected(
        Error(ErrorCode::kInvalidArgument,
              std::format("Pixel buffer size {} does not match expected {}",
                          data.pixels.size(), expected_size)));
  }

  // Create tiling scheme and enumerate tiles.
  tiling::Cdb1GlobalGrid grid;
  auto [min_lod, max_lod] = grid.LodRange();
  if (desc.target_lod < min_lod || desc.target_lod > max_lod) {
    return std::unexpected(
        Error(ErrorCode::kInvalidLod,
              std::format("LOD {} is outside valid range [{}, {}]",
                          desc.target_lod, min_lod, max_lod)));
  }

  auto tiles = grid.TilesForBounds(desc.bounds, desc.target_lod);
  if (tiles.empty()) {
    return std::unexpected(
        Error(ErrorCode::kInvalidArgument,
              "No tiles intersect the given bounds"));
  }

  crs::Crs crs(impl_->profile.epsg_code, impl_->profile.vertical_epsg_code);
  auto [writer, ext] = MakeWriter(impl_->profile.default_coverage_encoding);

  for (const auto& addr : tiles) {
    auto tile_extent = grid.TileExtent(addr);
    auto [tile_w, tile_h] = grid.TilePixelSize(desc.target_lod);

    // Build output path and create directories.
    auto path = impl_->path_builder.CoveragePath(
        desc.dataset_name, addr, ext);
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) {
      return std::unexpected(
          Error(ErrorCode::kIoError,
                std::format("Failed to create directory: {}", ec.message())));
    }

    // Single-tile fast path: skip extraction when data matches exactly.
    if (tiles.size() == 1 &&
        data.width == tile_w && data.height == tile_h) {
      auto result = writer->WriteTile(
          path, data.pixels, tile_w, tile_h, data.band_count,
          data.pixel_type, tile_extent, desc.metadata, crs);
      if (!result) return result;
    } else {
      auto tile_pixels = ExtractTilePixels(
          data, desc.bounds, tile_extent, tile_w, tile_h);
      auto result = writer->WriteTile(
          path, tile_pixels, tile_w, tile_h, data.band_count,
          data.pixel_type, tile_extent, desc.metadata, crs);
      if (!result) return result;
    }
  }

  // Register dataset if not already present.
  auto it = std::find_if(
      impl_->datasets.begin(), impl_->datasets.end(),
      [&](const metadata::DatasetInfo& d) {
        return d.id == desc.dataset_name;
      });
  if (it == impl_->datasets.end()) {
    impl_->datasets.push_back(metadata::DatasetInfo{
        .id = desc.dataset_name,
        .title = desc.dataset_name,
        .description = desc.metadata.quantity_definition,
        .extent = desc.bounds,
    });
  }

  return {};
}

auto CdbDatastore::Finalize() -> std::expected<void, Error> {
  // Write datasets metadata.
  auto metadata_dir = impl_->path_builder.GlobalMetadataDir();
  return metadata::WriteDatasetsMetadata(metadata_dir, impl_->datasets);
}

auto CdbDatastore::GenerateLodPyramid(
    const std::string& dataset,
    int from_lod, int to_lod) -> std::expected<void, Error> {
  tiling::Cdb1GlobalGrid grid;
  auto [min_lod, max_lod] = grid.LodRange();

  if (from_lod < min_lod || from_lod > max_lod) {
    return std::unexpected(Error(ErrorCode::kInvalidLod,
        std::format("from_lod {} is outside valid range [{}, {}]",
                    from_lod, min_lod, max_lod)));
  }
  if (to_lod < min_lod || to_lod > max_lod) {
    return std::unexpected(Error(ErrorCode::kInvalidLod,
        std::format("to_lod {} is outside valid range [{}, {}]",
                    to_lod, min_lod, max_lod)));
  }
  if (to_lod >= from_lod) {
    return std::unexpected(Error(ErrorCode::kInvalidArgument,
        "to_lod must be less than from_lod"));
  }

  auto src_dir = impl_->path_builder.CoverageLodDir(dataset, from_lod);
  if (!std::filesystem::exists(src_dir)) {
    return std::unexpected(Error(ErrorCode::kNotFound,
        std::format("No tiles at LOD {} for dataset '{}'",
                    from_lod, dataset)));
  }

  crs::Crs crs(impl_->profile.epsg_code, impl_->profile.vertical_epsg_code);
  auto [writer, ext] = MakeWriter(impl_->profile.default_coverage_encoding);

  for (int src_lod = from_lod; src_lod > to_lod; --src_lod) {
    int dst_lod = src_lod - 1;
    auto src_lod_dir =
        impl_->path_builder.CoverageLodDir(dataset, src_lod);

    if (!std::filesystem::exists(src_lod_dir)) {
      return std::unexpected(Error(ErrorCode::kNotFound,
          std::format("No tiles found at LOD {} for dataset '{}'",
                      src_lod, dataset)));
    }

    if (src_lod >= 1) {
      // Positive LOD: group children by parent, merge 2x2, downsample.
      struct ParentGroup {
        std::optional<std::filesystem::path> children[4];
      };
      std::map<tiling::TileAddress, ParentGroup> groups;

      for (const auto& entry :
           std::filesystem::directory_iterator(src_lod_dir)) {
        if (!entry.is_regular_file()) continue;
        if (!IsSupportedRasterExt(entry.path().extension().string()))
          continue;

        auto stem = entry.path().stem().string();
        auto addr = ParseTileAddress(stem, src_lod);
        if (!addr) continue;

        // Compute parent address at dst_lod.
        int pr = (dst_lod >= 1) ? addr->tile_row / 2 : 0;
        int pc = (dst_lod >= 1) ? addr->tile_col / 2 : 0;
        tiling::TileAddress parent{dst_lod, addr->geocell, pr, pc};

        // Quadrant index: 0=SW, 1=SE, 2=NW, 3=NE.
        int qr = addr->tile_row % 2;  // 0=south, 1=north
        int qc = addr->tile_col % 2;  // 0=west, 1=east
        int idx = qr * 2 + qc;

        groups[parent].children[idx] = entry.path();
      }

      for (auto& [parent_addr, group] : groups) {
        std::optional<TilePixels> child_data[4];
        const TilePixels* child_ptrs[4] = {};

        for (int i = 0; i < 4; ++i) {
          if (!group.children[i]) continue;
          auto result = ReadGeoTiff(*group.children[i]);
          if (!result) return std::unexpected(result.error());
          child_data[i] = std::move(*result);
          child_ptrs[i] = &*child_data[i];
        }

        // Find first available child for reference metadata.
        const TilePixels* ref = nullptr;
        for (int i = 0; i < 4; ++i) {
          if (child_ptrs[i]) { ref = child_ptrs[i]; break; }
        }
        if (!ref) continue;

        auto mosaic = AssembleChildTiles(
            child_ptrs, ref->width, ref->height,
            ref->band_count, ref->pixel_type, ref->nodata);

        auto downsampled = DownsampleBox(
            mosaic.data(), ref->width * 2, ref->height * 2,
            ref->width, ref->height,
            ref->band_count, ref->pixel_type, ref->nodata);

        auto parent_path = impl_->path_builder.CoveragePath(
            dataset, parent_addr, ext);
        std::error_code ec;
        std::filesystem::create_directories(parent_path.parent_path(), ec);
        if (ec) {
          return std::unexpected(Error(ErrorCode::kIoError,
              std::format("Failed to create directory: {}",
                          ec.message())));
        }

        auto parent_extent = grid.TileExtent(parent_addr);
        coverage::CoverageMetadata meta{.data_null = ref->nodata};

        auto result = writer->WriteTile(
            parent_path, downsampled, ref->width, ref->height,
            ref->band_count, ref->pixel_type,
            parent_extent, meta, crs);
        if (!result) return result;
      }
    } else {
      // Negative LOD: downsample single tile per geocell.
      auto geocells = CollectGeocells(src_lod_dir);

      for (const auto& geocell : geocells) {
        tiling::TileAddress src_addr{src_lod, geocell, 0, 0};
        auto src_path = impl_->path_builder.CoveragePath(
            dataset, src_addr, ext);
        if (!std::filesystem::exists(src_path)) continue;

        auto tile = ReadGeoTiff(src_path);
        if (!tile) return std::unexpected(tile.error());

        auto [dst_w, dst_h] = grid.TilePixelSize(dst_lod);
        auto downsampled = DownsampleBox(
            tile->pixels.data(), tile->width, tile->height,
            dst_w, dst_h,
            tile->band_count, tile->pixel_type, tile->nodata);

        tiling::TileAddress dst_addr{dst_lod, geocell, 0, 0};
        auto dst_path = impl_->path_builder.CoveragePath(
            dataset, dst_addr, ext);
        std::error_code ec;
        std::filesystem::create_directories(dst_path.parent_path(), ec);
        if (ec) {
          return std::unexpected(Error(ErrorCode::kIoError,
              std::format("Failed to create directory: {}",
                          ec.message())));
        }

        auto dst_extent = grid.TileExtent(dst_addr);
        coverage::CoverageMetadata meta{.data_null = tile->nodata};

        auto result = writer->WriteTile(
            dst_path, downsampled, dst_w, dst_h,
            tile->band_count, tile->pixel_type,
            dst_extent, meta, crs);
        if (!result) return result;
      }
    }
  }

  return {};
}

auto CdbDatastore::ListDatasets() const
    -> const std::vector<metadata::DatasetInfo>& {
  return impl_->datasets;
}

auto CdbDatastore::LodRange(const std::string& dataset) const
    -> std::expected<std::pair<int, int>, Error> {
  auto dataset_dir = impl_->root / "coverages" / dataset;
  if (!std::filesystem::exists(dataset_dir)) {
    return std::unexpected(
        Error(ErrorCode::kNotFound,
              std::format("Dataset '{}' not found", dataset)));
  }

  int min_lod = 999;
  int max_lod = -999;
  bool found = false;

  for (const auto& entry :
       std::filesystem::directory_iterator(dataset_dir)) {
    if (!entry.is_directory()) continue;
    auto name = entry.path().filename().string();

    if (name.size() < 6 || name.substr(0, 4) != "lod_") continue;

    int lod = 0;
    if (name.size() > 7 && name.substr(4, 3) == "neg") {
      auto [ptr, ec] = std::from_chars(
          name.data() + 7, name.data() + name.size(), lod);
      if (ec != std::errc{}) continue;
      lod = -lod;
    } else {
      auto [ptr, ec] = std::from_chars(
          name.data() + 4, name.data() + name.size(), lod);
      if (ec != std::errc{}) continue;
    }

    // Only count LODs that contain files.
    bool has_files = false;
    for (const auto& f :
         std::filesystem::directory_iterator(entry.path())) {
      if (f.is_regular_file()) { has_files = true; break; }
    }
    if (!has_files) continue;

    min_lod = std::min(min_lod, lod);
    max_lod = std::max(max_lod, lod);
    found = true;
  }

  if (!found) {
    return std::unexpected(
        Error(ErrorCode::kNotFound,
              std::format("No LOD data found for dataset '{}'", dataset)));
  }

  return std::pair{min_lod, max_lod};
}

auto CdbDatastore::ReadCoverage(
    const std::string& dataset,
    const tiling::TileAddress& addr)
    -> std::expected<coverage::CoverageData, Error> {
  static constexpr const char* kExts[] = {
      "tif", "gpkg", "jp2", "png"};

  std::filesystem::path tile_path;
  bool found = false;
  for (const auto* ext : kExts) {
    auto p = impl_->path_builder.CoveragePath(dataset, addr, ext);
    if (std::filesystem::exists(p)) {
      tile_path = p;
      found = true;
      break;
    }
  }

  if (!found) {
    return std::unexpected(
        Error(ErrorCode::kNotFound,
              std::format("Tile not found for dataset '{}' at LOD {}, "
                          "geocell ({},{}), row {}, col {}",
                          dataset, addr.lod,
                          addr.geocell.lat, addr.geocell.lon,
                          addr.tile_row, addr.tile_col)));
  }

  auto result = ReadGeoTiff(tile_path);
  if (!result) return std::unexpected(result.error());

  coverage::CoverageData cov;
  cov.width = result->width;
  cov.height = result->height;
  cov.band_count = result->band_count;
  cov.pixel_type = result->pixel_type;
  cov.pixels = static_cast<std::vector<std::byte>&&>(result->pixels);
  return cov;
}

}  // namespace cdbapi
