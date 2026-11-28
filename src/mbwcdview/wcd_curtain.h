/*--------------------------------------------------------------------
 *    The MB-system:  wcd_curtain.h  28/11/2026
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
 * Curtain (side view) of the water column of a file (no Qt): every water column
 * ping is one column at its along-track position, holding the samples of the
 * beams in a range of beam numbers (counted within each swath, in every swath of
 * the ping), combined by depth: the mean of the linear power (10^(dB/10)) and the
 * strongest value of the samples in each depth cell. A sample covers the depths
 * of its range interval, so cells finer than the sample spacing have no gaps.
 * Depths are those of the swath view: ray traced through the sound velocity
 * profile or straight rays, below the waterline when the transducer depth is known.
 */

#ifndef MBWCDVIEW_WCD_CURTAIN_H
#define MBWCDVIEW_WCD_CURTAIN_H

#include <atomic>
#include <string>
#include <vector>

#include "wcd_data.h"

namespace wcd {

struct CurtainOptions {
  int first_beam = 0;   // beam numbers within a swath, inclusive
  int last_beam = -1;   // -1: the last beam of each swath
  bool raytrace = true; // trace through the sound velocity profile when one exists
};

struct Curtain {
  int ncols = 0;                 // water column pings
  int nz = 0;                    // depth cells
  double dz = 1.0;               // m, cell height; cell k covers depths [k dz, (k + 1) dz)
  std::vector<double> along;     // per ping: along-track distance (m) or, without positions, the ping index
  bool along_is_distance = false;
  std::vector<float> mean, max;  // dB, ncols x nz (column by column), NaN = no sample
  int first_beam = 0, last_beam = -1;  // as used (last clipped to the widest swath)
  int beams_max = 0;             // most beams in one swath of any ping
  bool traced = false;           // some pings ray traced
  int column(double along) const;  // the ping whose column holds this along-track position, -1 if none
  /* along-track span of column i: half way to its neighbours (the outer ones as wide on both sides) */
  void column_span(int i, double &x0, double &x1) const;
};

/* along-track distance of every water column ping: cumulative distance between the
   positions of successive pings; false (ping indexes) when fewer than two have a position */
bool along_track(const Line &line, std::vector<double> &along);

/* most beams in one swath of any ping of the file */
int beams_per_swath_max(const WaterColumnFile &wc);

/* builds the curtain with several threads; cancel (may be nullptr) stops it early (returns false) */
bool build_curtain(const Line &line, const CurtainOptions &options, Curtain &curtain,
                   const std::atomic<bool> *cancel = nullptr, std::string *message = nullptr);

}  // namespace wcd

#endif
