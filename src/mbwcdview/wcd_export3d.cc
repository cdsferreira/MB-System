/*--------------------------------------------------------------------
 *    The MB-system:  wcd_export3d.cc  28/11/2026
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

#include "wcd_export3d.h"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>

#include "mb_define.h"
#include "mb_status.h"
#include "wcd_raytrace.h"

namespace wcd {

/*--------------------------------------------------------------------*/
/* lasso */

bool LassoPolygon::contains(double px, double pz) const {
  /* even-odd rule */
  const size_t n = std::min(x.size(), z.size());
  if (n < 3)
    return false;
  bool inside = false;
  for (size_t i = 0, j = n - 1; i < n; j = i++) {
    if ((z[i] > pz) != (z[j] > pz) && px < (x[j] - x[i]) * (pz - z[i]) / (z[j] - z[i]) + x[i])
      inside = !inside;
  }
  return inside;
}

bool Lasso::has(bool curtain) const {
  for (const LassoPolygon &p : polygons)
    if (p.curtain == curtain)
      return true;
  return false;
}

bool Lasso::contains(bool curtain, double x, double depth) const {
  for (const LassoPolygon &p : polygons)
    if (p.curtain == curtain && p.contains(x, depth))
      return true;
  return false;
}

bool read_lasso(const std::string &path, Lasso &lasso, std::string *message) {
  FILE *f = fopen(path.c_str(), "r");
  if (f == nullptr) {
    if (message != nullptr)
      *message = "cannot open " + path + ": " + strerror(errno);
    return false;
  }
  Lasso read;
  char buffer[1024];
  int line_number = 0;
  bool ok = true;
  while (ok && fgets(buffer, sizeof(buffer), f) != nullptr) {
    line_number++;
    char *hash = strchr(buffer, '#');
    if (hash != nullptr)
      *hash = '\0';
    char word[16];
    double x, z;
    if (sscanf(buffer, "%15s", word) != 1)
      continue;
    if (strcmp(word, "fan") == 0 || strcmp(word, "curtain") == 0) {
      read.polygons.emplace_back();
      read.polygons.back().curtain = word[0] == 'c';
    }
    else if (sscanf(buffer, "%lf %lf", &x, &z) == 2 && std::isfinite(x) && std::isfinite(z) &&
             !read.polygons.empty()) {
      read.polygons.back().x.push_back(x);
      read.polygons.back().z.push_back(z);
    }
    else {
      if (message != nullptr)
        *message = path + " line " + std::to_string(line_number) + ": expected fan, curtain or x depth";
      ok = false;
    }
  }
  fclose(f);
  if (!ok)
    return false;
  for (const LassoPolygon &p : read.polygons)
    if (p.x.size() < 3) {
      if (message != nullptr)
        *message = path + ": a polygon has fewer than three vertices";
      return false;
    }
  lasso = std::move(read);
  return true;
}

bool write_lasso(const std::string &path, const Lasso &lasso, std::string *message) {
  FILE *f = fopen(path.c_str(), "w");
  if (f == nullptr) {
    if (message != nullptr)
      *message = "cannot write " + path + ": " + strerror(errno);
    return false;
  }
  fprintf(f, "# mbwcdview lasso: polygons of the fan view (across track, depth) or the curtain\n"
             "# (along track, depth), in m, depth positive down\n");
  for (const LassoPolygon &p : lasso.polygons) {
    fprintf(f, "%s\n", p.curtain ? "curtain" : "fan");
    for (size_t i = 0; i < p.x.size() && i < p.z.size(); i++)
      fprintf(f, "%.3f %.3f\n", p.x[i], p.z[i]);
  }
  const bool ok = ferror(f) == 0;
  if (fclose(f) != 0 || !ok) {
    if (message != nullptr)
      *message = "cannot write " + path;
    return false;
  }
  return true;
}

/*--------------------------------------------------------------------*/

