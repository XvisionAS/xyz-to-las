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

#include <cxxopts.hpp>

// --- General Helpers ---

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

  cxxopts::Options options("xyz2las", "Convert XYZ/GDAL files to LAS/LAZ");
  options.add_options()
    ("positional", "Positional arguments (inputs... output)", cxxopts::value<std::vector<std::string>>())
    ("s,scale", "Scale factor", cxxopts::value<double>()->default_value("0.01"))
    ("c,color", "Colorize points based on Z-height (dark to light)", cxxopts::value<bool>()->default_value("false"))
    ("h,help", "Print usage");

  options.parse_positional({"positional"});
  options.positional_help("<input1.xyz> [input2.xyz ...] <output.las|laz>");

  cxxopts::ParseResult result;
  try {
    result = options.parse(argc, argv);
  } catch (const cxxopts::exceptions::exception& e) {
    std::cerr << "Error parsing options: " << e.what() << std::endl;
    std::cout << options.help() << std::endl;
    return 1;
  }

  if (result.count("help")) {
    std::cout << options.help() << std::endl;
    return 0;
  }

  if (!result.count("positional")) {
    std::cerr << "Error: Missing input/output files." << std::endl;
    std::cout << options.help() << std::endl;
    return 1;
  }

  auto files = result["positional"].as<std::vector<std::string>>();
  if (files.size() < 2) {
    std::cerr << "Error: At least one input file and one output file are required." << std::endl;
    std::cout << options.help() << std::endl;
    return 1;
  }

  std::string outputFilename = files.back();
  files.pop_back();
  std::vector<std::string> inputFilenames = files;

  double scale = result["scale"].as<double>();
  bool colorize = result["color"].as<bool>();

  std::vector<double> zValues;
  std::string         srsWKT = "";
  PointCollector      pc1;
  pc1.colorize = colorize;
  pc1.zValues  = &zValues;

  for (const auto& inputFilename : inputFilenames) {
    std::cout << "Processing " << inputFilename << std::endl;
    if (!processInput(inputFilename, pc1, srsWKT)) {
      std::cerr << "Cannot open or process input file: " << inputFilename << std::endl;
      return 1;
    }
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

    for (const auto& inputFilename : inputFilenames) {
      std::string dummySrs;
      processInput(inputFilename, pc2, dummySrs);
      std::cout << std::endl;
    }

    std::cout << "Successfully wrote " << pc2.count << " points." << std::endl;
  } catch (std::exception const& e) {
    std::cerr << "Error during writing: " << e.what() << std::endl;
    return 1;
  }

  return 0;
}
