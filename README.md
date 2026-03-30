# cdbapi++

An open, free C++20 library for reading and writing [OGC CDB Version 2](https://docs.ogc.org/is/23-034/23-034.html) geospatial datastores.

CDB is an OGC standard for organizing tiled geospatial data -- elevation, imagery, 3D models -- into a structured file hierarchy optimized for fast, reliable access by simulation and visualization systems.

## Features

- **Write coverages** -- tile raster data (elevation, imagery) into a spec-compliant CDB directory structure with correct geotransform, CRS, and metadata
- **LOD pyramid generation** -- downsample fine LOD tiles to coarser LODs using nodata-aware box-filter averaging
- **Read coverages** -- open existing datastores, list datasets, query LOD ranges, read individual tiles
- **Multiple formats** -- GeoTIFF (default), GeoPackage, JPEG2000, PNG, all via GDAL
- **Linked metadata** -- Version.xml, Datasets.xml, CRS.wkt, and Datastore_Metadata.xml with OGC-compliant link elements for resource discovery
- **CDB1 Global Grid tiling** -- coalescence zones, geocell addressing, LOD -10 through 23
- **Pixel-perfect round-trips** -- write and read back with zero data loss

## Application Profile

Since CDB 2.0 Core is abstract, this library implements a concrete application profile (`cdbapi_default_v1`):

| Setting | Value |
|---------|-------|
| CRS | EPSG:4326 (WGS 84) |
| Tiling | CDB1GlobalGrid (LODs -10 to 23) |
| Naming | snake_case, English, ASCII |
| Metadata | XML (global), XML (dataset) |
| Coverage format | GeoTIFF (configurable) |
| Unit of measure | Meters |

## Quick Start

```cpp
#include "cdbapi/cdbapi.h"

// Create a new datastore
cdbapi::metadata::DatastoreMetadata meta{
    .id = "my-datastore",
    .title = "My CDB",
    .description = "Elevation data",
    .contact_point = "user@example.com",
    .created = "2026-03-30"};

auto ds = cdbapi::CdbDatastore::Create("my_cdb", cdbapi::DefaultProfile(), meta);

// Write coverage data (1024x1024 float32 at LOD 0)
cdbapi::coverage::CoverageDescriptor desc{
    .dataset_name = "elevation",
    .bounds = {36.0, -113.0, 37.0, -112.0},
    .target_lod = 0,
    .metadata = {.field_name = "elevation",
                 .quantity_definition = "height above geoid",
                 .data_null = -32767.0}};

cdbapi::coverage::CoverageData data{
    .width = 1024, .height = 1024, .band_count = 1,
    .pixel_type = cdbapi::coverage::PixelType::kFloat32,
    .pixels = my_pixel_buffer};

ds->WriteCoverage(desc, data);

// Generate LOD pyramid (LOD 0 down to LOD -3)
ds->GenerateLodPyramid("elevation", 0, -3);
ds->Finalize();

// Later: open and read back
auto ds2 = cdbapi::CdbDatastore::Open("my_cdb");
auto& datasets = ds2->ListDatasets();         // ["elevation"]
auto range = ds2->LodRange("elevation");      // {-3, 0}

cdbapi::tiling::TileAddress addr{0, {36, -113}, 0, 0};
auto tile = ds2->ReadCoverage("elevation", addr);
// tile->pixels contains the original data
```

## CDB Directory Structure

```
my_cdb/
  global_metadata/
    Version.xml               Spec version + links to other metadata
    Datasets.xml              Dataset catalog with links to coverages
    Datastore_Metadata.xml    DCAT-aligned datastore description
    CRS.wkt                   WKT2 coordinate reference system
  coverages/
    elevation/
      lod_00/
        n36_w113.tif          1024x1024 GeoTIFF (LOD 0)
      lod_neg01/
        n36_w113.tif          512x512 (LOD -1)
      lod_neg02/
        n36_w113.tif          256x256 (LOD -2)
```

## Building

Requires C++20, CMake 3.25+, and [vcpkg](https://vcpkg.io/).

```bash
# Install dependencies (via vcpkg manifest mode)
# VCPKG_ROOT must be set, or use CMakePresets.json

cmake --preset default
cmake --build --preset default
ctest --preset default
```

### Dependencies

| Library | Purpose |
|---------|---------|
| [GDAL](https://gdal.org/) | Raster I/O (GeoTIFF, GPKG, JP2, PNG) |
| [PROJ](https://proj.org/) | CRS/WKT2 generation |
| [pugixml](https://pugixml.org/) | XML metadata read/write |
| [nlohmann-json](https://github.com/nlohmann/json) | JSON metadata |
| [Catch2](https://github.com/catchorg/Catch2) | Test framework |

## Spec Reference

- [OGC CDB 2.0 Core (23-034)](https://docs.ogc.org/is/23-034/23-034.html)

## License

This project is open and free to use. See [LICENSE](LICENSE) for details.