namespace {

struct Point {
  double lon, lat, time_d;
  float z, value;
  unsigned int ping_number;
  int ping;  // water column ping index
};

using Visit = std::function<void(const Point &)>;

std::vector<size_t> selected_pings(const Line &line, const Export3dOptions &o) {
  std::vector<size_t> pings;
  const int n = (int)line.wc.size();
  const int first = std::max(0, o.first_ping), last = std::min(n - 1, o.last_ping);
  for (int i = first; i <= last; i += std::max(1, o.ping_step))
    pings.push_back((size_t)i);
  return pings;
}

bool in_window(const Export3dOptions &o, double v) { return std::isfinite(v) && v >= o.value_min && v <= o.value_max; }

/* the samples of the fans of the selected pings; false when cancelled */
bool visit_fans(const Line &line, const Export3dOptions &o, const std::vector<size_t> &pings,
                const std::vector<double> &along, const std::atomic<bool> *cancel, const Export3dProgress &progress,
                double p0, double p1, int &npings, const Visit &visit) {
  const WaterColumnFile &wc = line.wc;
  const bool fan_lasso = o.lasso_only && o.lasso.has(false);
  const bool curtain_lasso = o.lasso_only && o.lasso.has(true);
  npings = 0;
  std::vector<int> beams;
  std::vector<float> amp;
  SwathGeometry g;
  for (size_t ip = 0; ip < pings.size(); ip++) {
    if (cancel != nullptr && cancel->load())
      return false;
    if (progress && (ip & 15) == 0)
      progress(p0 + (p1 - p0) * (double)ip / (double)pings.size());
    const size_t i = pings[ip];
    const WcPing &p = wc.pings[i];
    double lon, lat, sog, cog;
    line.nav_at(p.time_d, lon, lat, sog, cog);
    double heading = line.heading_at(p.time_d);
    if (!std::isfinite(heading))
      heading = cog;
    if (!std::isfinite(lon) || !std::isfinite(lat) || !std::isfinite(heading))
      continue;
    npings++;
    const double h = heading * M_PI / 180.0;
    const double se = std::cos(h), sn = -std::sin(h);  // starboard unit vector (east, north)
    double mtodeglon, mtodeglat;
    mb_coor_scale(0, lat, &mtodeglon, &mtodeglat);
    double draft = 0.0;
    line.draft(i, draft);
    const Svp *svp = o.beams.raytrace ? line.svp(p.time_d) : nullptr;
    const double ping_along = i < along.size() ? along[i] : (double)i;
    if (curtain_lasso) {
      /* skip pings whose column no curtain polygon reaches */
      bool reached = false;
      for (const LassoPolygon &poly : o.lasso.polygons)
        if (poly.curtain && !poly.x.empty() &&
            ping_along >= *std::min_element(poly.x.begin(), poly.x.end()) &&
            ping_along <= *std::max_element(poly.x.begin(), poly.x.end()))
          reached = true;
      if (!reached)
        continue;
    }
    for (int s : wc.swaths(i)) {
      int ns = 0;
      wc.amplitudes(i, s, beams, ns, amp);
      if (beams.empty() || ns <= 0)
        continue;
      const int first_beam = std::max(0, o.beams.first_beam);
      const int last_beam = o.beams.last_beam < 0 ? (int)beams.size() - 1
                                                  : std::min(o.beams.last_beam, (int)beams.size() - 1);
      if (wc.has_fill) {
        /* the fill value before the first echo is no data, as in the swath view */
        const float fill = (float)wc.fill_value + 0.05f;
        for (size_t b = 0; b < beams.size(); b++) {
          float *a = &amp[b * (size_t)ns];
          for (int k = 0; k < ns && a[k] <= fill; k++)
            a[k] = std::numeric_limits<float>::quiet_NaN();
        }
      }
      if (!swath_geometry(p, beams, ns, draft, svp, g, nullptr))
        continue;
      for (int b = first_beam; b <= last_beam; b++)
        for (int k = 0; k < ns; k++) {
          const size_t j = (size_t)b * (size_t)ns + (size_t)k;
          const float v = amp[j];
          const double across = g.across[j], depth = g.depth[j];
          if (!in_window(o, v) || !std::isfinite(across) || !std::isfinite(depth))
            continue;
          if (fan_lasso && !o.lasso.contains(false, across, depth))
            continue;
          if (curtain_lasso && !o.lasso.contains(true, ping_along, depth))
            continue;
          Point pt;
          pt.lon = lon + across * se * mtodeglon;
          pt.lat = lat + across * sn * mtodeglat;
          pt.z = (float)-depth;
          pt.value = v;
          pt.time_d = p.time_d;
          pt.ping_number = p.ping_number;
          pt.ping = (int)i;
          visit(pt);
        }
    }
  }
  return true;
}

/* the cells of the curtain columns of the selected pings; false when cancelled */
bool visit_curtain(const Line &line, const Export3dOptions &o, const std::vector<size_t> &pings, const Curtain &c,
                   const std::atomic<bool> *cancel, const Export3dProgress &progress, double p0, double p1,
                   int &npings, const Visit &visit) {
  const WaterColumnFile &wc = line.wc;
  const std::vector<float> &values = o.combine_max ? c.max : c.mean;
  npings = 0;
  for (size_t ip = 0; ip < pings.size(); ip++) {
    if (cancel != nullptr && cancel->load())
      return false;
    if (progress && (ip & 63) == 0)
      progress(p0 + (p1 - p0) * (double)ip / (double)pings.size());
    const size_t i = pings[ip];
    if ((int)i >= c.ncols)
      continue;
    const WcPing &p = wc.pings[i];
    double lon, lat, sog, cog;
    line.nav_at(p.time_d, lon, lat, sog, cog);
    if (!std::isfinite(lon) || !std::isfinite(lat))
      continue;
    npings++;
    const double x = i < c.along.size() ? c.along[i] : (double)i;
    for (int k = 0; k < c.nz; k++) {
      const float v = values[i * (size_t)c.nz + (size_t)k];
      const double depth = (k + 0.5) * c.dz;
      if (!in_window(o, v) || (o.lasso_only && !o.lasso.contains(true, x, depth)))
        continue;
      Point pt;
      pt.lon = lon;
      pt.lat = lat;
      pt.z = (float)-depth;
      pt.value = v;
      pt.time_d = p.time_d;
      pt.ping_number = p.ping_number;
      pt.ping = (int)i;
      visit(pt);
    }
  }
  return true;
}

/*--------------------------------------------------------------------*/
/* binary output, little endian whatever the host */

bool host_little_endian() {
  const uint16_t one = 1;
  unsigned char b;
  memcpy(&b, &one, 1);
  return b == 1;
}

class Bytes {
 public:
  std::vector<unsigned char> data;
  void raw(const void *p, size_t n) {
    const unsigned char *c = static_cast<const unsigned char *>(p);
    if (little_)
      data.insert(data.end(), c, c + n);
    else
      for (size_t i = n; i > 0; i--)
        data.push_back(c[i - 1]);
  }
  void u8(unsigned v) { data.push_back((unsigned char)v); }
  void u16(uint16_t v) { raw(&v, 2); }
  void i16(int16_t v) { raw(&v, 2); }
  void u32(uint32_t v) { raw(&v, 4); }
  void i32(int32_t v) { raw(&v, 4); }
  void u64(uint64_t v) { raw(&v, 8); }
  void f32(float v) { raw(&v, 4); }
  void f64(double v) { raw(&v, 8); }
  void text(const std::string &s, size_t n) {  // fixed length, zero padded
    for (size_t i = 0; i < n; i++)
      data.push_back(i < s.size() ? (unsigned char)s[i] : 0);
  }
  void zeros(size_t n) { data.insert(data.end(), n, 0); }

