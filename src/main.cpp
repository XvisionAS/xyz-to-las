#include <algorithm>
#include <iostream>
#include <string>
#include <vector>
#include <cmath>
#include <fstream>
#include <liblas/liblas.hpp>
#include "gdal_priv.h"
#include "ogrsf_frmts.h"
#include "PointCollector.hpp"
#include "InputProcessor.hpp"

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

  std::cout << "Processing " << inputFilename << std::endl;

  std::vector<double> zValues;
  std::string         srsWKT = "";
  PointCollector      pc1;
  pc1.colorize = colorize;
  pc1.zValues  = &zValues;

  if (!processInput(inputFilename, pc1, srsWKT)) {
    std::cerr << "Cannot open or process input file: " << inputFilename << std::endl;
    return 1;
  }

  std::cout << std::endl;

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

    std::cout << std::endl;

    std::cout << "Successfully wrote " << pc2.count << " points." << std::endl;
  } catch (std::exception const& e) {
    std::cerr << "Error during writing: " << e.what() << std::endl;
    return 1;
  }

  return 0;
}
