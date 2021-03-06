// DATEINAME        : hydEdit_math.h
// ERSTELLUNGSDATUM : 20.09.93
// COPYRIGHT (C) 1993: ATLAS ELEKTRONIK GMBH, 28305 BREMEN
//
// See README file for copying and redistribution conditions.

#ifndef SURF_PB_MATH_H_
#define SURF_PB_MATH_H_

#include <math.h>
#include <stdbool.h>

#ifndef PI
#define PI 3.14159265359
#endif

typedef struct {
  double x;
  double y;
} XY_Coords;

typedef struct {
  double angle;
  double cmean;
  double ckeel;
  double travelTime;
  double draught;
  double heaveTx;
  double heaveRx;
  double pitchTx;
  double transducerOffsetStar;
  double transducerOffsetAhead;
  double depth;
  double posStar;
  double posAhead;
} FanParam;

/* Plattkarten-Projektion */

#define M_PER_RAD_LAT      ((double)(60.0*180.0*1852.0/PI))
#define M_PER_RAD_LON(LAT) (((double)(60.0*180.0*1852.0/PI))*cos(LAT))

#define M_TO_RAD_Y(Y)      ((double)(Y/M_PER_RAD_LAT))
#define M_TO_RAD_X(X,LAT)  ((double)(X/(M_PER_RAD_LON(LAT))))
#define RAD_TO_METER_Y(LAT)      ((double)(LAT*M_PER_RAD_LAT))
#define RAD_TO_METER_X(LON,LAT)  ((double)(LON*(M_PER_RAD_LON(LAT))))

// double pbAtan2(double y, double x);

double setToPlusMinusPI(double angle);
bool depthFromTT(FanParam *fanParam, bool isPitchcompensated);

#endif  // SURF_PB_MATH_H_