 private:
  bool little_ = host_little_endian();
};

/* GPS-UTC leap seconds: the Unix times from which each applies (to 2017-01-01, 18 s) */
double gps_minus_utc(double time_d) {
  static const double steps[] = {362793600,  394329600,  425865600,  489024000,  567993600,  631152000,
                                 662688000,  709948800,  741484800,  773020800,  820454400,  867715200,
                                 915148800,  1136073600, 1230768000, 1341100800, 1435708800, 1483228800};
  int n = 0;
  for (double t : steps)
    if (time_d >= t)
      n++;
  return n;
}

/* LAS 1.4 "adjusted standard GPS time": GPS seconds since 1980-01-06 minus 1e9 */
double adjusted_gps_time(double time_d) { return time_d - 315964800.0 + gps_minus_utc(time_d) - 1.0e9; }

std::string utm_wkt(int zone, bool north) {
  char wkt[1024];
  snprintf(wkt, sizeof(wkt),
           "PROJCS[\"WGS 84 / UTM zone %d%c\",GEOGCS[\"WGS 84\",DATUM[\"WGS_1984\",SPHEROID[\"WGS 84\",6378137,"
           "298.257223563,AUTHORITY[\"EPSG\",\"7030\"]],AUTHORITY[\"EPSG\",\"6326\"]],PRIMEM[\"Greenwich\",0,"
           "AUTHORITY[\"EPSG\",\"8901\"]],UNIT[\"degree\",0.0174532925199433,AUTHORITY[\"EPSG\",\"9122\"]],"
           "AUTHORITY[\"EPSG\",\"4326\"]],PROJECTION[\"Transverse_Mercator\"],PARAMETER[\"latitude_of_origin\",0],"
           "PARAMETER[\"central_meridian\",%d],PARAMETER[\"scale_factor\",0.9996],PARAMETER[\"false_easting\","
           "500000],PARAMETER[\"false_northing\",%d],UNIT[\"metre\",1,AUTHORITY[\"EPSG\",\"9001\"]],"
           "AXIS[\"Easting\",EAST],AXIS[\"Northing\",NORTH],AUTHORITY[\"EPSG\",\"%d\"]]",
           zone, north ? 'N' : 'S', zone * 6 - 183, north ? 0 : 10000000, (north ? 32600 : 32700) + zone);
  return wkt;
}

/* the files of one object (fan or curtain) */
class Writers {
 public:
  ~Writers() { close(); }

