#include <algorithm>
#include <cfloat>
#include <fstream>
#include <iostream>
#include <liblas/liblas.hpp>
#include <sstream>
#include <string>
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "fast_float/fast_float.h"
#include <mio/mmap.hpp>
#include "gdal_priv.h"
#include "ogrsf_frmts.h"
#include "cpl_error.h"

// Helper to collect bounds and write points
struct PointCollector {
  double               minX, minY, minZ;
  double               maxX, maxY, maxZ;
  long                 count;
  bool                 colorize;
  std::vector<double>* zValues;
  liblas::Header*      header;
  liblas::Writer*      writer;
  double               colorMinZ, zFactor;
  long                 totalPoints;
  liblas::Point*       reusablePoint;

  PointCollector() : minX(DBL_MAX), minY(DBL_MAX), minZ(DBL_MAX),
                     maxX(-DBL_MAX), maxY(-DBL_MAX), maxZ(-DBL_MAX),
                     count(0), colorize(false), zValues(nullptr),
                     header(nullptr), writer(nullptr), colorMinZ(0), zFactor(0), totalPoints(0), reusablePoint(nullptr) {}

  ~PointCollector() {
    if (reusablePoint) {
      delete reusablePoint;
    }
  }

  void addPoint(double x, double y, double z) {
    if (x < minX) {
      minX = x;
    }
    if (x > maxX) {
      maxX = x;
    }
    if (y < minY) {
      minY = y;
    }
    if (y > maxY) {
      maxY = y;
    }
    if (z < minZ) {
      minZ = z;
    }
    if (z > maxZ) {
      maxZ = z;
    }
    count++;

    if (count % 100000 == 0) {
      if (totalPoints > 0) {
        int percent = static_cast<int>((count * 100.0) / totalPoints);
        std::cout << "\rWriting points: " << count << " / " << totalPoints << " (" << percent << "%)" << std::flush;
      } else {
        std::cout << "\rScanning points: " << count << "..." << std::flush;
      }
    }

    if (colorize && zValues) {
      zValues->push_back(z);
    }

    if (writer && header) {
      if (!reusablePoint) {
        reusablePoint = new liblas::Point(header);
      }
      reusablePoint->SetCoordinates(x, y, z);
      if (colorize) {
        double normZ = (z - colorMinZ) * zFactor;
        if (normZ < 0) {
          normZ = 0;
        }
        if (normZ > 1) {
          normZ = 1;
        }
        uint16_t      val = static_cast<uint16_t>(normZ * 65535.0);
        liblas::Color c(val, val, val);
        reusablePoint->SetColor(c);
      }
      writer->WritePoint(*reusablePoint);
    }
  }

  void processGeometry(OGRGeometry* g) {
    if (!g) {
      return;
    }
    OGRwkbGeometryType type = wkbFlatten(g->getGeometryType());
    if (type == wkbPoint) {
      OGRPoint* p = (OGRPoint*)g;
      addPoint(p->getX(), p->getY(), p->getZ());
    } else if (wkbFlatten(type) == wkbGeometryCollection ||
               type == wkbMultiPoint || type == wkbMultiLineString ||
               type == wkbMultiPolygon) {
      OGRGeometryCollection* gc = (OGRGeometryCollection*)g;
      for (int i = 0; i < gc->getNumGeometries(); ++i) {
        processGeometry(gc->getGeometryRef(i));
      }
    } else if (type == wkbLineString) {
      OGRLineString* ls = (OGRLineString*)g;
      for (int i = 0; i < ls->getNumPoints(); ++i) {
        addPoint(ls->getX(i), ls->getY(i), ls->getZ(i));
      }
    } else if (type == wkbPolygon) {
      OGRPolygon* poly = (OGRPolygon*)g;
      processGeometry(poly->getExteriorRing());
      for (int i = 0; i < poly->getNumInteriorRings(); ++i) {
        processGeometry(poly->getInteriorRing(i));
      }
    }
  }
};

// --- Input Processing Functions ---

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

  if (!pc.writer) { // Only log on first pass
    std::cout << "Using GDAL loader (Driver: " << driverName << ")" << std::endl;
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
  } else {
    for (int i = 0; i < poDS->GetLayerCount(); ++i) {
      OGRLayer* poLayer = poDS->GetLayer(i);
      poLayer->ResetReading();
      OGRFeature* poFeature;
      while ((poFeature = poLayer->GetNextFeature()) != nullptr) {
        pc.processGeometry(poFeature->GetGeometryRef());
        OGRFeature::DestroyFeature(poFeature);
      }
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

  if (!pc.writer) { // Only log on first pass
    std::cout << "Using fast manual XYZ loader (mio)." << std::endl;
  }

  const char* ptr = mmap.data();
  const char* end = ptr + mmap.size();

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

    ptr = endOfLine + 1;
  }

  return true;
}

// Helper to determine which input processor to use
bool processInput(const std::string& filename, PointCollector& pc, std::string& srsWKT) {
  if (processGDAL(filename, pc, srsWKT)) {
    return true;
  }
  return processXYZ(filename, pc);
}

