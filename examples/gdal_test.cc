#include <cstdio>
#include <cstring>
#include <vector>
#include "gdal.h"

int main(int argc, char* argv[]) {
  GDALAllRegister();

  if (argc < 2) { std::printf("Usage: gdal_test <file>\n"); return 1; }

  GDALDatasetH ds = GDALOpen(argv[1], GA_ReadOnly);
  if (!ds) { std::printf("Cannot open\n"); return 1; }

  int w = GDALGetRasterXSize(ds);
  int h = GDALGetRasterYSize(ds);
  std::printf("Size: %d x %d\n", w, h);

  GDALRasterBandH band = GDALGetRasterBand(ds, 1);
  GDALDataType dt = GDALGetRasterDataType(band);
  std::printf("Type: %s\n", GDALGetDataTypeName(dt));

  // CRS
  const char* proj = GDALGetProjectionRef(ds);
  std::printf("CRS: %s\n", (proj && proj[0]) ? proj : "(none)");

  // GeoTransform
  double gt[6];
  if (GDALGetGeoTransform(ds, gt) == CE_None) {
    std::printf("GeoTransform: [%.6f, %.6f, %.6f, %.6f, %.6f, %.6f]\n",
                gt[0], gt[1], gt[2], gt[3], gt[4], gt[5]);
  } else {
    std::printf("GeoTransform: (not set)\n");
  }

  // NoData
  int has_nodata = 0;
  double nodata = GDALGetRasterNoDataValue(band, &has_nodata);
  std::printf("NoData: %s (%.1f)\n", has_nodata ? "yes" : "no", nodata);

  // Stats
  double min_val, max_val, mean, stddev;
  GDALComputeRasterStatistics(band, FALSE, &min_val, &max_val,
                              &mean, &stddev, nullptr, nullptr);
  std::printf("Stats: min=%.2f max=%.2f mean=%.2f\n",
              min_val, max_val, mean);

  GDALClose(ds);
  return 0;
}
