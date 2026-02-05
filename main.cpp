#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <algorithm>
#include <liblas/liblas.hpp>

void printUsage() {
    std::cout << "Usage: xyz2las <input.xyz> <output.las|laz> [scale]" << std::endl;
    std::cout << "  scale: optional float, default 0.01" << std::endl;
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
    if (argc > 3) {
        try {
            scale = std::stod(argv[3]);
        } catch (...) {
            std::cerr << "Invalid scale provided, using default 0.01" << std::endl;
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

    std::cout << "Converting " << inputFilename << " to " << outputFilename << "..." << std::endl;

    // Configure LAS Header
    liblas::Header header;
    header.SetVersionMajor(1);
    header.SetVersionMinor(2);
    header.SetDataFormatId(liblas::ePointFormat0);
    header.SetScale(scale, scale, scale);
    
    if (isLazFile(outputFilename)) {
        std::cout << "Compressed output detected (.laz). Enabling LASzip compression." << std::endl;
        header.SetCompressed(true);
    }
    
    // Create Writer
    try {
        liblas::Writer writer(ofs, header);

        std::string line;
        long pointCount = 0;
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
                writer.WritePoint(point);
                pointCount++;
            }
        }
        std::cout << "Successfully wrote " << pointCount << " points." << std::endl;

    } catch (std::exception const& e) {
        std::cerr << "Error during writing: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
