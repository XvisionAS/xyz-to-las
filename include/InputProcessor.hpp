#pragma once

#include <string>
#include "PointCollector.hpp"

bool processGDAL(const std::string& filename, PointCollector& pc, std::string& srsWKT);
bool processXYZ(const std::string& filename, PointCollector& pc);
bool processInput(const std::string& filename, PointCollector& pc, std::string& srsWKT);
