/*--------------------------------------------------------------------
 *    The MB-system:  wcd_export3d.h  28/11/2026
 *
 *    Copyright (c) 2026 by
 *    Christian dos Santos Ferreira
 *      MARUM
 *      University of Bremen
 *      Bremen Germany
 *    David W. Caress (caress@mbari.org)
 *      Monterey Bay Aquarium Research Institute
 *      Moss Landing, California, USA
 *    Dale N. Chayes
 *      Center for Coastal and Ocean Mapping
 *      University of New Hampshire
 *      Durham, New Hampshire, USA
 *
 *    MB-System was created by Caress and Chayes in 1992 at the
 *      Lamont-Doherty Earth Observatory
 *      Columbia University
 *      Palisades, NY 10964
 *
 *    See README.md file for copying and redistribution conditions.
 *--------------------------------------------------------------------*/
/*
 * Export of the water column as 3D points (no Qt): the samples of the fans of
 * chosen pings and the cells of the curtain, each at its geographic position and
 * depth, written as mbgrdviz vector files (lon lat z value), binary PLY and LAS 1.4
 * (point format 7, UTM coordinates).
 *
 * A fan sample is placed across track from its ping's position, along the
 * heading (positive starboard), at the depth of the swath view; a curtain cell at
 * its ping's position and the depth of its centre. z is the elevation, -depth,
 * as mbgrid -A2 grids it. Nothing is cut at the bottom detection.
 *
 * A lasso holds polygons drawn in the fan view (across track, depth) or in the
 * curtain (along track, depth); with lasso_only, a fan sample is exported when it
 * lies in a fan polygon (or there is none) and its ping and depth lie in a curtain
 * polygon (or there is none), a curtain cell when it lies in a curtain polygon.
 */

#ifndef MBWCDVIEW_WCD_EXPORT3D_H
#define MBWCDVIEW_WCD_EXPORT3D_H

#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <limits>
#include <string>
#include <vector>

#include "wcd_curtain.h"
#include "wcd_data.h"

namespace wcd {

struct LassoPolygon {
  bool curtain = false;      // false: fan view (x across track), true: curtain (x along track)
  std::vector<double> x, z;  // m; z is depth, positive down
  bool contains(double px, double pz) const;
};

struct Lasso {
  std::vector<LassoPolygon> polygons;
  bool empty() const { return polygons.empty(); }
  bool has(bool curtain) const;
  bool contains(bool curtain, double x, double depth) const;  // in any polygon of that view
};

/* Lasso file: lines "fan" or "curtain" start a polygon, followed by lines "x depth" (m);
   '#' starts a comment. Polygons need three vertices or more. */
bool read_lasso(const std::string &path, Lasso &lasso, std::string *message);
bool write_lasso(const std::string &path, const Lasso &lasso, std::string *message);

struct Export3dOptions {
  bool fan = true, curtain = false;
  int first_ping = 0, last_ping = 0, ping_step = 1;  // water column ping indexes, inclusive
  CurtainOptions beams;     // beam range (fans and curtain) and ray tracing
  bool combine_max = false; // curtain value: the strongest sample instead of the mean
  double value_min = -std::numeric_limits<double>::infinity();  // dB, samples outside are left out
  double value_max = std::numeric_limits<double>::infinity();
  size_t budget = 0;        // most points per object, 0 = all; above it every n-th is kept
  bool lasso_only = false;
  Lasso lasso;
  bool vec = true, ply = false, las = false;
  std::string prefix;       // files <prefix>_fan.<ext> and <prefix>_curtain.<ext>
  std::string name;         // vector name in the .vec files (no spaces), default from the prefix
  std::array<uint8_t, 768> colors{};  // palette, 256 RGB entries, for PLY and LAS
  double color_min = -60.0, color_max = 0.0;  // dB at the ends of the palette (and the .vec MIN/MAX)
};

struct Export3dCount {
  size_t points = 0;   // samples or cells selected
  size_t written = 0;  // after the budget
  size_t stride = 1;
  int pings = 0;       // pings with a position
  double value_min = 0.0, value_max = 0.0;  // dB of the selected points
};

struct Export3dResult {
  Export3dCount fan, curtain;
  std::string crs;     // e.g. "EPSG:32628" of the PLY and LAS files
  std::vector<std::string> files;
};

using Export3dProgress = std::function<void(double fraction)>;

/* the points an export would write, without writing; curtain: the curtain built with
   options.beams, or nullptr to build it here */
bool count_3d(const Line &line, const Export3dOptions &options, Export3dResult &result,
              const Curtain *curtain = nullptr, const std::atomic<bool> *cancel = nullptr,
              const Export3dProgress &progress = Export3dProgress(), std::string *message = nullptr);

bool export_3d(const Line &line, const Export3dOptions &options, Export3dResult &result,
               const Curtain *curtain = nullptr, const std::atomic<bool> *cancel = nullptr,
               const Export3dProgress &progress = Export3dProgress(), std::string *message = nullptr);

}  // namespace wcd

#endif
