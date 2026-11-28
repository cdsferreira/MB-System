/*--------------------------------------------------------------------
 *    The MB-system:  wcd_curtain.cc  28/11/2026
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

#include "wcd_curtain.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <thread>

#include "mb_define.h"
#include "mb_process.h"
#include "mb_status.h"
#include "wcd_raytrace.h"

namespace wcd {

namespace {

constexpr float NaNf = std::numeric_limits<float>::quiet_NaN();
constexpr int CELLS_TARGET = 1000;   // about this many depth cells over the deepest sample
constexpr double ANGLE_STEP = 0.25;  // deg, rows of a depth table
constexpr int TIME_STEPS = 2000;     // columns of a depth table

/* step 1, 2 or 5 x 10^n, at least x */
double nice_step(double x) {
  if (!(x > 0.0))
    return 1.0;
  const double p = std::pow(10.0, std::floor(std::log10(x)));
  const double m = x / p;
  return (m <= 1.0 ? 1.0 : (m <= 2.0 ? 2.0 : (m <= 5.0 ? 5.0 : 10.0))) * p;
}

/* Depth of a ray traced through one sound velocity profile from the transducer depth
   draft0, by take-off angle in the profile (deg from vertical, after Snell's law from the
   sound speed at the transducer, as mb_rt() applies it) and one-way travel time: one
   mb_rt() ray per ANGLE_STEP, its path times summed as swath_geometry() sums them. A ping
   at another transducer depth is shifted by the difference. */
class DepthTable {
 public:
  bool build(const Svp &svp, double draft0, double t_max, std::string *message) {
    if (!model_.init(svp, message))
      return false;
    draft0_ = draft0;
    v_source_ = model_.speed_at(draft0);
    nt_ = TIME_STEPS + 1;
    dt_ = std::max(t_max, 1.0e-3) / TIME_STEPS;
    na_ = (int)std::lround(90.0 / ANGLE_STEP) + 1;
    z_.assign((size_t)na_ * nt_, NaNf);
    const int nplot_max = 6 * ((int)model_.nodes() + 2) + 64;
    std::vector<double> xplot(nplot_max), zplot(nplot_max), tplot(nplot_max);
    for (int a = 0; a < na_; a++) {
      int nplot = 0, ray_stat = 0, error = MB_ERROR_NO_ERROR;
      double x, z, tt;
      if (mb_rt(0, model_.model(), draft0, std::min(a * ANGLE_STEP, 89.99), t_max + dt_, MBP_ANGLES_OK, 0.0, 0.0,
                nplot_max, &nplot, xplot.data(), zplot.data(), tplot.data(), &x, &z, &tt, &ray_stat,
                &error) != MB_SUCCESS ||
          nplot < 2)
        continue;
      tplot[0] = 0.0;
      for (int k = 1; k < nplot; k++)
        tplot[k] = tplot[k - 1] + std::hypot(xplot[k] - xplot[k - 1], zplot[k] - zplot[k - 1]) /
                                      model_.speed_at(0.5 * (zplot[k] + zplot[k - 1]));
      float *row = &z_[(size_t)a * nt_];
      int k1 = 1;
      for (int j = 0; j < nt_; j++) {
        const double t = j * dt_;
        if (t > tplot[nplot - 1] + 1.0e-5)
          break;
        while (k1 < nplot - 1 && tplot[k1] < t)
          k1++;
        const double span = tplot[k1] - tplot[k1 - 1];
        const double f = span > 0.0 ? std::min(1.0, std::max(0.0, (t - tplot[k1 - 1]) / span)) : 0.0;
        row[j] = (float)(zplot[k1 - 1] + f * (zplot[k1] - zplot[k1 - 1]));
      }
    }
    return true;
  }

  /* take-off angle in the profile of a beam leaving at takeoff (deg from vertical) with the
     sound speed ssv at the transducer */
  double profile_angle(double takeoff, double ssv) const {
    if (!(ssv > 0.0))
      return takeoff;
    return RTD * std::asin(std::min(1.0, std::sin(DTR * takeoff) * v_source_ / ssv));
  }

  /* depth at one-way time t along the ray of profile angle a; NaN beyond the table or where
     the ray turned back before t */
  float depth(double a, double t, double draft) const {
    const double fa = a / ANGLE_STEP, ft = t / dt_;
    if (!(fa >= 0.0) || !(ft >= 0.0))
      return NaNf;
    const int ia = std::min((int)fa, na_ - 2), it = (int)ft;
    if (it >= nt_ - 1)
      return NaNf;
    const double wa = std::min(1.0, fa - ia), wt = ft - it;
    const float *r0 = &z_[(size_t)ia * nt_], *r1 = r0 + nt_;
    const double z0 = r0[it] + wt * (r0[it + 1] - r0[it]);
    const double z1 = r1[it] + wt * (r1[it + 1] - r1[it]);
    return (float)(z0 + wa * (z1 - z0) + (draft - draft0_));
  }

