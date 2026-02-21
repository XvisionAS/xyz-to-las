#include "InputProcessor.hpp"
#include <iostream>
#include <vector>
#include <cstring>
#include "fast_float/fast_float.h"
#include <mio/mmap.hpp>
#include "gdal_priv.h"
#include "cpl_error.h"

bool processGDAL(const std::string& filename, PointCollector& pc, std::string& srsWKT) {
  // Suppress GDAL errors while probing to avoid noise for unsupported text formats
  CPLPushErrorHandler(CPLQuietErrorHandler);
  GDALDataset* poDS = (GDALDataset*)GDALOpenEx(filename.c_str(), GDAL_OF_VECTOR | GDAL_OF_RASTER, NULL, NULL, NULL);
  CPLPopErrorHandler();

  if (poDS == nullptr) {
    return false;
  }

  std::string driverName = poDS->GetDriverName();
  if (driverName == "XYZ") {
    // GDAL's XYZ driver is specifically for gridded rasters.
    // Irregular point clouds cause "ERROR 6: Missing values".
    // We close it and fall back to our manual XYZ parser which is more robust for point clouds.
    GDALClose(poDS);
    return false;
  }

  const char* wkt = poDS->GetProjectionRef();
  if (wkt && strlen(wkt) > 0) {
    srsWKT = wkt;
  }

  if (poDS->GetRasterCount() > 0) {
    GDALRasterBand* poBand = poDS->GetRasterBand(1);
    int             nXSize = poBand->GetXSize();
    int             nYSize = poBand->GetYSize();
    double          adfGT[6];
    bool            hasGeo     = poDS->GetGeoTransform(adfGT) == CE_None;
    int             bHasNoData = 0;
    double          noData     = poBand->GetNoDataValue(&bHasNoData);

    std::vector<float> row(nXSize);
    for (int y = 0; y < nYSize; ++y) {
      if (!pc.quiet && pc.totalPoints == 0 && y % 100 == 0) {
        int percent = static_cast<int>((y * 100.0) / nYSize);
        std::cout << "\rScanning file: " << percent << "%   " << std::flush;
      }
      if (poBand->RasterIO(GF_Read, 0, y, nXSize, 1, &row[0], nXSize, 1, GDT_Float32, 0, 0) != CE_None) {
        continue;
      }
      for (int x = 0; x < nXSize; ++x) {
        double z = row[x];
        if (bHasNoData && z == noData) {
          continue;
        }
        double wx = static_cast<double>(x), wy = static_cast<double>(y);
        if (hasGeo) {
          wx = adfGT[0] + x * adfGT[1] + y * adfGT[2];
          wy = adfGT[3] + x * adfGT[4] + y * adfGT[5];
        }
        pc.addPoint(wx, wy, z);
      }
    }
    if (!pc.quiet && pc.totalPoints == 0) std::cout << "\rScanning file: 100%   " << std::flush;
  } else {
    for (int i = 0; i < poDS->GetLayerCount(); ++i) {
      OGRLayer* poLayer = poDS->GetLayer(i);
      poLayer->ResetReading();
      GIntBig totalFeatures = poLayer->GetFeatureCount();
      GIntBig currentFeature = 0;
      OGRFeature* poFeature;
      while ((poFeature = poLayer->GetNextFeature()) != nullptr) {
        currentFeature++;
        if (!pc.quiet && pc.totalPoints == 0 && currentFeature % 10000 == 0 && totalFeatures > 0) {
          int percent = static_cast<int>((currentFeature * 100.0) / totalFeatures);
          std::cout << "\rScanning file: " << percent << "%   " << std::flush;
        }
        pc.processGeometry(poFeature->GetGeometryRef());
        OGRFeature::DestroyFeature(poFeature);
      }
      if (!pc.quiet && pc.totalPoints == 0) std::cout << "\rScanning file: 100%   " << std::flush;
    }
  }

  GDALClose(poDS);
  return true;
}

bool processXYZ(const std::string& filename, PointCollector& pc) {
  std::error_code error;
  mio::mmap_source mmap;
  mmap.map(filename, error);
  if (error) {
    return false;
  }

  const char* ptr = mmap.data();
  const char* end = ptr + mmap.size();
  long last_count = pc.count;

  while (ptr < end) {
    const char* next_newline = (const char*)std::memchr(ptr, '\n', end - ptr);
    const char* endOfLine = next_newline ? next_newline : end;

    const char* linePtr = ptr;
    // Skip whitespace
    while (linePtr < endOfLine && (*linePtr == ' ' || *linePtr == '\t' || *linePtr == '\r')) {
      linePtr++;
    }

    if (linePtr < endOfLine && *linePtr != '#' && *linePtr != '/') {
      double x, y, z;
      auto answer = fast_float::from_chars(linePtr, endOfLine, x);
      if (answer.ec == std::errc()) {
        linePtr = answer.ptr;
        while (linePtr < endOfLine && (*linePtr == ' ' || *linePtr == '\t' || *linePtr == '\r')) linePtr++;
        
        answer = fast_float::from_chars(linePtr, endOfLine, y);
        if (answer.ec == std::errc()) {
          linePtr = answer.ptr;
          while (linePtr < endOfLine && (*linePtr == ' ' || *linePtr == '\t' || *linePtr == '\r')) linePtr++;
          
          answer = fast_float::from_chars(linePtr, endOfLine, z);
          if (answer.ec == std::errc()) {
            pc.addPoint(x, y, z);
          }
        }
      }
    }

    if (!pc.quiet && pc.totalPoints == 0 && pc.count - last_count >= 100000) {
      int percent = static_cast<int>((ptr - mmap.data()) * 100.0 / mmap.size());
      std::cout << "\rScanning file: " << percent << "%   " << std::flush;
      last_count = pc.count;
    }

    ptr = endOfLine + 1;
  }

  if (!pc.quiet && pc.totalPoints == 0) {
    std::cout << "\rScanning file: 100%   " << std::flush;
  }

  return true;
}

bool processInput(const std::string& filename, PointCollector& pc, std::string& srsWKT) {
  if (processGDAL(filename, pc, srsWKT)) {
    return true;
  }
  return processXYZ(filename, pc);
}