  bool open(const Export3dOptions &o, const std::string &kind, size_t npoints, void *pj, double x0, double y0,
            int zone, bool north, Export3dResult &result, std::string *message) {
    pj_ = pj;
    x0_ = x0;
    y0_ = y0;
    o_ = &o;
    npoints_ = npoints;
    segments_ = kind == "fan";  // one vector per fan in mbgrdviz, one for the curtain
    const std::string base = o.prefix + "_" + kind;
    std::string name = o.name.empty() ? o.prefix.substr(o.prefix.find_last_of("/\\") + 1) : o.name;
    std::replace(name.begin(), name.end(), ' ', '_');
    name += "_" + kind;
    if (o.vec) {
      if (!open_file(base + ".vec", "w", vec_, result, message))
        return false;
      fprintf(vec_, "## Vector File Version 1.0\n## VECTORNAME %s\n## VECTORCOLOR 4\n## ROUTESIZE 1\n", name.c_str());
      fprintf(vec_, "## MIN %.1f\n## MAX %.1f\n", o.color_min, o.color_max);
    }
    const std::string crs = std::string("EPSG:") + std::to_string((north ? 32600 : 32700) + zone);
    if (o.ply) {
      if (!open_file(base + ".ply", "wb", ply_, result, message))
        return false;
      fprintf(ply_, "ply\nformat binary_little_endian 1.0\n");
      fprintf(ply_, "comment MB-System mbwcdview water column %s\n", kind.c_str());
      fprintf(ply_, "comment crs %s (WGS 84 / UTM zone %d%c)\n", crs.c_str(), zone, north ? 'N' : 'S');
      fprintf(ply_, "comment offset %.3f %.3f 0.000 (x y are easting northing minus it, z = -depth, m)\n", x0, y0);
      fprintf(ply_, "comment amplitude in dB, colours from %.1f to %.1f dB\n", o.color_min, o.color_max);
      fprintf(ply_,
              "element vertex %zu\nproperty float x\nproperty float y\nproperty float z\nproperty uchar red\n"
              "property uchar green\nproperty uchar blue\nproperty float amplitude\nproperty uint ping\n"
              "end_header\n",
              npoints);
    }
    if (o.las) {
      if (!open_file(base + ".las", "wb", las_, result, message))
        return false;
      wkt_ = utm_wkt(zone, north);
      if (!write_las_header())
        return fail(base + ".las", message);
    }
    return true;
  }