 private:
  RayModel model_;
  double draft0_ = 0.0, v_source_ = 0.0, dt_ = 1.0;
  int na_ = 0, nt_ = 0;
  std::vector<float> z_;
};

}  // namespace

/*--------------------------------------------------------------------*/
int Curtain::column(double x) const {
  if (ncols == 0)
    return -1;
  double x0, x1;
  column_span(0, x0, x1);
  if (x < x0)
    return -1;
  column_span(ncols - 1, x0, x1);
  if (x >= x1)
    return -1;
  /* the last column whose span starts at or before x */
  int lo = 0, hi = ncols - 1;
  while (lo < hi) {
    const int mid = (lo + hi + 1) / 2;
    column_span(mid, x0, x1);
    if (x0 <= x)
      lo = mid;
    else
      hi = mid - 1;
  }
  return lo;
}

void Curtain::column_span(int i, double &x0, double &x1) const {
  const double half_before = i > 0 ? 0.5 * (along[i] - along[i - 1]) : -1.0;
  const double half_after = i + 1 < ncols ? 0.5 * (along[i + 1] - along[i]) : -1.0;
  const double h0 = half_before >= 0.0 ? half_before : (half_after >= 0.0 ? half_after : 0.5);
  const double h1 = half_after >= 0.0 ? half_after : h0;
  x0 = along[i] - h0;
  x1 = along[i] + h1;
}

/*--------------------------------------------------------------------*/
bool along_track(const Line &line, std::vector<double> &along) {
  const size_t n = line.wc.size();
  along.assign(n, 0.0);
  double lon0 = 0.0, lat0 = 0.0, distance = 0.0;
  int valid = 0;
  for (size_t i = 0; i < n; i++) {
    double lon, lat, sog, cog;
    line.nav_at(line.wc.pings[i].time_d, lon, lat, sog, cog);
    if (std::isfinite(lon) && std::isfinite(lat)) {
      if (valid > 0) {
        const double dn = (lat - lat0) * 111320.0;
        const double de = (lon - lon0) * 111320.0 * std::cos(DTR * 0.5 * (lat + lat0));
        distance += std::hypot(dn, de);
      }
      lon0 = lon;
      lat0 = lat;
      valid++;
    }
    along[i] = distance;  // a ping without a position stays at the previous one
  }
  if (valid >= 2 && distance > 0.0)
    return true;
  for (size_t i = 0; i < n; i++)
    along[i] = (double)i;
  return false;
}

int beams_per_swath_max(const WaterColumnFile &wc) {
  int most = 0;
  std::map<int, int> count;
  for (const WcPing &p : wc.pings) {
    count.clear();
    for (const int s : p.swath)
      most = std::max(most, ++count[s]);
  }
  return most;
}

