#include "PointCollector.hpp"

PointCollector::PointCollector() : minX(DBL_MAX), minY(DBL_MAX), minZ(DBL_MAX),
                     maxX(-DBL_MAX), maxY(-DBL_MAX), maxZ(-DBL_MAX),
                     count(0), colorize(false), zValues(nullptr),
                     header(nullptr), writer(nullptr), colorMinZ(0), zFactor(0), totalPoints(0), reusablePoint(nullptr) {}

PointCollector::~PointCollector() {
  if (reusablePoint) {
    delete reusablePoint;
  }
}

void PointCollector::addPoint(double x, double y, double z) {
  if (x < minX) minX = x;
  if (x > maxX) maxX = x;
  if (y < minY) minY = y;
  if (y > maxY) maxY = y;
  if (z < minZ) minZ = z;
  if (z > maxZ) maxZ = z;
  count++;

  if (count % 100000 == 0) {
    if (totalPoints > 0) {
      int percent = static_cast<int>((count * 100.0) / totalPoints);
      std::cout << "\rWriting points: " << count << " / " << totalPoints << " (" << percent << "%)" << std::flush;
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
      if (normZ < 0) normZ = 0;
      if (normZ > 1) normZ = 1;
      uint16_t      val = static_cast<uint16_t>(normZ * 65535.0);
      liblas::Color c(val, val, val);
      reusablePoint->SetColor(c);
    }
    writer->WritePoint(*reusablePoint);
  }
}

void PointCollector::processGeometry(OGRGeometry* g) {
  if (!g) return;
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
