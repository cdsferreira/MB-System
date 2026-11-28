#!/usr/bin/env python3
#--------------------------------------------------------------------
#    The MB-system:  mbwcdview_test.py  1/11/2026
#
#    Copyright (c) 2026 by
#    Christian dos Santos Ferreira
#      MARUM
#      University of Bremen
#      Bremen Germany
#    David W. Caress (caress@mbari.org)
#      Monterey Bay Aquarium Research Institute
#      Moss Landing, California, USA
#    Dale N. Chayes
#      Center for Coastal and Ocean Mapping
#      University of New Hampshire
#      Durham, New Hampshire, USA
#
#    See README.md file for copying and redistribution conditions.
#--------------------------------------------------------------------
"""mbwcdview (C++/Qt6) without a window: usage, --dump and --export on a synthetic
*.kmall/*.kmwcd pair, *.wcd file and Reson 7k *.s7k file. Run only when built with
-DbuildQt6Tools=ON."""

import math
import os
import re
import struct
import subprocess
import sys
import tempfile
import unittest
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import synth_all as A  # noqa: E402
import synth_kmall as S  # noqa: E402
import synth_s7k as R  # noqa: E402


def read_png(path):
  """Width, height, bytes per pixel and rows of an 8 bit RGB or RGBA PNG without interlace, as Qt writes them."""
  with open(path, "rb") as f:
    data = f.read()
  pos, header, idat = 8, None, b""
  while pos < len(data):
    length, kind = struct.unpack(">I4s", data[pos:pos + 8])
    if kind == b"IHDR":
      header = struct.unpack(">IIBBBBB", data[pos + 8:pos + 8 + length])
    elif kind == b"IDAT":
      idat += data[pos + 8:pos + 8 + length]
    pos += 12 + length
  width, height, depth, color, _, _, interlace = header
  if depth != 8 or color not in (2, 6) or interlace != 0:
    raise ValueError("unsupported PNG %s" % (header,))
  bpp = 3 if color == 2 else 4
  stride = width * bpp
  raw = zlib.decompress(idat)
  rows, prev = [], bytearray(stride)
  for y in range(height):
    start = y * (stride + 1)
    kind, line = raw[start], bytearray(raw[start + 1:start + 1 + stride])
    for i in range(stride):
      a = line[i - bpp] if i >= bpp else 0
      b = prev[i]
      c = prev[i - bpp] if i >= bpp else 0
      if kind == 1:
        line[i] = (line[i] + a) & 0xff
      elif kind == 2:
        line[i] = (line[i] + b) & 0xff
      elif kind == 3:
        line[i] = (line[i] + ((a + b) >> 1)) & 0xff
      elif kind == 4:
        p = a + b - c
        pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
        line[i] = (line[i] + (a if pa <= pb and pa <= pc else (b if pb <= pc else c))) & 0xff
    rows.append(line)
    prev = line
  return width, height, bpp, rows


def read_vec(path):
  """Header lines and segments (lists of lon, lat, z, value) of an mbgrdviz vector file."""
  header, segments = [], [[]]
  with open(path) as f:
    for line in f:
      if line.startswith("##"):
        header.append(line.strip())
      elif line.startswith(">"):
        segments.append([])
      else:
        segments[-1].append(tuple(float(v) for v in line.split()))
  return header, [seg for seg in segments if seg]


