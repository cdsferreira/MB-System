/*--------------------------------------------------------------------
 *    The MB-system:  wcd_palette.h  28/11/2026
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
 * Colour palettes of the amplitudes, 256 colours from the lowest to the highest value:
 * Viridis, Haxby and Rainbow (mbview's Haxby and Bright colour tables, low values blue
 * and magenta) and Grayscale (black to white).
 */

#ifndef MBWCDVIEW_WCD_PALETTE_H
#define MBWCDVIEW_WCD_PALETTE_H

#include <QImage>
#include <QRgb>
#include <QString>
#include <QStringList>

namespace wcd {

enum class Palette { Viridis, Haxby, Grayscale, Rainbow };

/* the names, in the order of the enum: "Viridis", "Haxby", "Grayscale", "Rainbow" */
const QStringList &palette_names();
QString palette_name(Palette palette);
/* case-insensitive name; false if unknown */
bool palette_from_name(const QString &name, Palette &palette);

/* colour i (0 = lowest value) of 256 */
QRgb palette_color(Palette palette, int i);

/* the palette as a 256 x 1 RGBA image (the colour map texture) */
QImage colormap_image(Palette palette = Palette::Viridis);

}  // namespace wcd

#endif