// --- General Helpers ---

void printUsage() {
  std::cout << "Usage: xyz2las <input> <output.las|laz> [scale] [-c|--color]" << std::endl;
  std::cout << "  scale: optional float, default 0.01" << std::endl;
  std::cout << "  -c, --color: optional, colorize points based on Z-height (dark to light)" << std::endl;
}

bool isLazFile(const std::string& filename) {
  if (filename.length() < 4) {
    return false;
  }
  std::string ext = filename.substr(filename.length() - 4);
  std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
  return ext == ".laz";
}

#ifndef RUN_TESTS
int main(int argc, char* argv[]) {
  GDALAllRegister();
  OGRRegisterAll();

  if (argc < 3) {
    printUsage();
    return 1;
  }

  std::string inputFilename  = argv[1];
  std::string outputFilename = argv[2];
  double      scale          = 0.01;
  bool        colorize       = false;

  for (int i = 3; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "-c" || arg == "--color") {
      colorize = true;
    } else {
      try {
        scale = std::stod(arg);
      } catch (...) {
      }
    }
  }

  std::cout << "Scanning " << inputFilename << "..." << std::endl;

  std::vector<double> zValues;
  std::string         srsWKT = "";
  PointCollector      pc1;
  pc1.colorize = colorize;
  pc1.zValues  = &zValues;

  if (!processInput(inputFilename, pc1, srsWKT)) {
    std::cerr << "Cannot open or process input file: " << inputFilename << std::endl;
    return 1;
  }

  if (pc1.count >= 100000) {
    std::cout << std::endl;
  }

  if (pc1.count == 0) {
    std::cerr << "No valid points found." << std::endl;
    return 1;
  }

  std::cout << "Found " << pc1.count << " points." << std::endl;
  std::cout << "Bounds: [" << pc1.minX << ", " << pc1.minY << ", " << pc1.minZ << "] - ["
            << pc1.maxX << ", " << pc1.maxY << ", " << pc1.maxZ << "]" << std::endl;

  // Configure LAS Header
  liblas::Header header;
  header.SetVersionMajor(1);
  header.SetVersionMinor(2);

  if (!srsWKT.empty()) {
    liblas::SpatialReference srs;
    try {
      srs.SetWKT(srsWKT);
      header.SetSRS(srs);
      std::cout << "Spatial Reference system set from input metadata." << std::endl;
    } catch (...) {
    }
  }

  header.SetScale(scale, scale, scale);
  header.SetPointRecordsCount(pc1.count);
  header.SetMin(pc1.minX, pc1.minY, pc1.minZ);
  header.SetMax(pc1.maxX, pc1.maxY, pc1.maxZ);
  header.SetOffset(std::floor(pc1.minX), std::floor(pc1.minY), std::floor(pc1.minZ));
  header.SetDataFormatId(colorize ? liblas::ePointFormat2 : liblas::ePointFormat0);
  if (isLazFile(outputFilename)) {
    header.SetCompressed(true);
  }

  // Compute percentile-based Z range for colorization
  double colorMinZ = pc1.minZ;
  double colorMaxZ = pc1.maxZ;
  if (colorize && !zValues.empty()) {
    std::cout << "Calculating Z percentiles for colorization..." << std::endl;
    std::sort(zValues.begin(), zValues.end());
    colorMinZ = zValues[static_cast<size_t>(zValues.size() * 0.02)];
    colorMaxZ = zValues[std::min(static_cast<size_t>(zValues.size() * 0.98), zValues.size() - 1)];
    std::cout << "Color Z range (2nd-98th percentile): [" << colorMinZ << ", " << colorMaxZ << "]" << std::endl;
    zValues.clear();
    zValues.shrink_to_fit();
  }
  double zRange = colorMaxZ - colorMinZ;
  if (zRange == 0.0) {
    zRange = 1.0;
  }
  double zFactor = 1.0 / zRange;

  // Create Writer and Second Pass
  try {
    std::ofstream ofs(outputFilename, std::ios::out | std::ios::binary);
    if (!ofs.is_open()) {
      std::cerr << "Cannot open output file: " << outputFilename << std::endl;
      return 1;
    }
    liblas::Writer writer(ofs, header);
    PointCollector pc2;
    pc2.colorize    = colorize;
    pc2.header      = &header;
    pc2.writer      = &writer;
    pc2.colorMinZ   = colorMinZ;
    pc2.zFactor     = zFactor;
    pc2.totalPoints = pc1.count;

    std::string dummySrs;
    processInput(inputFilename, pc2, dummySrs);

    if (pc2.count >= 100000) {
      std::cout << std::endl;
    }

    std::cout << "Successfully wrote " << pc2.count << " points." << std::endl;
  } catch (std::exception const& e) {
    std::cerr << "Error during writing: " << e.what() << std::endl;
    return 1;
  }

  return 0;
}
#endif
