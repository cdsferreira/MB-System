/*--------------------------------------------------------------------
 *    The MB-system:  mbwcdview.cc  1/11/2026
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
 * MBWCDVIEW displays water column data, one swath at a time, read through MBIO.
 * Opening a Kongsberg *.kmwcd, *.kmall, *.wcd or *.all finds the other file of the line,
 * so the soundings, navigation and transducer depth of the *.kmall or *.all can be shown
 * with the water column of the *.kmwcd or *.wcd; Reson 7k *.s7k files hold both. The samples
 * are ray traced through the logged sound velocity profile. Without a window, --dump prints
 * a summary of a line and --export writes an image of one swath.
 *
 * Author:  C. dos Santos Ferreira
 * Date:    November 1, 2026
 */

#include <getopt.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include <QApplication>
#include <QImage>
#include <QString>

#include "mb_define.h"
#include "mb_status.h"
#include "wcd_curtain.h"
#include "wcd_curtain_view.h"
#include "wcd_data.h"
#include "wcd_dump.h"
#include "wcd_export3d.h"
#include "wcd_palette.h"
#include "wcd_plot.h"
#include "wcd_scene.h"
#include "wcd_window.h"

constexpr char program_name[] = "MBWCDVIEW";
constexpr char help_message[] =
    "MBWCDVIEW displays water column data, one swath at a time, read through MBIO.\n"
    "Opening a Kongsberg *.kmwcd, *.kmall, *.wcd or *.all finds the other file of the survey\n"
    "line, so that its soundings and navigation are shown with the water column; Reson 7k\n"
    "*.s7k files hold both. A datalist opens its first file, and Previous/Next file go\n"
    "through the others.\n"
    "The curtain shows the water column from the side: each ping is a column at its\n"
    "along-track position, combining by depth the samples of the beams in the range of\n"
    "--beams (counted within each swath), which the swath view dims outside of.\n"
    "--dump prints a summary of the line, --export writes an image of one swath and\n"
    "--export-curtain one of the curtain, all without opening a window.\n"
    "--export-3d writes the fans of the --pings and/or the curtain as 3D points, the samples\n"
    "in --range (dB) and, with --lasso, in its polygons: --format-3d mbgrdviz vector files\n"
    "(lon lat z value), PLY and LAS 1.4 (UTM), as <name>_fan.<ext> and <name>_curtain.<ext>.";
constexpr char usage_message[] =
    "mbwcdview [--input=file -Ifile --format=format -Fformat --ping=index --swath=index\n"
    "           --bottom --wc-bottom --straight --beams=first/last --combine=mean|max\n"
    "           --palette=viridis|haxby|grayscale|rainbow --curtain=below|beside|off\n"
    "           --exaggeration=value --export=image.png --export-curtain=image.png\n"
    "           --size=widthxheight --range=min/max\n"
    "           --export-3d=fan|curtain|both --export-3d-name=name --pings=current|first/last[/step]\n"
    "           --format-3d=vec,ply,las --budget=points --lasso=file\n"
    "           --dump --verbose -V --help -H]";