/*--------------------------------------------------------------------*/
bool build_curtain(const Line &line, const CurtainOptions &options, Curtain &c, const std::atomic<bool> *cancel,
                   std::string *message) {
  c = Curtain();
  if (!line.has_wc || line.wc.size() == 0) {
    if (message != nullptr)
      *message = "no water column data";
    return false;
  }
  const WaterColumnFile &wc = line.wc;
  const size_t n = wc.size();
  c.ncols = (int)n;
  c.along_is_distance = along_track(line, c.along);
  c.beams_max = beams_per_swath_max(wc);
  c.first_beam = std::max(0, options.first_beam);
  c.last_beam = options.last_beam < 0 ? c.beams_max - 1 : std::min(options.last_beam, c.beams_max - 1);

  /* transducer depths, the deepest sample and the depth cells */
  std::vector<double> drafts(n, 0.0);
  double draft_max = 0.0;
  for (size_t i = 0; i < n; i++) {
    line.draft(i, drafts[i]);
    draft_max = std::max(draft_max, drafts[i]);
  }
  const double depth_max = wc.range_max_m + draft_max;
  c.dz = nice_step(depth_max / CELLS_TARGET);
  c.nz = std::max(1, (int)std::ceil(depth_max / c.dz) + 1);
  c.mean.assign((size_t)c.ncols * c.nz, NaNf);
  c.max.assign((size_t)c.ncols * c.nz, NaNf);

  /* one depth table per sound velocity profile, from the first ping that uses it */
  std::vector<const DepthTable *> table_of(n, nullptr);
  std::map<const Svp *, std::unique_ptr<DepthTable>> tables;
  if (options.raytrace) {
    std::map<const Svp *, std::pair<double, double>> used;  // draft of the first ping, longest one-way time
    for (size_t i = 0; i < n; i++) {
      const Svp *svp = line.svp(wc.pings[i].time_d);
      if (svp == nullptr)
        continue;
      double t_max = 0.0;
      const WcPing &p = wc.pings[i];
      for (int b = 0; b < p.nbeams(); b++)
        t_max = std::max(t_max, 0.5 * (p.start_sample[b] + p.nsamples[b] + 1) * p.sample_interval[b]);
      auto it = used.find(svp);
      if (it == used.end())
        used[svp] = {drafts[i], t_max};
      else
        it->second.second = std::max(it->second.second, t_max);
    }
    for (const auto &u : used) {
      auto table = std::make_unique<DepthTable>();
      if (table->build(*u.first, u.second.first, u.second.second, message))
        tables[u.first] = std::move(table);
    }
    for (size_t i = 0; i < n; i++) {
      const auto it = tables.find(line.svp(wc.pings[i].time_d));
      if (it != tables.end())
        table_of[i] = it->second.get();
    }
    c.traced = !tables.empty();
  }

  /* linear power of every amplitude value (0.1 dB steps) */
  std::vector<float> power(65536);
  for (int v = -32768; v < 32768; v++)
    power[(size_t)(v + 32768)] = (float)std::pow(10.0, 0.01 * v);
  const int16_t fill_raw = (int16_t)std::lround(10.0 * wc.fill_value);

  /* the pings, interleaved over the threads; each ping writes only its own column */
  const int nthreads = std::max(1, std::min((int)std::thread::hardware_concurrency(), 8));
  auto work = [&](int first) {
    std::vector<double> sum(c.nz);
    std::vector<uint32_t> count(c.nz);
    std::vector<int16_t> peak(c.nz);
    std::vector<float> z;
    std::map<int, int> row_in_swath;
    for (size_t i = (size_t)first; i < n; i += (size_t)nthreads) {
      if (cancel != nullptr && cancel->load())
        return;
      const WcPing &p = wc.pings[i];
      const DepthTable *table = table_of[i];
      const double draft = drafts[i];
      std::fill(sum.begin(), sum.end(), 0.0);
      std::fill(count.begin(), count.end(), 0u);
      std::fill(peak.begin(), peak.end(), std::numeric_limits<int16_t>::min());
      row_in_swath.clear();
      size_t offset = p.amp_offset;
      for (int b = 0; b < p.nbeams(); b++) {
        const int ns = p.nsamples[b];
        const int16_t *a = &wc.amp[offset];
        offset += (size_t)ns;
        const int row = row_in_swath[p.swath[b]]++;
        if (row < c.first_beam || row > c.last_beam || ns <= 0)
          continue;

        /* the fill value before the first echo is no data, as in the swath view */
        int k0 = 0;
        if (wc.has_fill)
          while (k0 < ns && a[k0] <= fill_raw)
            k0++;
        if (k0 >= ns)
          continue;

        /* depth of every sample */
        const double down = std::cos(DTR * p.angle_along[b]) * std::cos(DTR * p.angle_across[b]);
        const double interval = p.sample_interval[b];
        z.resize((size_t)ns);
        if (table != nullptr) {
          const double angle = table->profile_angle(RTD * std::acos(std::max(-1.0, std::min(1.0, down))),
                                                    p.soundspeed[b]);
          for (int k = k0; k < ns; k++)
            z[k] = table->depth(angle, 0.5 * (p.start_sample[b] + k) * interval, draft);
        }
        else {
          const double step = 0.5 * p.soundspeed[b] * interval * down;
          for (int k = k0; k < ns; k++)
            z[k] = (float)(draft + (p.start_sample[b] + k) * step);
        }

        /* each sample covers the depths from half way to the sample before to half way to the next */
        for (int k = k0; k < ns; k++) {
          if (!std::isfinite(z[k]))
            continue;
          const bool before = k > k0 && std::isfinite(z[k - 1]);
          const bool after = k + 1 < ns && std::isfinite(z[k + 1]);
          const double half_up = before ? 0.5 * (z[k] - z[k - 1]) : (after ? 0.5 * (z[k + 1] - z[k]) : 0.0);
          const double half_down = after ? 0.5 * (z[k + 1] - z[k]) : half_up;
          int c0 = (int)std::floor((z[k] - std::fabs(half_up)) / c.dz);
          int c1 = (int)std::floor((z[k] + std::fabs(half_down)) / c.dz);
          c0 = std::max(c0, 0);
          c1 = std::min(c1, c.nz - 1);
          if (c1 > c0 && (z[k] + std::fabs(half_down)) == (c1 * c.dz))
            c1--;  // a range ending exactly on a cell boundary does not reach into the cell below
          const float pw = power[(size_t)(a[k] + 32768)];
          for (int cell = c0; cell <= c1; cell++) {
            sum[cell] += pw;
            count[cell]++;
            peak[cell] = std::max(peak[cell], a[k]);
          }
        }
      }
      float *mean = &c.mean[i * (size_t)c.nz];
      float *mx = &c.max[i * (size_t)c.nz];
      for (int k = 0; k < c.nz; k++) {
        if (count[k] == 0)
          continue;
        mean[k] = (float)(10.0 * std::log10(sum[k] / count[k]));
        mx[k] = 0.1f * peak[k];
      }
    }
  };
  std::vector<std::thread> threads;
  for (int t = 1; t < nthreads; t++)
    threads.emplace_back(work, t);
  work(0);
  for (std::thread &t : threads)
    t.join();
  if (cancel != nullptr && cancel->load()) {
    if (message != nullptr)
      *message = "cancelled";
    return false;
  }
  return true;
}

}  // namespace wcd