  void add(const Point &p) {
    if (vec_ != nullptr) {
      if (segments_ && p.ping != last_ping_ && written_ > 0)
        fputs(">\n", vec_);
      fprintf(vec_, "%.8f %.8f %.2f %.1f\n", p.lon, p.lat, p.z, p.value);
    }
    last_ping_ = p.ping;
    written_++;
    if (ply_ == nullptr && las_ == nullptr)
      return;
    double e = 0.0, n = 0.0;
    int error = MB_ERROR_NO_ERROR;
    mb_proj_forward(0, pj_, p.lon, p.lat, &e, &n, &error);
    const unsigned char *rgb = color(p.value);
    if (ply_ != nullptr) {
      ply_bytes_.f32((float)(e - x0_));
      ply_bytes_.f32((float)(n - y0_));
      ply_bytes_.f32(p.z);
      ply_bytes_.u8(rgb[0]);
      ply_bytes_.u8(rgb[1]);
      ply_bytes_.u8(rgb[2]);
      ply_bytes_.f32(p.value);
      ply_bytes_.u32(p.ping_number);
      flush(ply_bytes_, ply_);
    }
    if (las_ != nullptr) {
      const double scale = 0.001;
      las_bytes_.i32((int32_t)std::lround((e - x0_) / scale));
      las_bytes_.i32((int32_t)std::lround((n - y0_) / scale));
      las_bytes_.i32((int32_t)std::lround(p.z / scale));
      const double f = (p.value - o_->color_min) / (o_->color_max - o_->color_min);
      las_bytes_.u16((uint16_t)std::lround(65535.0 * std::min(1.0, std::max(0.0, std::isfinite(f) ? f : 0.0))));
      las_bytes_.u8(0x11);  // return 1 of 1
      las_bytes_.u8(0);     // classification flags, scanner channel, scan direction, edge
      las_bytes_.u8(1);     // unclassified
      las_bytes_.u8(0);     // user data
      las_bytes_.i16(0);    // scan angle
      las_bytes_.u16(0);    // point source id
      las_bytes_.f64(adjusted_gps_time(p.time_d));
      for (int c = 0; c < 3; c++)
        las_bytes_.u16((uint16_t)(rgb[c] * 257));
      las_bytes_.f32(p.value);
      las_bytes_.u32(p.ping_number);
      if (written_ == 1) {
        min_[0] = max_[0] = e;
        min_[1] = max_[1] = n;
        min_[2] = max_[2] = p.z;
      }
      const double xyz[3] = {e, n, p.z};
      for (int c = 0; c < 3; c++) {
        min_[c] = std::min(min_[c], xyz[c]);
        max_[c] = std::max(max_[c], xyz[c]);
      }
      flush(las_bytes_, las_);
    }
  }

  /* complete the files; false if a write failed or fewer points came than announced */
  bool finish(std::string *message) {
    bool ok = written_ == npoints_;
    if (!ok && message != nullptr)
      *message = "point count changed during the export";
    flush(ply_bytes_, ply_, true);
    flush(las_bytes_, las_, true);
    if (las_ != nullptr) {
      /* the bounds, in the header from byte 179 */
      Bytes b;
      b.f64(max_[0]);
      b.f64(min_[0]);
      b.f64(max_[1]);
      b.f64(min_[1]);
      b.f64(max_[2]);
      b.f64(min_[2]);
      if (fseek(las_, 179, SEEK_SET) != 0 || fwrite(b.data.data(), 1, b.data.size(), las_) != b.data.size())
        failed_ = true;
    }
    if (!close())
      failed_ = true;
    if (failed_ && message != nullptr)
      *message = "cannot write " + failed_path_;
    return ok && !failed_;
  }