def read_ply(path):
  """Header lines and records (x, y, z, r, g, b, amplitude, ping) of a PLY file written by mbwcdview."""
  with open(path, "rb") as f:
    data = f.read()
  end = data.index(b"end_header\n") + len(b"end_header\n")
  header = data[:end].decode().splitlines()
  size = struct.calcsize("<fffBBBfI")
  records = [struct.unpack_from("<fffBBBfI", data, end + i * size) for i in range((len(data) - end) // size)]
  return header, records, (len(data) - end) % size


def read_las(path):
  """Header fields, variable length records and points (x, y, z, intensity, gps time, r, g, b, amplitude, ping)
  of a LAS 1.4 point format 7 file with two extra attributes."""
  with open(path, "rb") as f:
    data = f.read()
  h = {"signature": data[0:4], "encoding": struct.unpack_from("<H", data, 6)[0],
       "version": struct.unpack_from("<BB", data, 24), "header_size": struct.unpack_from("<H", data, 94)[0]}
  h["offset"], h["nvlr"], h["format"], h["length"] = struct.unpack_from("<IIBH", data, 96)
  h["scale"] = struct.unpack_from("<3d", data, 131)
  h["origin"] = struct.unpack_from("<3d", data, 155)
  h["bounds"] = struct.unpack_from("<6d", data, 179)  # max x, min x, max y, min y, max z, min z
  h["count"] = struct.unpack_from("<Q", data, 247)[0]
  vlrs, pos = [], h["header_size"]
  for _ in range(h["nvlr"]):
    user, record, length = struct.unpack_from("<16sHH", data, pos + 2)
    vlrs.append((user.rstrip(b"\0").decode(), record, data[pos + 54:pos + 54 + length]))
    pos += 54 + length
  points = []
  for i in range(h["count"]):
    o = h["offset"] + i * h["length"]
    x, y, z, intensity = struct.unpack_from("<iiiH", data, o)
    gps, r, g, b, amplitude, ping = struct.unpack_from("<dHHHfI", data, o + 22)
    points.append((h["origin"][0] + x * h["scale"][0], h["origin"][1] + y * h["scale"][1],
                   h["origin"][2] + z * h["scale"][2], intensity, gps, r, g, b, amplitude, ping))
  return h, vlrs, points, len(data) - h["offset"] - h["count"] * h["length"]


def local_metres(lon, lat, lon0, lat0):
  """East and north (m) of lon, lat from lon0, lat0 on a sphere, good over a few hundred metres."""
  return (lon - lon0) * 111194.9 * math.cos(math.radians(lat0)), (lat - lat0) * 111194.9


class MbwcdviewTest(unittest.TestCase):

  @classmethod
  def setUpClass(cls):
    cls.program = os.environ["MBWCDVIEW"]
    cls.tmp = tempfile.TemporaryDirectory()
    cls.kmall, cls.kmwcd = S.write_line(cls.tmp.name)

  @classmethod
  def tearDownClass(cls):
    cls.tmp.cleanup()

  def test_usage(self):
    result = subprocess.run([self.program, "-H"], capture_output=True, text=True)
    self.assertEqual(result.returncode, 0)
    self.assertIn("usage: mbwcdview", result.stderr)

  def test_dump(self):
    out = subprocess.run([self.program, "--dump", "-I", self.kmwcd, "--ping=1"], capture_output=True, text=True,
                         check=True).stdout
    self.assertIn(S.KMALL_NAME, out)
    self.assertIn("water column pings %d, matched %d" % (S.NPINGS - 1, S.NPINGS - 1), out)
    self.assertIn("fill value -94.0 dB, amplitude -55.0 .. -10.0 dB", out)
    self.assertIn("ping number %d" % (S.PING0 + 2), out)
    ratio = re.search(r"straight ([0-9.]+), ray traced ([0-9.]+)", out)
    self.assertIsNotNone(ratio, out)
    # flat seafloor, constant sound speed: both geometries agree and put the detections on the
    # soundings, apart from the 1-2 deg along track tilt of the sectors (cos 2 deg = 0.9994)
    self.assertAlmostEqual(float(ratio.group(1)), float(ratio.group(2)), places=4)
    self.assertAlmostEqual(float(ratio.group(1)), 1.0, delta=0.001)

  def test_export(self):
    image = os.path.join(self.tmp.name, "ping.png")
    subprocess.run([self.program, "-I", self.kmall, "--ping=1", "--bottom", "--wc-bottom", "--export=" + image,
                    "--size=800x600"], check=True)
    with open(image, "rb") as f:
      self.assertEqual(f.read(8), b"\x89PNG\r\n\x1a\n")

  def test_export_draws_swath(self):
    # the swath is drawn on the graphics card (QRhi or OpenGL): the image area of the
    # plot holds many colours of the colour map, where axes and labels have none
    image = os.path.join(self.tmp.name, "swath.png")
    subprocess.run([self.program, "-I", self.kmall, "--ping=1", "--export=" + image, "--size=400x300"], check=True)
    width, height, bpp, rows = read_png(image)
    # image area of WcdPlot::plotRect(): margins 72 left, 104 right, 16 top, 46 + 20 bottom
    colours = set()
    for y in range(16 + 4, height - 66 - 4):
      row = rows[y]
      for x in range(72 + 4, width - 104 - 4):
        r, g, b = row[x * bpp], row[x * bpp + 1], row[x * bpp + 2]
        if max(r, g, b) - min(r, g, b) > 16:
          colours.add((r, g, b))
    self.assertGreater(len(colours), 20)

  def export_3d(self, name, *options):
    """Run a 3D export of the synthetic line into the temporary directory; the prefix of its files."""
    prefix = os.path.join(self.tmp.name, name)
    result = subprocess.run([self.program, "-I", self.kmall, "--export-3d-name=" + prefix] + list(options),
                            capture_output=True, text=True, timeout=120)
    self.assertEqual(result.returncode, 0, result.stderr)
    return prefix, result.stderr

  def test_export_3d_formats(self):
    """The fans of all water column pings and the curtain in the three formats agree point for point."""
    prefix, _ = self.export_3d("all3d", "--export-3d=both", "--pings=0/%d" % (S.NPINGS - 2),
                               "--format-3d=vec,ply,las")
    for kind in ("fan", "curtain"):
      header, segments = read_vec(prefix + "_%s.vec" % kind)
      self.assertIn("## Vector File Version 1.0", header)
      self.assertIn("## MIN -55.0", header)
      self.assertIn("## MAX -10.0", header)
      points = [p for seg in segments for p in seg]
      self.assertGreater(len(points), 100)
      # one segment per ping for the fans; the fill value (-94 dB) is no data before the first echo of a beam, and
      # data within it, as in the swath view: sample INNER_FILL of beam 1 of the first ping, in both swaths
      self.assertEqual(len(segments), S.NPINGS - 1 if kind == "fan" else 1)
      inner_fill = [p for p in points if p[3] == -94.0]
      self.assertEqual(len(inner_fill), 2 if kind == "fan" else 0)
      self.assertTrue(all(-55.0 <= p[3] <= -10.0 for p in points if p[3] != -94.0))
      self.assertTrue(all(p[2] < 0.0 for p in points))
      ply_header, records, rest = read_ply(prefix + "_%s.ply" % kind)
      self.assertEqual(rest, 0)
      self.assertIn("element vertex %d" % len(points), ply_header)
      self.assertEqual(len(records), len(points))
      h, vlrs, las, rest = read_las(prefix + "_%s.las" % kind)
      self.assertEqual((h["signature"], h["version"], h["format"], h["length"], h["header_size"]),
                       (b"LASF", (1, 4), 7, 44, 375))
      self.assertEqual(h["encoding"], 0x11)
      self.assertEqual(rest, 0)
      self.assertEqual(h["count"], len(points))
      self.assertEqual([(u, r) for u, r, _ in vlrs], [("LASF_Projection", 2112), ("LASF_Spec", 4)])
      wkt = vlrs[0][2].decode()
      self.assertIn("UTM zone", wkt)
      ply_offset = [line for line in ply_header if line.startswith("comment offset")][0].split()
      self.assertEqual((float(ply_offset[2]), float(ply_offset[3])), h["origin"][:2])
      for c, (hi, lo) in enumerate(((0, 1), (2, 3), (4, 5))):
        values = [p[c] for p in las]
        self.assertAlmostEqual(max(values), h["bounds"][hi], delta=0.001)
        self.assertAlmostEqual(min(values), h["bounds"][lo], delta=0.001)
      lon0, lat0 = points[0][0], points[0][1]
      for v, r, l in zip(points, records, las):
        self.assertAlmostEqual(v[2], r[2], delta=0.006)
        self.assertAlmostEqual(v[2], l[2], delta=0.006)
        self.assertAlmostEqual(v[3], l[8], delta=0.051)
        self.assertEqual(r[7], l[9])
        self.assertEqual((r[3] * 257, r[4] * 257, r[5] * 257), l[5:8])
        self.assertAlmostEqual(r[0] + h["origin"][0], l[0], delta=0.002)
        self.assertAlmostEqual(r[1] + h["origin"][1], l[1], delta=0.002)
      # the projected distances are those of the longitudes and latitudes (UTM scale within 0.1 %)
      far = max(range(len(points)), key=lambda i: math.hypot(*local_metres(points[i][0], points[i][1], lon0, lat0)))
      d_geo = math.hypot(*local_metres(points[far][0], points[far][1], lon0, lat0))
      d_utm = math.hypot(las[far][0] - las[0][0], las[far][1] - las[0][1])
      self.assertGreater(d_geo, 5.0)
      self.assertAlmostEqual(d_utm / d_geo, 1.0, delta=0.002)
      # the ping numbers and the times of the pings
      pings = sorted(set(l[9] for l in las))
      self.assertEqual(pings, list(range(S.PING0 + 1, S.PING0 + S.NPINGS)))
      for l in las[:20]:
        # adjusted standard GPS time: 18 leap seconds in 2025
        self.assertAlmostEqual(l[4] + 1.0e9 + 315964800.0 - 18.0, S.ping_time(l[9]), delta=0.05)

  def test_export_3d_fan_geometry(self):
    """A fan stands across the track at its ping's position, starboard to the right of the heading."""
    lasso = os.path.join(self.tmp.name, "sides.lasso")
    sides = {}
    for side, x0, x1 in (("starboard", 0.5, 1.0e5), ("port", -1.0e5, -0.5)):
      with open(lasso, "w") as f:
        f.write("# one side of the fans\nfan\n%g 0\n%g 0\n%g 1e5\n%g 1e5\n" % (x0, x1, x1, x0))
      prefix, _ = self.export_3d("side_" + side, "--export-3d=fan", "--ping=0", "--lasso=" + lasso)
      _, segments = read_vec(prefix + "_fan.vec")
      sides[side] = segments[0]
    lat0, lon0 = S.position(S.ping_time(S.PING0 + 1))
    h = math.radians(S.HEADING)
    for side, sign in (("starboard", 1.0), ("port", -1.0)):
      self.assertGreater(len(sides[side]), 20)
      for p in sides[side]:
        e, n = local_metres(p[0], p[1], lon0, lat0)
        across, along = e * math.cos(h) - n * math.sin(h), e * math.sin(h) + n * math.cos(h)
        self.assertGreater(sign * across, 0.4, (side, p))
        self.assertLess(abs(along), 0.05 * abs(across) + 0.5, (side, p))

  def test_export_3d_range_budget(self):
    prefix, _ = self.export_3d("range3d", "--export-3d=fan", "--pings=0/3", "--range=-30/-20")
    header, segments = read_vec(prefix + "_fan.vec")
    points = [p for seg in segments for p in seg]
    self.assertIn("## MIN -30.0", header)
    self.assertGreater(len(points), 20)
    self.assertTrue(all(-30.0 <= p[3] <= -20.0 for p in points))
    _, all_segments = read_vec(self.export_3d("range3d_all", "--export-3d=fan", "--pings=0/3")[0] + "_fan.vec")
    n = sum(len(seg) for seg in all_segments)
    self.assertLess(len(points), n)
    budget = n // 7
    prefix, err = self.export_3d("budget3d", "--export-3d=fan", "--pings=0/3", "--budget=%d" % budget, "-V")
    _, segments = read_vec(prefix + "_fan.vec")
    stride = -(-n // budget)
    self.assertEqual(sum(len(seg) for seg in segments), -(-n // stride))
    self.assertIn("fan: %d points of 4 pings" % n, err)
    self.assertIn("(stride %d)" % stride, err)
    # every second ping: two segments
    _, segments = read_vec(self.export_3d("step3d", "--export-3d=fan", "--pings=0/3/2")[0] + "_fan.vec")
    self.assertEqual(len(segments), 2)

  def test_export_3d_lasso(self):
    """Fan polygons select by across track distance and depth, curtain polygons by along track distance and depth."""
    lasso = os.path.join(self.tmp.name, "depth.lasso")
    depth = S.DRAFT + 0.5 * S.BOTTOM_BELOW_TX
    with open(lasso, "w") as f:
      f.write("fan\n-1e5 0\n1e5 0\n1e5 %g\n-1e5 %g\n" % (depth, depth))
    prefix, _ = self.export_3d("lasso3d", "--export-3d=both", "--pings=0/3", "--lasso=" + lasso)
    _, segments = read_vec(prefix + "_fan.vec")
    points = [p for seg in segments for p in seg]
    self.assertGreater(len(points), 20)
    self.assertTrue(all(p[2] >= -depth for p in points))
    # no curtain polygon: no curtain cells
    self.assertFalse(os.path.exists(prefix + "_curtain.vec"))
    # a curtain polygon over the first metres of the track and the upper water: fans of the first ping only
    with open(lasso, "w") as f:
      f.write("curtain\n-1 0\n1 0\n1 %g\n-1 %g\n" % (depth, depth))
    prefix, err = self.export_3d("lasso3d_curtain", "--export-3d=both", "--pings=0/3", "--lasso=" + lasso, "-V")
    _, fan = read_vec(prefix + "_fan.vec")
    _, curtain = read_vec(prefix + "_curtain.vec")
    self.assertEqual(len(fan), 1)
    self.assertIn("fan: %d points of 4 pings" % len(fan[0]), err)
    self.assertTrue(all(p[2] >= -depth for p in fan[0] + curtain[0]))
    self.assertEqual(len(set((p[0], p[1]) for p in curtain[0])), 1)
    for bad in ("fan\n0 0\n1 1\n", "0 0\n", "fan\n0 zero\n"):
      with open(lasso, "w") as f:
        f.write(bad)
      result = subprocess.run([self.program, "-I", self.kmall, "--export-3d=fan", "--lasso=" + lasso,
                               "--export-3d-name=" + os.path.join(self.tmp.name, "bad3d")], capture_output=True,
                              text=True, timeout=60)
      self.assertNotEqual(result.returncode, 0, bad)

  def test_export_3d_options(self):
    for bad in ("--export-3d=side", "--format-3d=vec,xyz", "--pings=3/1", "--pings=0/2/0", "--range=-10/-30",
                "--budget=-5", "--range=low"):
      result = subprocess.run([self.program, "-I", self.kmall, bad], capture_output=True, text=True, timeout=60)
      self.assertNotEqual(result.returncode, 0, bad)
      self.assertIn("usage: mbwcdview", result.stderr, bad)

  def test_dump_curtain(self):
    """The curtain of the line: one column per water column ping at its along-track distance
    (the synthetic line moves 1e-5 deg in latitude and longitude per second)."""
    out = subprocess.run([self.program, "--dump", "-I", self.kmwcd, "--beams=4/11"], capture_output=True, text=True,
                         check=True).stdout
    found = re.search(r"curtain: beams (\d+) \.\. (\d+) of (\d+) per swath, (ray traced|straight rays), (\d+) columns x "
                      r"(\d+) cells of ([0-9.e-]+) m, filled (\d+), along track ([0-9.]+) m", out)
    self.assertIsNotNone(found, out)
    self.assertEqual(found.group(1, 2, 3), ("4", "11", str(S.NBEAMS)))
    self.assertEqual(int(found.group(5)), S.NPINGS - 1)
    self.assertGreater(int(found.group(8)), 0)
    # first to last water column ping (fan 0 times), flat earth as the curtain computes it
    t0, t1 = S.ping_time(S.PING0 + 1), S.ping_time(S.PING0 + S.NPINGS - 1)
    (lat0, lon0), (lat1, lon1) = S.position(t0), S.position(t1)
    dn = (lat1 - lat0) * 111320.0
    de = (lon1 - lon0) * 111320.0 * math.cos(math.radians(0.5 * (lat0 + lat1)))
    self.assertAlmostEqual(float(found.group(9)), math.hypot(dn, de), delta=0.05)
    # the deepest sample is within the cells
    self.assertGreater(int(found.group(6)) * float(found.group(7)), S.DRAFT + S.BOTTOM_BELOW_TX)
    all_beams = subprocess.run([self.program, "--dump", "-I", self.kmwcd], capture_output=True, text=True,
                               check=True).stdout
    self.assertIn("curtain: beams 0 .. %d of %d per swath" % (S.NBEAMS - 1, S.NBEAMS), all_beams)

  def test_curtain_options(self):
    image = os.path.join(self.tmp.name, "curtain.png")
    subprocess.run([self.program, "-I", self.kmall, "--beams=2/13", "--combine=max", "--palette=haxby",
                    "--exaggeration=2", "--export-curtain=" + image, "--size=800x400"], check=True)
    width, height, bpp, rows = read_png(image)
    self.assertEqual((width, height), (800, 400))
    for bad in ("--beams=5", "--beams=9/3", "--combine=sum", "--palette=jet", "--curtain=above",
                "--exaggeration=0"):
      result = subprocess.run([self.program, "-I", self.kmall, bad, "--export-curtain=" + image], capture_output=True,
                              text=True)
      self.assertNotEqual(result.returncode, 0, bad)
      self.assertIn("usage: mbwcdview", result.stderr, bad)

  def test_export_palette(self):
    # the swath in another palette: Grayscale has no colours, only greys
    image = os.path.join(self.tmp.name, "gray.png")
    subprocess.run([self.program, "-I", self.kmall, "--ping=1", "--palette=grayscale", "--export=" + image,
                    "--size=400x300"], check=True)
    self.assertEqual(len(self.plot_colours(image)), 0)

  def test_dump_datalist(self):
    """A datalist stands for its first file without a window; the two files of the line count once."""
    datalist = os.path.join(self.tmp.name, "lines.mb-1")
    with open(datalist, "w") as f:
      f.write("%s 261\n%s 261\n" % (self.kmall, self.kmwcd))
    out = subprocess.run([self.program, "--dump", "-I", datalist], capture_output=True, text=True,
                         check=True).stdout
    self.assertIn("datalist %s: 1 files, the first shown" % datalist, out)
    self.assertIn("water column pings %d, matched %d" % (S.NPINGS - 1, S.NPINGS - 1), out)

  def test_dump_wcd(self):
    wcd = A.write(os.path.join(self.tmp.name, A.WCD_NAME))
    out = subprocess.run([self.program, "--dump", "-I", wcd, "--ping=2"], capture_output=True, text=True,
                         check=True).stdout
    self.assertIn("water column pings %d, matched 0" % len(A.PINGS), out)
    self.assertIn("amplitude %.1f .. " % (0.5 * -128 - A.TVG_OFFSET), out)
    self.assertIn("fill value none", out)
    self.assertIn("ping number %d" % A.PINGS[2], out)
    self.assertIn("beams %d," % (A.nbeams_read(2) + 2), out)  # two empty beams mark the lost datagram

  def test_dump_s7k(self):
    """A *.s7k holds the survey pings and the water column; its soundings are the detections,
    which the water column gives in fractions of a sample, so the beams are compared with the
    soundings of their travel time (with an extra detection early in ping 0, by index most outer
    beams would get a neighbour's sounding, metres away)."""
    s7k = R.write(self.tmp.name)
    out = subprocess.run([self.program, "--dump", "-I", s7k, "--ping=1"], capture_output=True, text=True,
                         check=True).stdout
    nwc = R.NPINGS - len(R.NO_WC)
    self.assertIn("survey file: %s\nwater column file: %s" % (s7k, s7k), out)
    self.assertIn("survey pings %d," % R.NPINGS, out)
    self.assertIn("water column pings %d, matched %d" % (nwc, nwc), out)
    self.assertIn("fill value none", out)
    self.assertIn("ping number %d" % (R.PING0 + 1), out)
    # the sonar is far below the surface: the depth window starts just above it
    window = re.search(r"longest slant range ([0-9.]+) m, depth window from ([0-9.]+) m", out)
    self.assertIsNotNone(window, out)
    self.assertAlmostEqual(float(window.group(2)), R.SENSORDEPTH - 0.1 * float(window.group(1)), delta=0.11)
    ratio = re.search(r"straight ([0-9.]+), ray traced ([0-9.]+)", out)
    self.assertIsNotNone(ratio, out)
    # flat seafloor, constant sound speed, no profile: the detections lie on the soundings
    self.assertAlmostEqual(float(ratio.group(1)), 1.0, delta=1.0e-4)
    self.assertEqual(ratio.group(1), ratio.group(2))
    across = re.search(r"sounding\|: straight ([0-9.]+) m", out)
    self.assertIsNotNone(across, out)
    self.assertLess(float(across.group(1)), 0.05)
    self.assertIn("by travel time: %d (detections in fractions of a sample), 0 (" % R.NBEAMS, out)

  def plot_colours(self, image):
    """Colours in the image area of the plot (as in test_export_draws_swath)."""
    width, height, bpp, rows = read_png(image)
    colours = set()
    for y in range(16 + 4, height - 66 - 4):
      row = rows[y]
      for x in range(72 + 4, width - 104 - 4):
        r, g, b = row[x * bpp], row[x * bpp + 1], row[x * bpp + 2]
        if max(r, g, b) - min(r, g, b) > 16:
          colours.add((r, g, b))
    return colours

  def test_export_s7k(self):
    s7k = R.write(self.tmp.name)
    image = os.path.join(self.tmp.name, "s7k.png")
    subprocess.run([self.program, "-I", s7k, "--ping=0", "--bottom", "--wc-bottom", "--export=" + image,
                    "--size=400x300"], check=True)
    self.assertTrue(os.path.getsize(image) > 0)
    # without the overlays, whose markers alone have many colours: the swath is drawn
    subprocess.run([self.program, "-I", s7k, "--ping=0", "--export=" + image, "--size=400x300"], check=True)
    self.assertGreater(len(self.plot_colours(image)), 20)

  def test_dump_extra_detections(self):
    """With an extra detection in the first receive fan and a sound speed gradient, straight
    rays put the detections on the (straight ray) soundings and ray tracing through the
    gradient moves the outer ones away: the pairs found once serve both geometries (paired in
    each geometry the ray traced comparison found no soundings). test_dump_pairing checks
    the pairs themselves."""
    directory = os.path.join(self.tmp.name, "extra")
    os.mkdir(directory)
    kmall, kmwcd = S.write_line(directory, extra=True, extra_fan=0, svp_gradient=40.0)
    out = subprocess.run([self.program, "--dump", "-I", kmwcd, "--ping=1"], capture_output=True, text=True,
                         check=True).stdout
    across = re.search(r"sounding\|: straight ([0-9.na]+) m, ray traced ([0-9.na]+) m", out)
    self.assertIsNotNone(across, out)
    self.assertLess(float(across.group(1)), 0.5)
    self.assertGreater(float(across.group(2)), 5.0)

  def test_dump_pairing(self):
    """Soundings that a sonar's ray tracing moved toward nadir, here by a fifth of their across
    track distance, lie nearer in direction to another beam's detection than to their own;
    with the extra detection in the first fan they are not in beam order either. Each beam
    finds its own sounding by travel time, so the outer beams' water column detections (on
    straight rays) lie a fifth of their across track distance outside their soundings."""
    directory = os.path.join(self.tmp.name, "pairing")
    os.mkdir(directory)
    kmall, kmwcd = S.write_line(directory, extra=True, extra_fan=0, sounding_scale=0.8)
    out = subprocess.run([self.program, "--dump", "-I", kmwcd, "--ping=1"], capture_output=True, text=True,
                         check=True).stdout
    ratio = re.search(r"straight ([0-9.na]+), ray traced ([0-9.na]+)", out)
    self.assertIsNotNone(ratio, out)
    self.assertAlmostEqual(float(ratio.group(1)), 1.0, delta=0.001)
    across = re.search(r"sounding\|: straight ([0-9.na]+) m, ray traced ([0-9.na]+) m", out)
    self.assertIsNotNone(across, out)
    outer = 0.2 * S.BOTTOM_BELOW_TX * math.tan(math.radians(S.ANGLE_MAX))
    self.assertAlmostEqual(float(across.group(1)), outer, delta=1.0)
    self.assertEqual(across.group(1), across.group(2))  # constant sound speed: ray tracing changes nothing
    self.assertIn("by travel time: %d (detections in fractions of a sample), 0 (whole samples, ties by beam angle), "
                  "0 (whole samples, ties by direction)" % (2 * S.NBEAMS), out)

  def whole_samples_dump(self, name, **options):
    """--dump of the pairing line of test_dump_pairing with its water column detections in
    whole samples, as *.all and *.wcd files give them; returns the output and the outer beams'
    across track difference to expect when each beam takes its own sounding (the detection on
    straight rays at the whole sample before the sounding's travel time)."""
    directory = os.path.join(self.tmp.name, name)
    os.mkdir(directory)
    kmall, kmwcd = S.write_line(directory, extra=True, extra_fan=0, sounding_scale=0.8, whole_samples=True,
                                **options)
    out = subprocess.run([self.program, "--dump", "-I", kmwcd, "--ping=1"], capture_output=True, text=True,
                         check=True).stdout
    x = S.BOTTOM_BELOW_TX * math.tan(math.radians(S.ANGLE_MAX))
    det = S.detect_samples(0)
    return out, x * int(det) / det - 0.8 * x

  def test_dump_pairing_whole_samples(self):
    """Whole samples do not give the soundings' travel times exactly: each beam takes the
    sounding nearest in time, of those within a sample the one nearest in beam angle (the
    beam angles MBIO gives agree with those of the water column), its own."""
    out, outer = self.whole_samples_dump("whole")
    self.assertIn("by travel time: 0 (detections in fractions of a sample), %d (whole samples, ties by beam angle), "
                  "0 (whole samples, ties by direction)" % (2 * S.NBEAMS), out)
    across = re.search(r"sounding\|: straight ([0-9.na]+) m", out)
    self.assertIsNotNone(across, out)
    self.assertAlmostEqual(float(across.group(1)), outer, delta=1.0)

  def test_dump_pairing_whole_samples_angles_off(self):
    """As test_dump_pairing_whole_samples, with the soundings' beam angles 7.5 deg off those of
    the water column (as MBIO's for kmall, computed with the roll at the echo, can be): ties go
    by direction, and each beam still takes its own sounding."""
    out, outer = self.whole_samples_dump("whole_off", rx_angle_offset=7.5)
    self.assertIn("by travel time: 0 (detections in fractions of a sample), 0 (whole samples, ties by beam angle), "
                  "%d (whole samples, ties by direction)" % (2 * S.NBEAMS), out)
    across = re.search(r"sounding\|: straight ([0-9.na]+) m", out)
    self.assertIsNotNone(across, out)
    self.assertAlmostEqual(float(across.group(1)), outer, delta=1.0)

  def test_unmatched_deep(self):
    """A water column ping without its survey ping, from a sonar deep below the surface whose
    depth changes from ping to ping, is drawn at the transducer depth interpolated from the
    survey pings around it, inside the default depth window."""
    directory = os.path.join(self.tmp.name, "deep")
    os.mkdir(directory)
    kmall, kmwcd = S.write_line(directory, draft=3000.0, draft_step=10.0, skip_mrz=(S.PING0 + 2,))
    out = subprocess.run([self.program, "--dump", "-I", kmwcd, "--ping=1"], capture_output=True, text=True,
                         check=True).stdout
    self.assertIn("water column pings %d, matched %d" % (S.NPINGS - 1, S.NPINGS - 2), out)
    # half way between the pings before (3010 m) and after (3030 m) it
    self.assertIn("ping number %d" % (S.PING0 + 2), out)
    self.assertIn("transducer depth 3020.000 m (interpolated)", out)
    window = re.search(r"longest slant range ([0-9.]+) m, depth window from ([0-9.]+) m", out)
    self.assertIsNotNone(window, out)
    self.assertAlmostEqual(float(window.group(2)), 3000.0 - 0.1 * float(window.group(1)), delta=0.11)
    image = os.path.join(directory, "unmatched.png")
    subprocess.run([self.program, "-I", kmwcd, "--ping=1", "--export=" + image, "--size=400x300"], check=True)
    self.assertGreater(len(self.plot_colours(image)), 20)

  def test_dump_no_data(self):
    """An empty file or one without data of its format has neither survey nor water column:
    --dump fails with a message."""
    directory = os.path.join(self.tmp.name, "nodata")
    os.mkdir(directory)
    for name, content in (("empty.kmall", b""), ("garbage.kmall", b"no sonar data here\n" * 50),
                          ("empty.s7k", b""), ("garbage.s7k", b"no sonar data here\n" * 50)):
      path = os.path.join(directory, name)
      with open(path, "wb") as f:
        f.write(content)
      result = subprocess.run([self.program, "--dump", "-I", path], capture_output=True, text=True)
      self.assertNotEqual(result.returncode, 0, name)
      self.assertIn("no water column or survey data", result.stderr, name)

  def test_ship_window(self):
    """A ship's transducer is shallower than the longest slant range: the window starts at the
    surface."""
    directory = os.path.join(self.tmp.name, "ship")
    os.mkdir(directory)
    kmall, kmwcd = S.write_line(directory, draft=300.0)
    out = subprocess.run([self.program, "--dump", "-I", kmall], capture_output=True, text=True,
                         check=True).stdout
    window = re.search(r"longest slant range ([0-9.]+) m, depth window from ([0-9.]+) m", out)
    self.assertIsNotNone(window, out)
    self.assertGreater(300.0, 0.1 * float(window.group(1)))  # the rule before started below the surface
    self.assertEqual(float(window.group(2)), 0.0)

  def test_window(self):
    env = dict(os.environ, QT_QPA_PLATFORM="offscreen")
    result = subprocess.run([os.environ["MBWCDVIEW_WINDOW_TEST"], self.kmall, self.tmp.name], capture_output=True,
                            text=True, cwd=self.tmp.name, env=env, timeout=300)
    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
    self.assertIn(" 0 failed", result.stdout)

  def test_embed(self):
    # two copies of the line, because each tool opens its own files
    a, b = (os.path.join(self.tmp.name, name) for name in ("embed_a", "embed_b"))
    os.mkdir(a)
    os.mkdir(b)
    file_a = S.write_line(a)[0]
    file_b = S.write_line(b)[0]
    env = dict(os.environ, QT_QPA_PLATFORM="offscreen")
    result = subprocess.run([os.environ["MBWCDVIEW_EMBED_TEST"], file_a, file_b, self.tmp.name], capture_output=True,
                            text=True, cwd=self.tmp.name, env=env, timeout=300)
    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
    self.assertIn(" 0 failed", result.stdout)


if __name__ == "__main__":
  unittest.main()