int main(int argc, char **argv) {
  static struct option options[] = {{"input", required_argument, nullptr, 'I'},
                                    {"format", required_argument, nullptr, 'F'},
                                    {"ping", required_argument, nullptr, 'P'},
                                    {"swath", required_argument, nullptr, 'S'},
                                    {"bottom", no_argument, nullptr, 'b'},
                                    {"wc-bottom", no_argument, nullptr, 'w'},
                                    {"straight", no_argument, nullptr, 's'},
                                    {"export", required_argument, nullptr, 'E'},
                                    {"export-curtain", required_argument, nullptr, 'C'},
                                    {"beams", required_argument, nullptr, 'B'},
                                    {"combine", required_argument, nullptr, 'M'},
                                    {"palette", required_argument, nullptr, 'L'},
                                    {"curtain", required_argument, nullptr, 'U'},
                                    {"exaggeration", required_argument, nullptr, 'X'},
                                    {"size", required_argument, nullptr, 'Z'},
                                    {"range", required_argument, nullptr, 'R'},
                                    {"export-3d", required_argument, nullptr, 'T'},
                                    {"export-3d-name", required_argument, nullptr, 'N'},
                                    {"pings", required_argument, nullptr, 'G'},
                                    {"format-3d", required_argument, nullptr, 'Y'},
                                    {"budget", required_argument, nullptr, 'Q'},
                                    {"lasso", required_argument, nullptr, 'O'},
                                    {"dump", no_argument, nullptr, 'D'},
                                    {"verbose", no_argument, nullptr, 'V'},
                                    {"help", no_argument, nullptr, 'H'},
                                    {nullptr, 0, nullptr, 0}};

  std::string input, export_file, export_curtain, export_3d_name, lasso_file;
  int format = 0, ping = 0, swath = -1, verbose = 0, width = 1400, height = 900;
  bool help = false, dump = false, errflg = false, combine_max = false;
  wcd::SceneOptions scene;
  wcd::CurtainOptions curtain;
  wcd::Palette palette = wcd::Palette::Viridis;
  wcd::CurtainLayout layout = wcd::CurtainLayout::Below;
  double exaggeration = 1.0;
  bool has_range = false;
  double range_min = 0.0, range_max = 0.0;
  bool export_3d = false, pings_current = true;
  wcd::Export3dOptions export3d;
  int c, option_index;
  while ((c = getopt_long(argc, argv, "VvHhI:i:F:f:P:p:S:", options, &option_index)) != -1) {
    switch (c) {
    case 'I':
    case 'i':
      input = optarg;
      break;
    case 'F':
    case 'f':
      format = atoi(optarg);
      break;
    case 'P':
    case 'p':
      ping = atoi(optarg);
      break;
    case 'S':
      swath = atoi(optarg);
      break;
    case 'b':
      scene.bottom = true;
      break;
    case 'w':
      scene.wc_bottom = true;
      break;
    case 's':
      scene.raytrace = false;
      break;
    case 'E':
      export_file = optarg;
      break;
    case 'C':
      export_curtain = optarg;
      break;
    case 'B':
      if (sscanf(optarg, "%d/%d", &curtain.first_beam, &curtain.last_beam) != 2 || curtain.first_beam < 0 ||
          curtain.last_beam < curtain.first_beam)
        errflg = true;
      break;
    case 'M':
      if (strcmp(optarg, "max") == 0)
        combine_max = true;
      else if (strcmp(optarg, "mean") != 0)
        errflg = true;
      break;
    case 'L':
      if (!wcd::palette_from_name(QString::fromLocal8Bit(optarg), palette))
        errflg = true;
      break;
    case 'U':
      if (strcmp(optarg, "below") == 0)
        layout = wcd::CurtainLayout::Below;
      else if (strcmp(optarg, "beside") == 0)
        layout = wcd::CurtainLayout::Beside;
      else if (strcmp(optarg, "off") == 0)
        layout = wcd::CurtainLayout::Off;
      else
        errflg = true;
      break;
    case 'X':
      if (sscanf(optarg, "%lf", &exaggeration) != 1 || !(exaggeration > 0.0))
        errflg = true;
      break;
    case 'Z':
      if (sscanf(optarg, "%dx%d", &width, &height) != 2 || width < 200 || height < 150)
        errflg = true;
      break;
    case 'R':
      if (sscanf(optarg, "%lf/%lf", &range_min, &range_max) != 2 || !(range_min < range_max))
        errflg = true;
      has_range = true;
      break;
    case 'T':
      export_3d = true;
      export3d.fan = strcmp(optarg, "fan") == 0 || strcmp(optarg, "both") == 0;
      export3d.curtain = strcmp(optarg, "curtain") == 0 || strcmp(optarg, "both") == 0;
      if (!export3d.fan && !export3d.curtain)
        errflg = true;
      break;
    case 'N':
      export_3d_name = optarg;
      break;
    case 'G':
      if (strcmp(optarg, "current") == 0) {
        pings_current = true;
      }
      else {
        const int n = sscanf(optarg, "%d/%d/%d", &export3d.first_ping, &export3d.last_ping, &export3d.ping_step);
        if (n == 2)
          export3d.ping_step = 1;
        if (n < 2 || export3d.first_ping < 0 || export3d.last_ping < export3d.first_ping || export3d.ping_step < 1)
          errflg = true;
        pings_current = false;
      }
      break;
    case 'Y': {
      export3d.vec = export3d.ply = export3d.las = false;
      const std::string list = optarg;
      size_t start = 0;
      while (start <= list.size()) {
        const size_t end = std::min(list.find(',', start), list.size());
        const std::string f = list.substr(start, end - start);
        if (f == "vec")
          export3d.vec = true;
        else if (f == "ply")
          export3d.ply = true;
        else if (f == "las")
          export3d.las = true;
        else
          errflg = true;
        start = end + 1;
      }
      break;
    }
    case 'Q': {
      char *end = nullptr;
      const long long n = strtoll(optarg, &end, 10);
      if (end == optarg || *end != '\0' || n < 0)
        errflg = true;
      else
        export3d.budget = (size_t)n;
      break;
    }
    case 'O':
      lasso_file = optarg;
      break;
    case 'D':
      dump = true;
      break;
    case 'V':
    case 'v':
      verbose++;
      break;
    case 'H':
    case 'h':
      help = true;
      break;
    default:
      errflg = true;
    }
  }
  if (input.empty() && optind < argc)
    input = argv[optind];

  if (errflg) {
    fprintf(stderr, "usage: %s\n", usage_message);
    fprintf(stderr, "\nProgram <%s> Terminated\n", program_name);
    return MB_ERROR_BAD_USAGE;
  }
  if (verbose == 1 || help) {
    fprintf(stderr, "\nProgram %s\n", program_name);
    fprintf(stderr, "MB-system Version %s\n", MB_VERSION);
  }
  if (help) {
    fprintf(stderr, "\n%s\n", help_message);
    fprintf(stderr, "\nusage: %s\n", usage_message);
    return MB_ERROR_NO_ERROR;
  }

  /* without a window, a datalist stands for its first file */
  if ((dump || !export_file.empty() || !export_curtain.empty() || export_3d) && !input.empty() &&
      wcd::is_datalist(input, format)) {
    std::vector<std::pair<std::string, int>> files;
    std::string message;
    if (!wcd::read_datalist(input, files, &message) || files.empty()) {
      fprintf(stderr, "%s: cannot read datalist %s: %s\n", program_name, input.c_str(), message.c_str());
      return MB_ERROR_OPEN_FAIL;
    }
    if (dump)
      printf("datalist %s: %zu files, the first shown\n", input.c_str(), files.size());
    input = files.front().first;
    format = files.front().second;
  }

  if (dump) {
    if (input.empty()) {
      fprintf(stderr, "usage: %s\n", usage_message);
      return MB_ERROR_BAD_USAGE;
    }
    return wcd::dump(input, format, ping, curtain);
  }

  /* write the fans and/or the curtain as 3D points without a window */
  if (export_3d) {
    wcd::Line line;
    std::string message;
    if (input.empty() || !line.load(input, wcd::Progress(), &message, format) || !line.has_wc) {
      fprintf(stderr, "%s: no water column data in %s %s\n", program_name, input.c_str(), message.c_str());
      return MB_ERROR_OPEN_FAIL;
    }
    if (pings_current)
      export3d.first_ping = export3d.last_ping = std::min(std::max(ping, 0), (int)line.wc.size() - 1);
    export3d.beams = curtain;
    export3d.beams.raytrace = scene.raytrace;
    export3d.combine_max = combine_max;
    export3d.color_min = has_range ? range_min : line.wc.amp_min;
    export3d.color_max = has_range ? range_max : line.wc.amp_max;
    if (has_range) {
      export3d.value_min = range_min;
      export3d.value_max = range_max;
    }
    for (int i = 0; i < 256; i++) {
      const QRgb rgb = wcd::palette_color(palette, i);
      export3d.colors[(size_t)i * 3] = (uint8_t)qRed(rgb);
      export3d.colors[(size_t)i * 3 + 1] = (uint8_t)qGreen(rgb);
      export3d.colors[(size_t)i * 3 + 2] = (uint8_t)qBlue(rgb);
    }
    if (!lasso_file.empty()) {
      if (!wcd::read_lasso(lasso_file, export3d.lasso, &message)) {
        fprintf(stderr, "%s: %s\n", program_name, message.c_str());
        return MB_ERROR_OPEN_FAIL;
      }
      export3d.lasso_only = true;
    }
    if (export_3d_name.empty()) {
      /* the input file name without directory and extension, in the current directory */
      std::string base = input.substr(input.find_last_of("/\\") + 1);
      const size_t dot = base.find_last_of('.');
      export_3d_name = dot == std::string::npos || dot == 0 ? base : base.substr(0, dot);
    }
    export3d.prefix = export_3d_name;
    wcd::Export3dResult result;
    if (!wcd::export_3d(line, export3d, result, nullptr, nullptr, wcd::Export3dProgress(), &message)) {
      fprintf(stderr, "%s: 3D export failed: %s\n", program_name, message.c_str());
      return MB_ERROR_WRITE_FAIL;
    }
    for (int object = 0; object < 2; object++) {
      const bool fan = object == 0;
      if ((fan && !export3d.fan) || (!fan && !export3d.curtain))
        continue;
      const wcd::Export3dCount &n = fan ? result.fan : result.curtain;
      if (n.written == 0)
        fprintf(stderr, "%s: %s: no points selected, nothing written\n", program_name, fan ? "fan" : "curtain");
      else if (verbose > 0)
        fprintf(stderr, "%s: %s: %zu points of %d pings (%.1f .. %.1f dB), %zu written (stride %zu)\n",
                program_name, fan ? "fan" : "curtain", n.points, n.pings, n.value_min, n.value_max, n.written,
                n.stride);
    }
    if (verbose > 0)
      for (const std::string &f : result.files) {
        const bool vec = f.size() > 4 && f.compare(f.size() - 4, 4, ".vec") == 0;
        fprintf(stderr, "%s: wrote %s%s%s\n", program_name, f.c_str(), vec ? "" : ", ", vec ? "" : result.crs.c_str());
      }
    if (export_file.empty() && export_curtain.empty())
      return MB_ERROR_NO_ERROR;
  }

  QApplication app(argc, argv);

  /* write the curtain to an image without a window */
  if (!export_curtain.empty()) {
    wcd::Line line;
    std::string message;
    if (input.empty() || !line.load(input, wcd::Progress(), &message, format) || !line.has_wc) {
      fprintf(stderr, "%s: no water column data in %s %s\n", program_name, input.c_str(), message.c_str());
      return MB_ERROR_OPEN_FAIL;
    }
    curtain.raytrace = scene.raytrace;
    auto made = std::make_shared<wcd::Curtain>();
    if (!wcd::build_curtain(line, curtain, *made, nullptr, &message)) {
      fprintf(stderr, "%s: no curtain: %s\n", program_name, message.c_str());
      return MB_ERROR_OPEN_FAIL;
    }
    wcd::CurtainPlot plot;
    plot.setCurtain(made);
    plot.setMaxMode(combine_max);
    plot.setPalette(palette);
    plot.setColorRange(has_range ? range_min : line.wc.amp_min, has_range ? range_max : line.wc.amp_max);
    plot.setDepthWindow(line.default_depth_min(), 1.0e5);
    const QSize size(width, height);
    plot.setExaggeration(exaggeration, size);
    plot.showStart();
    if (ping >= 0 && ping < made->ncols) {
      plot.setCurrent(ping);
      plot.setLabel(QString("ping %1").arg(line.wc.pings[ping].ping_number));
      plot.followCurrent(size);
    }
    const QString caption = QString::fromStdString(input.substr(input.find_last_of("/\\") + 1));
    if (!plot.render(size, 1.0, caption).save(QString::fromStdString(export_curtain))) {
      fprintf(stderr, "%s: cannot write %s\n", program_name, export_curtain.c_str());
      return MB_ERROR_WRITE_FAIL;
    }
    if (verbose > 0)
      fprintf(stderr, "%s: curtain of beams %d .. %d, %d pings x %d cells of %g m, %s, written to %s\n", program_name,
              made->first_beam, made->last_beam, made->ncols, made->nz, made->dz,
              made->traced ? "ray traced" : "straight rays", export_curtain.c_str());
    if (export_file.empty())
      return MB_ERROR_NO_ERROR;
  }

  /* write one swath to an image without a window */
  if (!export_file.empty()) {
    wcd::Line line;
    std::string message;
    if (input.empty() || !line.load(input, wcd::Progress(), &message, format) || !line.has_wc) {
      fprintf(stderr, "%s: no water column data in %s %s\n", program_name, input.c_str(), message.c_str());
      return MB_ERROR_OPEN_FAIL;
    }
    const size_t i = (size_t)std::min(std::max(ping, 0), (int)line.wc.size() - 1);
    const std::vector<int> swaths = line.wc.swaths(i);
    const int s = (swath >= 0 && std::find(swaths.begin(), swaths.end(), swath) != swaths.end()) ? swath
                                                                                                    : swaths.front();
    scene.raytrace = scene.raytrace && line.svp(line.wc.pings[i].time_d) != nullptr;
    wcd::PlotData data;
    wcd::SceneInfo info;
    wcd::build_scene(line, i, s, scene, data, info);
    wcd::WcdPlot plot;
    plot.setPalette(palette);
    plot.setBeamRange(curtain.first_beam, curtain.last_beam);
    plot.setColorRange(has_range ? range_min : line.wc.amp_min, has_range ? range_max : line.wc.amp_max);
    plot.setDepthWindow(line.default_depth_min(), 1.0e5);
    plot.setData(std::move(data));
    const QString caption = QString::fromStdString(input.substr(input.find_last_of("/\\") + 1)) + " | " +
                            wcd::metadata_text(line, i, s);
    if (!plot.render(QSize(width, height), 1.0, caption).save(QString::fromStdString(export_file))) {
      fprintf(stderr, "%s: cannot write %s\n", program_name, export_file.c_str());
      return MB_ERROR_WRITE_FAIL;
    }
    if (verbose > 0)
      fprintf(stderr, "%s: ping %zu swath %d %s%s written to %s\n", program_name, i, s,
              info.traced ? "ray traced" : "straight rays", info.status.isEmpty() ? "" : (", " +
              info.status.toStdString()).c_str(), export_file.c_str());
    return MB_ERROR_NO_ERROR;
  }

  wcd::WaterColumnWindow window;
  wcd::WaterColumnWidget *editor = window.editor();
  editor->setPalette(palette);
  editor->setCombineMax(combine_max);
  editor->setCurtainLayout(layout);
  window.show();
  if (!input.empty()) {
    editor->openFile(QString::fromStdString(input));
    editor->showPing(ping);
    if (curtain.first_beam != 0 || curtain.last_beam >= 0 || exaggeration != 1.0) {
      /* the beam range and scale once the file is in */
      QObject::connect(editor, &wcd::WaterColumnWidget::fileOpened, editor, [editor, curtain, exaggeration]() {
        editor->setBeamRange(curtain.first_beam, curtain.last_beam);
        editor->setCurtainExaggeration(exaggeration);
      }, Qt::SingleShotConnection);
    }
  }
  return app.exec();
}