 private:
  bool open_file(const std::string &path, const char *mode, FILE *&f, Export3dResult &result, std::string *message) {
    f = fopen(path.c_str(), mode);
    if (f == nullptr) {
      if (message != nullptr)
        *message = "cannot write " + path + ": " + strerror(errno);
      return false;
    }
    paths_.push_back(path);
    result.files.push_back(path);
    return true;
  }

  bool fail(const std::string &path, std::string *message) {
    if (message != nullptr)
      *message = "cannot write " + path;
    return false;
  }

  void flush(Bytes &b, FILE *f, bool all = false) {
    if (f == nullptr || (!all && b.data.size() < (1u << 20)))
      return;
    if (!b.data.empty() && fwrite(b.data.data(), 1, b.data.size(), f) != b.data.size() && !failed_) {
      failed_ = true;
      failed_path_ = f == ply_ ? "the PLY file" : "the LAS file";
    }
    b.data.clear();
  }

  bool close() {
    bool ok = true;
    for (FILE **f : {&vec_, &ply_, &las_})
      if (*f != nullptr) {
        if (ferror(*f) != 0 || fclose(*f) != 0) {
          ok = false;
          if (failed_path_.empty())
            failed_path_ = "the export files";
        }
        *f = nullptr;
      }
    return ok;
  }

  const unsigned char *color(float value) const {
    const double f = (value - o_->color_min) / (o_->color_max - o_->color_min);
    const int i = std::isfinite(f) ? std::min(255, std::max(0, (int)std::floor(f * 256.0))) : 0;
    return &o_->colors[(size_t)i * 3];
  }

  /* LAS 1.4, point format 7 with two extra bytes attributes, the CRS as OGC WKT */
  bool write_las_header() {
    const uint16_t header_size = 375, record_length = 36 + 8;
    const size_t wkt_length = wkt_.size() + 1;
    const uint32_t offset = header_size + (54 + (uint32_t)wkt_length) + (54 + 2 * 192);
    time_t now = time(nullptr);
    struct tm t;
#ifdef _WIN32
    gmtime_s(&t, &now);
#else
    gmtime_r(&now, &t);
#endif
    Bytes b;
    b.text("LASF", 4);
    b.u16(0);                 // file source id
    b.u16(0x0001 | 0x0010);   // adjusted standard GPS time, CRS as WKT
    b.zeros(16);              // project id
    b.u8(1);
    b.u8(4);
    b.text("MB-System", 32);
    b.text("mbwcdview", 32);
    b.u16((uint16_t)(t.tm_yday + 1));
    b.u16((uint16_t)(t.tm_year + 1900));
    b.u16(header_size);
    b.u32(offset);
    b.u32(2);                 // variable length records
    b.u8(7);                  // point data format
    b.u16(record_length);
    b.u32(0);                 // legacy point count: 0 for formats 6 to 10
    b.zeros(5 * 4);           // legacy points by return
    b.f64(0.001);
    b.f64(0.001);
    b.f64(0.001);
    b.f64(x0_);
    b.f64(y0_);
    b.f64(0.0);
    b.zeros(6 * 8);           // bounds, written at the end
    b.u64(0);                 // waveform data
    b.u64(0);                 // first extended variable length record
    b.u32(0);
    b.u64(npoints_);
    b.u64(npoints_);          // points by return: all first returns
    b.zeros(14 * 8);
    /* the coordinate system */
    b.u16(0);
    b.text("LASF_Projection", 16);
    b.u16(2112);
    b.u16((uint16_t)wkt_length);
    b.text("OGC coordinate system WKT", 32);
    b.text(wkt_, wkt_length);
    /* the extra bytes: amplitude (float, dB) and ping number (unsigned long) */
    b.u16(0);
    b.text("LASF_Spec", 16);
    b.u16(4);
    b.u16(2 * 192);
    b.text("Extra bytes", 32);
    const struct {
      unsigned type;
      const char *name, *description;
    } extra[2] = {{9, "amplitude", "water column amplitude (dB)"}, {5, "ping", "ping number"}};
    for (const auto &e : extra) {
      b.zeros(2);
      b.u8(e.type);
      b.u8(0);  // options: no no_data, min, max, scale or offset
      b.text(e.name, 32);
      b.zeros(4);
      b.zeros(120);
      b.text(e.description, 32);
    }
    if (b.data.size() != offset)
      return false;
    return fwrite(b.data.data(), 1, b.data.size(), las_) == b.data.size();
  }

