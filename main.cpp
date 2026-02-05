#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <algorithm>
#include <cfloat>
#include <liblas/liblas.hpp>

void printUsage() {
    std::cout << "Usage: xyz2las <input.xyz> <output.las|laz> [scale] [-c|--color]" << std::endl;
    std::cout << "  scale: optional float, default 0.01" << std::endl;
    std::cout << "  -c, --color: optional, colorize points based on Z-height (dark to light)" << std::endl;
}

bool isLazFile(const std::string& filename) {
    if (filename.length() < 4) return false;
    std::string ext = filename.substr(filename.length() - 4);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    return ext == ".laz";
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        printUsage();
        return 1;
    }

    std::string inputFilename = argv[1];
    std::string outputFilename = argv[2];
    double scale = 0.01;
    bool colorize = false;

    // Parse arguments
    for (int i = 3; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-c" || arg == "--color") {
            colorize = true;
        } else {
            try {
                scale = std::stod(arg);
            } catch (...) {
                // Ignore if not a number or handle error
            }
        }
    }

    std::ifstream ifs(inputFilename);
    if (!ifs.is_open()) {
        std::cerr << "Cannot open input file: " << inputFilename << std::endl;
        return 1;
    }

    std::ofstream ofs(outputFilename, std::ios::out | std::ios::binary);
    if (!ofs.is_open()) {
        std::cerr << "Cannot open output file: " << outputFilename << std::endl;
        return 1;
    }

    std::cout << "Scanning " << inputFilename << "..." << std::endl;

    // Pass 1: Calculate Bounds and Count
    double minX = DBL_MAX, minY = DBL_MAX, minZ = DBL_MAX;
    double maxX = -DBL_MAX, maxY = -DBL_MAX, maxZ = -DBL_MAX;
    long pointCount = 0;
    std::string line;

    while (std::getline(ifs, line)) {
        size_t first = line.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) continue;
        if (line[first] == '#' || line[first] == '/') continue;

        std::stringstream ss(line);
        double x, y, z;
        if (ss >> x >> y >> z) {
            if (x < minX) minX = x;
            if (x > maxX) maxX = x;
            if (y < minY) minY = y;
            if (y > maxY) maxY = y;
            if (z < minZ) minZ = z;
            if (z > maxZ) maxZ = z;
            pointCount++;
        }
    }

    if (pointCount == 0) {
        std::cerr << "No valid points found." << std::endl;
        return 1;
    }

    // Reset stream for Pass 2
    ifs.clear();
    ifs.seekg(0);

    std::cout << "Found " << pointCount << " points." << std::endl;
    std::cout << "Bounds: [" << minX << ", " << minY << ", " << minZ << "] - [" 
              << maxX << ", " << maxY << ", " << maxZ << "]" << std::endl;

    // Configure LAS Header
    liblas::Header header;
    header.SetVersionMajor(1);
    header.SetVersionMinor(2);
    
    // Use Format 2 (Color) if requested, else Format 0
    if (colorize) {
        header.SetDataFormatId(liblas::ePointFormat2);
        std::cout << "Colorization enabled: Formatting as Point Format 2." << std::endl;
    } else {
        header.SetDataFormatId(liblas::ePointFormat0);
    }

    header.SetScale(scale, scale, scale);
    header.SetPointRecordsCount(pointCount);
    header.SetMin(minX, minY, minZ);
    header.SetMax(maxX, maxY, maxZ);

    // Optional: Set Offset to min values to preserve precision
    header.SetOffset(std::floor(minX), std::floor(minY), std::floor(minZ));
    
    if (isLazFile(outputFilename)) {
        std::cout << "Compressed output detected (.laz). Enabling LASzip compression." << std::endl;
        header.SetCompressed(true);
    }

    // Pre-calculate inverse Z range for colorization
    double zRange = maxZ - minZ;
    if (zRange == 0.0) zRange = 1.0; // Avoid divide by zero
    double zFactor = 1.0 / zRange;
    
    // Create Writer
    try {
        liblas::Writer writer(ofs, header);

        std::string line;
        long n = 0;
        while (std::getline(ifs, line)) {
            // Trim leading whitespace
            size_t first = line.find_first_not_of(" \t\r\n");
            if (first == std::string::npos) continue; // empty line

            // Check if comment
            if (line[first] == '#' || line[first] == '/') continue;

            std::stringstream ss(line);
            double x, y, z;
            
            // Try reading x y z
            if (ss >> x >> y >> z) {
                liblas::Point point(&header);
                point.SetCoordinates(x, y, z);

                if (colorize) {
                    // Normalize Z-height to 0.0 - 1.0
                    double normZ = (z - minZ) * zFactor;
                    if (normZ < 0) normZ = 0;
                    if (normZ > 1) normZ = 1;

                    // Map to 16-bit color (0 - 65535)
                    uint16_t val = static_cast<uint16_t>(normZ * 65535.0);
                    
                    liblas::Color c(val, val, val);
                    point.SetColor(c);
                }

                writer.WritePoint(point);
                n++;
            }
        }
        std::cout << "Successfully wrote " << n << " points." << std::endl;

    } catch (std::exception const& e) {
        std::cerr << "Error during writing: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
