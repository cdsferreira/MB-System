/*
 * Stands in for the X11 Intrinsics header while compiling the unchanged
 * mbnavadjust_prog.c without X11. The basic types come from the mbview
 * header mbv_xtypes.h, as mbview-qt uses them; the rest are the X11 types
 * that mb_xgraphics.h names. It is on the include path of mbnavadjust-qt only.
 */
#ifndef MBNAQ_X11_INTRINSIC_H_
#define MBNAQ_X11_INTRINSIC_H_

#include "mbv_xtypes.h"

struct mbv_x_visualinfo {
  int unused;
};
typedef void *GC;
typedef struct {
  int unused;
} XFontStruct;
typedef struct {
  int unused;
} Visual;
typedef unsigned long Colormap;

#endif