  const Export3dOptions *o_ = nullptr;
  void *pj_ = nullptr;
  double x0_ = 0.0, y0_ = 0.0;
  size_t npoints_ = 0, written_ = 0;
  int last_ping_ = -1;
  bool segments_ = false;
  FILE *vec_ = nullptr, *ply_ = nullptr, *las_ = nullptr;
  Bytes ply_bytes_, las_bytes_;
  std::string wkt_;
  double min_[3] = {0, 0, 0}, max_[3] = {0, 0, 0};
  bool failed_ = false;
  std::string failed_path_;
  std::vector<std::string> paths_;
};

bool check_options(const Line &line, const Export3dOptions &o, std::string *message) {
  std::string why;
  if (!line.has_wc || line.wc.size() == 0)
    why = "no water column data";
  else if (!o.fan && !o.curtain)
    why = "neither fan nor curtain chosen";
  else if (o.first_ping > o.last_ping || o.last_ping < 0 || o.first_ping >= (int)line.wc.size())
    why = "no pings in the range";
  else if (o.lasso_only && o.lasso.empty())
    why = "no lasso";
  else if (!(o.value_min <= o.value_max))
    why = "empty value range";
  if (!why.empty() && message != nullptr)
    *message = why;
  return why.empty();
}

/* count the points of both objects; *curtain built here when needed and not given */
bool count_all(const Line &line, const Export3dOptions &o, Export3dResult &result, const Curtain *&curtain,
               Curtain &built, std::vector<double> &along, const std::atomic<bool> *cancel,
               const Export3dProgress &progress, double scale, std::string *message) {
  result = Export3dResult();
  if (!check_options(line, o, message))
    return false;
  if (o.curtain && curtain == nullptr) {
    if (!build_curtain(line, o.beams, built, cancel, message))
      return false;
    curtain = &built;
  }
  if (curtain != nullptr)
    along = curtain->along;
  else
    along_track(line, along);
  const std::vector<size_t> pings = selected_pings(line, o);
  auto counter = [](Export3dCount &c) {
    return [&c](const Point &p) {
      if (c.points == 0)
        c.value_min = c.value_max = p.value;
      c.value_min = std::min(c.value_min, (double)p.value);
      c.value_max = std::max(c.value_max, (double)p.value);
      c.points++;
    };
  };
  auto finish = [&o](Export3dCount &c) {
    c.stride = o.budget > 0 && c.points > o.budget ? (c.points + o.budget - 1) / o.budget : 1;
    c.written = (c.points + c.stride - 1) / c.stride;
  };
  if (o.fan) {
    if (!visit_fans(line, o, pings, along, cancel, progress, 0.0, scale * (o.curtain ? 0.9 : 1.0), result.fan.pings,
                    counter(result.fan)))
      return false;
    finish(result.fan);
  }
  if (o.curtain) {
    if (!visit_curtain(line, o, pings, *curtain, cancel, progress, scale * (o.fan ? 0.9 : 0.0), scale,
                       result.curtain.pings, counter(result.curtain)))
      return false;
    finish(result.curtain);
  }
  return true;
}

}  // namespace

/*--------------------------------------------------------------------*/

bool count_3d(const Line &line, const Export3dOptions &options, Export3dResult &result, const Curtain *curtain,
              const std::atomic<bool> *cancel, const Export3dProgress &progress, std::string *message) {
  Curtain built;
  std::vector<double> along;
  const bool ok = count_all(line, options, result, curtain, built, along, cancel, progress, 1.0, message);
  if (ok && progress)
    progress(1.0);
  return ok;
}

