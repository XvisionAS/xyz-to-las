#pragma once

#include <cfloat>
#include <iostream>
#include <vector>
#include <liblas/liblas.hpp>
#include "ogrsf_frmts.h"

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
  bool                 quiet;

  PointCollector();
  ~PointCollector();

  void addPoint(double x, double y, double z);
  void processGeometry(OGRGeometry* g);
};
