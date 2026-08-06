#pragma once
//
// MapSampler — interpolation bilinéaire sur une MapData ECU avec conversion
// raw ↔ physique via factor/offset OpenDAMOS.
//
#include "ecu/RomPatcher.hpp"

#include <optional>
#include <vector>

namespace ecu {

struct AxisScale {
    double factor = 1.0;
    double offset = 0.0;
};

struct MapSampleResult {
    double value = 0.0;   // valeur interpolée (unité physique)
    int    ix0   = 0;     // indices de la cellule bas-gauche
    int    iy0   = 0;
    double xPhys = 0.0;   // coordonnées d'entrée utilisées
    double yPhys = 0.0;
};

// raw → physique
double axisToPhys(int16_t raw, const AxisScale& s);
double cellToPhys(int16_t raw, const AxisScale& s);

// Interpolation bilinéaire sur une map 2D. Les axes et cellules sont convertis
// via les facteurs fournis ; xPhys/yPhys sont dans les unités physiques des axes.
std::optional<MapSampleResult>
sampleMapBilinear(const MapData& map,
                  const AxisScale& xScale,
                  const AxisScale& yScale,
                  const AxisScale& dataScale,
                  double xPhys,
                  double yPhys);

// Trouve [i0, i1] encadrant `target` sur un axe physique croissant.
// Renvoie false si target est hors plage (clamp aux extrémités quand même).
bool findAxisBracket(const std::vector<int16_t>& axisRaw,
                     const AxisScale& scale,
                     double targetPhys,
                     int& i0,
                     int& i1,
                     double& t);

} // namespace ecu
