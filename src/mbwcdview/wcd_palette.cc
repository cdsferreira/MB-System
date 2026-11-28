/*--------------------------------------------------------------------
 *    The MB-system:  wcd_palette.cc  28/11/2026
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

#include "wcd_palette.h"

#include <algorithm>
#include <array>
#include <cmath>

#include <QColor>

#include "viridis.h"

namespace wcd {

namespace {

constexpr int NODES = 11;

/* mbview's colour tables (src/mbview/mbviewprivate.h), from the highest value to the lowest */
const float haxby_red[NODES] = {0.950f, 1.000f, 1.000f, 1.000f, 0.941f, 0.804f, 0.541f, 0.416f, 0.196f, 0.157f, 0.145f};
const float haxby_green[NODES] = {0.950f, 0.729f, 0.631f, 0.741f, 0.925f, 1.000f, 0.925f, 0.922f, 0.745f, 0.498f, 0.224f};
const float haxby_blue[NODES] = {0.950f, 0.522f, 0.267f, 0.341f, 0.475f, 0.635f, 0.682f, 1.000f, 1.000f, 0.984f, 0.686f};
const float bright_red[NODES] = {1.000f, 1.000f, 1.000f, 1.000f, 0.500f, 0.000f, 0.000f, 0.000f, 0.000f, 0.500f, 1.000f};
const float bright_green[NODES] = {0.000f, 0.250f, 0.500f, 1.000f, 1.000f, 1.000f, 1.000f, 0.500f, 0.000f, 0.000f, 0.000f};
const float bright_blue[NODES] = {0.000f, 0.000f, 0.000f, 0.000f, 0.000f, 0.000f, 1.000f, 1.000f, 1.000f, 1.000f, 1.000f};

/* colour i of 256 between the nodes of a table given from the highest value to the lowest */
QRgb from_table(const float *r, const float *g, const float *b, int i) {
  const double x = (255 - i) / 255.0 * (NODES - 1);
  const int k = std::min((int)x, NODES - 2);
  const double f = x - k;
  auto mix = [&](const float *c) { return (int)std::lround(255.0 * (c[k] + f * (c[k + 1] - c[k]))); };
  return qRgb(mix(r), mix(g), mix(b));
}

std::array<QRgb, 256> make(Palette palette) {
  std::array<QRgb, 256> colors;
  for (int i = 0; i < 256; i++) {
    switch (palette) {
    case Palette::Haxby:
      colors[i] = from_table(haxby_red, haxby_green, haxby_blue, i);
      break;
    case Palette::Grayscale:
      colors[i] = qRgb(i, i, i);
      break;
    case Palette::Rainbow:
      colors[i] = from_table(bright_red, bright_green, bright_blue, i);
      break;
    case Palette::Viridis:
    default:
      colors[i] = qRgb(viridis_rgb[i][0], viridis_rgb[i][1], viridis_rgb[i][2]);
    }
  }
  return colors;
}

}  // namespace

const QStringList &palette_names() {
  static const QStringList names = {"Viridis", "Haxby", "Grayscale", "Rainbow"};
  return names;
}

QString palette_name(Palette palette) { return palette_names().value((int)palette, "Viridis"); }

bool palette_from_name(const QString &name, Palette &palette) {
  const QStringList &names = palette_names();
  for (int i = 0; i < names.size(); i++) {
    if (names[i].compare(name, Qt::CaseInsensitive) == 0) {
      palette = (Palette)i;
      return true;
    }
  }
  return false;
}

QRgb palette_color(Palette palette, int i) {
  static const std::array<QRgb, 256> tables[4] = {make(Palette::Viridis), make(Palette::Haxby),
                                                   make(Palette::Grayscale), make(Palette::Rainbow)};
  return tables[std::min(std::max((int)palette, 0), 3)][std::min(std::max(i, 0), 255)];
}

QImage colormap_image(Palette palette) {
  QImage lut(256, 1, QImage::Format_RGBA8888);
  for (int i = 0; i < 256; i++)
    lut.setPixelColor(i, 0, QColor(palette_color(palette, i)));
  return lut;
}

}  // namespace wcd