bool export_3d(const Line &line, const Export3dOptions &options, Export3dResult &result, const Curtain *curtain,
               const std::atomic<bool> *cancel, const Export3dProgress &progress, std::string *message) {
  const Export3dOptions &o = options;
  if (o.prefix.empty()) {
    if (message != nullptr)
      *message = "no output name";
    return false;
  }
  if (!o.vec && !o.ply && !o.las) {
    if (message != nullptr)
      *message = "no output format chosen";
    return false;
  }
  Curtain built;
  std::vector<double> along;
  if (!count_all(line, o, result, curtain, built, along, cancel, progress, 0.4, message))
    return false;
  const std::vector<size_t> pings = selected_pings(line, o);

  /* UTM zone and offset from the first selected ping with a position */
  void *pj = nullptr;
  double x0 = 0.0, y0 = 0.0;
  int zone = 0;
  bool north = true;
  if (o.ply || o.las) {
    double lon = NAN, lat = NAN, sog, cog;
    for (size_t i : pings) {
      line.nav_at(line.wc.pings[i].time_d, lon, lat, sog, cog);
      if (std::isfinite(lon) && std::isfinite(lat))
        break;
    }
    if (!std::isfinite(lon) || !std::isfinite(lat)) {
      if (message != nullptr)
        *message = "no positions in the chosen pings";
      return false;
    }
    double reference_lon = lon;
    if (reference_lon < 180.0)
      reference_lon += 360.0;
    if (reference_lon >= 180.0)
      reference_lon -= 360.0;
    zone = std::min(60, std::max(1, (int)(((reference_lon + 183.0) / 6.0) + 0.5)));
    north = lat >= 0.0;
    mb_path projection;
    snprintf(projection, sizeof(projection), "UTM%2.2d%c", zone, north ? 'N' : 'S');
    int error = MB_ERROR_NO_ERROR;
    if (mb_proj_init(0, projection, &pj, &error) != MB_SUCCESS || pj == nullptr) {
      if (message != nullptr)
        *message = std::string("cannot initialise projection ") + projection;
      return false;
    }
    double e, n;
    mb_proj_forward(0, pj, lon, lat, &e, &n, &error);
    x0 = std::floor(e / 1000.0) * 1000.0;
    y0 = std::floor(n / 1000.0) * 1000.0;
    result.crs = std::string("EPSG:") + std::to_string((north ? 32600 : 32700) + zone);
  }

  bool ok = true;
  for (int object = 0; object < 2 && ok; object++) {
    const bool fan = object == 0;
    if ((fan && !o.fan) || (!fan && !o.curtain))
      continue;
    const Export3dCount &count = fan ? result.fan : result.curtain;
    if (count.written == 0)
      continue;
    Writers writers;
    if (!writers.open(o, fan ? "fan" : "curtain", count.written, pj, x0, y0, zone, north, result, message)) {
      ok = false;
      break;
    }
    size_t index = 0;
    const size_t stride = count.stride;
    const Visit write = [&writers, &index, stride](const Point &p) {
      if (index++ % stride == 0)
        writers.add(p);
    };
    int npings = 0;
    const double p0 = (fan || !o.fan) ? 0.4 : 0.7, p1 = (fan && o.curtain) ? 0.7 : 1.0;
    ok = fan ? visit_fans(line, o, pings, along, cancel, progress, p0, p1, npings, write)
             : visit_curtain(line, o, pings, curtain != nullptr ? *curtain : built, cancel, progress, p0, p1, npings,
                             write);
    if (!ok && message != nullptr)
      *message = "cancelled";
    if (!writers.finish(ok ? message : nullptr))
      ok = false;
  }
  if (pj != nullptr) {
    int error;
    mb_proj_free(0, &pj, &error);
  }
  if (ok && progress)
    progress(1.0);
  return ok;
}

}  // namespace wcd
