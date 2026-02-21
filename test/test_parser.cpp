#include <catch2/catch_test_macros.hpp>
#include <catch2/benchmark/catch_benchmark.hpp>
#include <fstream>
#include <cstdio>
#include <limits>
#include <cmath>

#include "gdal_priv.h"
#include "PointCollector.hpp"
#include "InputProcessor.hpp"

TEST_CASE("XYZ Parser handles basic files", "[parser]") {
    // Create a temporary test file
    const char* test_file = "test_basic.xyz";
    std::ofstream out(test_file);
    out << "1.0 2.0 3.0\n";
    out << "4.0\t5.0\t6.0\n";
    out << "  7.0  8.0  9.0  \n";
    out << "# This is a comment\n";
    out << "10.0 11.0 12.0 # inline comment\n";
    out.close();

    PointCollector pc;
    pc.quiet = true;
    bool success = processXYZ(test_file, pc);

    REQUIRE(success == true);
    REQUIRE(pc.count == 4);
    REQUIRE(pc.minX == 1.0);
    REQUIRE(pc.maxX == 10.0);
    REQUIRE(pc.minY == 2.0);
    REQUIRE(pc.maxY == 11.0);
    REQUIRE(pc.minZ == 3.0);
    REQUIRE(pc.maxZ == 12.0);

    std::remove(test_file);
}

TEST_CASE("XYZ Parser handles empty and invalid files", "[parser]") {
    const char* test_file = "test_invalid.xyz";
    std::ofstream out(test_file);
    out << "\n\n\n";
    out << "invalid data here\n";
    out << "1.0 2.0\n"; // Missing Z
    out.close();

    PointCollector pc;
    pc.quiet = true;
    bool success = processXYZ(test_file, pc);

    REQUIRE(success == true); // File opens successfully
    REQUIRE(pc.count == 0);   // But no valid points are found

    std::remove(test_file);
}

TEST_CASE("XYZ Parser Benchmark", "[benchmark]") {
    const char* test_file = "test_bench.xyz";
    
    // Generate a large file once
    std::ofstream out(test_file);
    for (int i = 0; i < 100000; ++i) {
        out << i << ".0 " << i << ".5 " << i << ".9\n";
    }
    out.close();

    BENCHMARK("Parse 100k points") {
        PointCollector pc;
        pc.quiet = true;
        return processXYZ(test_file, pc);
    };

    std::remove(test_file);
}

TEST_CASE("GDAL Parser handles GeoTIFF files", "[gdal]") {
    GDALAllRegister();
    const char* test_file = "test_gdal.tif";

    GDALDriver* poDriver = GetGDALDriverManager()->GetDriverByName("GTiff");
    REQUIRE(poDriver != nullptr);

    GDALDataset* poDS = poDriver->Create(test_file, 2, 2, 1, GDT_Float32, nullptr);
    REQUIRE(poDS != nullptr);

    double adfGeoTransform[6] = { 10.0, 2.0, 0.0, 20.0, 0.0, -2.0 };
    poDS->SetGeoTransform(adfGeoTransform);

    GDALRasterBand* poBand = poDS->GetRasterBand(1);
    poBand->SetNoDataValue(-9999.0);

    float rasterData[4] = { 1.0f, -9999.0f, 3.0f, 4.0f };
    CPLErr err = poBand->RasterIO(GF_Write, 0, 0, 2, 2, rasterData, 2, 2, GDT_Float32, 0, 0);
    REQUIRE(err == CE_None);

    GDALClose(poDS);

    PointCollector pc;
    pc.quiet = true;
    std::string srsWKT;
    bool success = processGDAL(test_file, pc, srsWKT);

    REQUIRE(success == true);
    REQUIRE(pc.count == 3); // One NoData value skipped

    // Check coordinates
    // Pixel 0,0 -> x = 10 + 0.5*2 = 11, y = 20 + 0.5*-2 = 19, z = 1
    // Pixel 1,0 -> NoData
    // Pixel 0,1 -> x = 10 + 0.5*2 = 11, y = 20 + 1.5*-2 = 17, z = 3
    // Pixel 1,1 -> x = 10 + 1.5*2 = 13, y = 20 + 1.5*-2 = 17, z = 4

    REQUIRE(pc.minX == 11.0);
    REQUIRE(pc.maxX == 13.0);
    REQUIRE(pc.minY == 17.0);
    REQUIRE(pc.maxY == 19.0);
    REQUIRE(pc.minZ == 1.0);
    REQUIRE(pc.maxZ == 4.0);

    std::remove(test_file);
}

TEST_CASE("GDAL Parser handles NaN and Scale/Offset", "[gdal]") {
    GDALAllRegister();
    const char* test_file = "test_gdal_nan.tif";

    GDALDriver* poDriver = GetGDALDriverManager()->GetDriverByName("GTiff");
    REQUIRE(poDriver != nullptr);

    GDALDataset* poDS = poDriver->Create(test_file, 2, 1, 1, GDT_Float32, nullptr);
    REQUIRE(poDS != nullptr);
    
    double adfGeoTransform[6] = { 0.0, 1.0, 0.0, 0.0, 0.0, 1.0 };
    poDS->SetGeoTransform(adfGeoTransform);

    GDALRasterBand* poBand = poDS->GetRasterBand(1);
    poBand->SetScale(2.0);
    poBand->SetOffset(10.0);
    
    float rasterData[2] = { 5.0f, std::numeric_limits<float>::quiet_NaN() };
    CPLErr err = poBand->RasterIO(GF_Write, 0, 0, 2, 1, rasterData, 2, 1, GDT_Float32, 0, 0);
    REQUIRE(err == CE_None);

    GDALClose(poDS);

    PointCollector pc;
    pc.quiet = true;
    std::string srsWKT;
    bool success = processGDAL(test_file, pc, srsWKT);

    REQUIRE(success == true);
    REQUIRE(pc.count == 1); // NaN skipped

    // z = 5.0 * 2.0 + 10.0 = 20.0
    REQUIRE(pc.minZ == 20.0);
    REQUIRE(pc.maxZ == 20.0);

    std::remove(test_file);
}
