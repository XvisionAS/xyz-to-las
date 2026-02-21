#include <catch2/catch_test_macros.hpp>
#include <catch2/benchmark/catch_benchmark.hpp>
#include <fstream>
#include <cstdio>

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
