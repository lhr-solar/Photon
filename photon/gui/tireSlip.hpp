#pragma once

#include "../parse/arena.hpp"

struct TireSlipSample {
  float wheelSpeedMps{};
  float gpsSpeedMps{};
  float mcVehicleSpeedMps{};
  float motorRpm{};
  float slipVsGpsPercent{};
  float slipVsMcPercent{};
  bool hasWheel{};
  bool hasGps{};
  bool hasMcVehicle{};
  bool hasSlipGps{};
  bool hasSlipMc{};
};

// Rear tire longitudinal slip from motor RPM (21.56" wheel) vs GPS and MC_VehicleVelocity.
bool tireSlipAt(const Arena& arena, double cursor, TireSlipSample& sample);

// Batch helper for plots: fill slip series aligned to motor-velocity sample times in [t0, t1].
// Returns number of points written (capped by capacity).
size_t tireSlipSeries(const Arena& arena, double rangeStart, double rangeEnd, double* times,
                      double* slipGpsPercent, double* slipMcPercent, size_t capacity);
