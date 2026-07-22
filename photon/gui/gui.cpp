#include "gui.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <numbers>
#include <string>
#include <string_view>
#include <vector>

#if PHOTON_GUI_RENDER_ITEMS
#include "../gpu/shader.hpp"
#include "custom_shader_vert_spv.hpp"
#include "glowButton_frag_spv.hpp"
#include "lens_frag_spv.hpp"
#include "newCar_glb.hpp"
#include "new_dyn_glb.hpp"
#include "track_glb.hpp"
#endif
#include "DDash/dashboard_tab.h"
#include "arena.hpp"
#include "config.hpp"
#include "customView.hpp"
#if defined(APPLE) || defined(__APPLE__)
#include "gpuMetalGui.hpp"
#include "imgui_impl_sdl3.h"
#else
#include "gpuGui.hpp"
#endif
#include "im_anim.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "imnodes.h"
#include "implot.h"
#include "plots.hpp"
#include "uiComponents.hpp"
#include "widget.hpp"

namespace {
#if PHOTON_GUI_RENDER_ITEMS
struct FrameView {
  const Message* message{};
  const double* times{};
  uint32_t count{};
  uint32_t index{};

  double time(int offset = 0) const { return times[index + offset]; }
  double value(uint32_t signal, int offset = 0) const {
    return static_cast<const double*>(message->signals[signal]->data)[index + offset];
  }
};

bool frameAt(const Arena& arena, uint32_t id, double cursor, FrameView& frame) {
  if (id >= arena.messages.size() || !arena.messages[id]) return false;
  const Message& message = *arena.messages[id];
  const uint32_t count = message.signalSize.value.load(std::memory_order_acquire) / sizeof(double);
  if (!count) return false;
  const auto* times = static_cast<const double*>(message.timeData);
  const double* found = std::upper_bound(times, times + count, cursor);
  if (found == times) return false;
  frame = {&message, times, count, static_cast<uint32_t>(--found - times)};
  return true;
}

bool frameLatest(const Arena& arena, uint32_t id, FrameView& frame) {
  if (id >= arena.messages.size() || !arena.messages[id]) return false;
  const Message& message = *arena.messages[id];
  const uint32_t count = message.signalSize.value.load(std::memory_order_acquire) / sizeof(double);
  if (!count) return false;
  frame = {&message, static_cast<const double*>(message.timeData), count, count - 1};
  return true;
}

bool frameAtOrLatest(const Arena& arena, uint32_t id, double cursor, FrameView& frame) {
  return frameAt(arena, id, cursor, frame) || frameLatest(arena, id, frame);
}

bool dynamicsSignalAt(const Arena& arena, uint32_t id, uint32_t signal, double cursor,
                      float& value) {
  FrameView frame{};
  if (!frameAtOrLatest(arena, id, cursor, frame) || signal >= frame.message->signalCount ||
      cursor - frame.time() > 5.0)
    return false;
  const double sample = frame.value(signal);
  if (!std::isfinite(sample)) return false;
  value = static_cast<float>(sample);
  return true;
}

uint32_t dynamicsSignalIndex(const Message& message, std::string_view name) {
  for (uint32_t signal = 0; signal < message.signalCount; ++signal) {
    const Signal* candidate = message.signals[signal];
    if (candidate && candidate->name == name) return signal;
  }
  return SIGNAL_MAX;
}

bool dynamicsSignalNamedAt(const Arena& arena, uint32_t id, std::string_view signalName,
                           double cursor, float& value) {
  FrameView frame{};
  if (!frameAtOrLatest(arena, id, cursor, frame) || cursor - frame.time() > 5.0) return false;
  const uint32_t signal = dynamicsSignalIndex(*frame.message, signalName);
  if (signal == SIGNAL_MAX) return false;
  const double sample = frame.value(signal);
  if (!std::isfinite(sample)) return false;
  value = static_cast<float>(sample);
  return true;
}

bool dynamicsSignalAtEither(const Arena& arena, uint32_t preferredId, uint32_t legacyId,
                            uint32_t signal, double cursor, float& value) {
  return dynamicsSignalAt(arena, preferredId, signal, cursor, value) ||
         dynamicsSignalAt(arena, legacyId, signal, cursor, value);
}

bool dynamicsSignalNamedAtEither(const Arena& arena, uint32_t preferredId, uint32_t legacyId,
                                 std::string_view signalName, double cursor, float& value) {
  return dynamicsSignalNamedAt(arena, preferredId, signalName, cursor, value) ||
         dynamicsSignalNamedAt(arena, legacyId, signalName, cursor, value);
}

bool dynamicsVectorAt(const Arena& arena, uint32_t id, double cursor, std::array<float, 3>& value,
                      std::string_view xName, std::string_view yName, std::string_view zName) {
  FrameView frame{};
  if (!frameAtOrLatest(arena, id, cursor, frame) || cursor - frame.time() > 5.0) return false;
  const uint32_t x = dynamicsSignalIndex(*frame.message, xName);
  const uint32_t y = dynamicsSignalIndex(*frame.message, yName);
  const uint32_t z = dynamicsSignalIndex(*frame.message, zName);
  if (x == SIGNAL_MAX || y == SIGNAL_MAX || z == SIGNAL_MAX) return false;
  const double samples[3] = {frame.value(x), frame.value(y), frame.value(z)};
  for (uint32_t axis = 0; axis < 3; ++axis) {
    if (!std::isfinite(samples[axis])) return false;
    value[axis] = static_cast<float>(samples[axis]);
  }
  return true;
}

bool dynamicsSuspensionAt(const Arena& arena, uint32_t preferredId, uint32_t legacyId,
                          double cursor, float& value) {
  FrameView frame{};
  const bool found = frameAtOrLatest(arena, preferredId, cursor, frame) ||
                     (legacyId != 0 && frameAtOrLatest(arena, legacyId, cursor, frame));
  if (!found || cursor - frame.time() > 5.0) return false;

  double sum = 0.0;
  uint32_t count = 0;
  for (uint32_t signal = 0; signal < frame.message->signalCount; ++signal) {
    const Signal* candidate = frame.message->signals[signal];
    if (!candidate || candidate->name.rfind("SUS_Data", 0) != 0) continue;
    const double sample = frame.value(signal);
    if (!std::isfinite(sample)) return false;
    sum += sample;
    ++count;
  }
  if (!count) return false;
  value = static_cast<float>(sum / static_cast<double>(count));
  return true;
}

void updateDynamicsPose(const Arena& arena, double cursor, GUI::DynamicsTelemetry& telemetry,
                        SceneObject& object) {
  float steering = telemetry.steeringDegrees;
  const bool hasSteering = dynamicsSignalNamedAt(arena, 688, "LWS_Angle", cursor, steering);
  telemetry.steeringDegrees = steering;

  float throttle = telemetry.throttlePercent;
  float brake = telemetry.brakePercent;
  const bool hasThrottle =
      dynamicsSignalNamedAt(arena, 80, "AccelPedal_Main_Pos", cursor, throttle);
  const bool hasBrake = dynamicsSignalNamedAt(arena, 80, "BrakePedal_Main_Pos", cursor, brake);
  telemetry.throttlePercent = throttle;
  telemetry.brakePercent = brake;

  float frontRightRpm = telemetry.frontRightRpm;
  float frontLeftRpm = telemetry.frontLeftRpm;
  float rearRpm = telemetry.rearRpm;
  const bool hasFrontRightRpm =
      dynamicsSignalNamedAtEither(arena, 5280, 1184, "RPM", cursor, frontRightRpm);
  const bool hasFrontLeftRpm =
      dynamicsSignalNamedAtEither(arena, 5328, 1232, "RPM", cursor, frontLeftRpm);
  const bool hasRearRpm =
      dynamicsSignalNamedAt(arena, 1059, "MC_MotorVelocity", cursor, rearRpm);
  // Wheel-speed boards occasionally emit saturated garbage (e.g. 0x8Cxx). Treat anything
  // above a physical solar-car wheel limit as invalid rather than animating wild RPM.
  constexpr float kMaxWheelRpm = 1200.0f;
  const bool frontRightRpmValid = hasFrontRightRpm && frontRightRpm >= 0.0f && frontRightRpm <= kMaxWheelRpm;
  const bool frontLeftRpmValid = hasFrontLeftRpm && frontLeftRpm >= 0.0f && frontLeftRpm <= kMaxWheelRpm;
  if (!frontRightRpmValid) frontRightRpm = 0.0f;
  if (!frontLeftRpmValid) frontLeftRpm = 0.0f;
  if (!hasRearRpm) {
    if (frontLeftRpmValid && frontRightRpmValid)
      rearRpm = (frontLeftRpm + frontRightRpm) * 0.5f;
    else if (frontLeftRpmValid)
      rearRpm = frontLeftRpm;
    else if (frontRightRpmValid)
      rearRpm = frontRightRpm;
  }
  telemetry.frontLeftRpm = frontLeftRpm;
  telemetry.frontRightRpm = frontRightRpm;
  telemetry.rearRpm = rearRpm;

  std::array<bool, 3> hasSuspension{};
  hasSuspension[0] = dynamicsSuspensionAt(arena, 4816, 720, cursor, telemetry.suspensionRaw[0]);
  hasSuspension[1] = dynamicsSuspensionAt(arena, 4768, 672, cursor, telemetry.suspensionRaw[1]);
  hasSuspension[2] = dynamicsSuspensionAt(arena, 4784, 0, cursor, telemetry.suspensionRaw[2]);
  constexpr std::array<uint32_t, 3> accelerationIds{4560, 4512, 4528};
  constexpr std::array<uint32_t, 3> angularVelocityIds{4561, 4513, 4529};
  for (size_t i = 0; i < telemetry.acceleration.size(); ++i) {
    telemetry.hasAcceleration[i] = dynamicsVectorAt(arena, accelerationIds[i], cursor,
                                                    telemetry.acceleration[i], "Accel_X", "Accel_Y",
                                                    "Accel_Z");
    telemetry.hasAngularVelocity[i] =
        dynamicsVectorAt(arena, angularVelocityIds[i], cursor, telemetry.angularVelocity[i],
                         "Gyro_X", "Gyro_Y", "Gyro_Z");
  }

  const bool timelineReset = telemetry.lastCursor < 0.0 || cursor < telemetry.lastCursor ||
                             cursor - telemetry.lastCursor > 0.5;
  std::array<float, 3> suspension{};
  for (size_t i = 0; i < suspension.size(); ++i) {
    if (timelineReset) {
      telemetry.suspensionReferenceValid[i] = false;
      telemetry.accelerationReferenceValid[i] = false;
    }
    if (hasSuspension[i] && !telemetry.suspensionReferenceValid[i]) {
      telemetry.suspensionReference[i] = telemetry.suspensionRaw[i];
      telemetry.suspensionReferenceValid[i] = true;
    }
    if (hasSuspension[i] && telemetry.suspensionReferenceValid[i])
      suspension[i] = std::clamp(
          (telemetry.suspensionRaw[i] - telemetry.suspensionReference[i]) / 4095.0f * 0.24f, -0.12f,
          0.12f);
    if (telemetry.hasAcceleration[i] && !telemetry.accelerationReferenceValid[i]) {
      telemetry.accelerationReference[i] = telemetry.acceleration[i];
      telemetry.accelerationReferenceValid[i] = true;
    }
  }

  std::array<std::array<float, 3>, 3> accelerationDelta{};
  std::array<float, 3> averageAcceleration{};
  std::array<float, 3> averageAngularVelocity{};
  uint32_t accelerationCount = 0;
  uint32_t angularVelocityCount = 0;
  for (size_t corner = 0; corner < telemetry.acceleration.size(); ++corner) {
    if (telemetry.hasAcceleration[corner] && telemetry.accelerationReferenceValid[corner]) {
      for (size_t axis = 0; axis < 3; ++axis) {
        accelerationDelta[corner][axis] =
            telemetry.acceleration[corner][axis] - telemetry.accelerationReference[corner][axis];
        averageAcceleration[axis] += accelerationDelta[corner][axis];
      }
      ++accelerationCount;
    }
    if (telemetry.hasAngularVelocity[corner]) {
      for (size_t axis = 0; axis < 3; ++axis)
        averageAngularVelocity[axis] += telemetry.angularVelocity[corner][axis];
      ++angularVelocityCount;
    }
  }
  if (accelerationCount > 0) {
    for (float& value : averageAcceleration) value /= static_cast<float>(accelerationCount);
  }
  if (angularVelocityCount > 0)
    for (float& value : averageAngularVelocity) value /= static_cast<float>(angularVelocityCount);

  const float dt = timelineReset
                       ? 0.0f
                       : static_cast<float>(std::clamp(cursor - telemetry.lastCursor, 0.0, 0.1));
  telemetry.lastCursor = cursor;
  SceneDynamicsPose& pose = object.dynamics;
  const float animatedFrontLeftRpm = frontLeftRpmValid ? frontLeftRpm : 0.0f;
  const float animatedFrontRightRpm = frontRightRpmValid ? frontRightRpm : 0.0f;
  const float animatedRearRpm =
      hasRearRpm || frontLeftRpmValid || frontRightRpmValid ? rearRpm : 0.0f;
  pose.frontLeftWheelDegrees =
      std::remainder(pose.frontLeftWheelDegrees + animatedFrontLeftRpm * 6.0f * dt, 360.0f);
  pose.frontRightWheelDegrees =
      std::remainder(pose.frontRightWheelDegrees + animatedFrontRightRpm * 6.0f * dt, 360.0f);
  pose.rearWheelDegrees =
      std::remainder(pose.rearWheelDegrees + animatedRearRpm * 6.0f * dt, 360.0f);

  const float response = 1.0f - std::exp(-10.0f * ImGui::GetIO().DeltaTime);
  const float roadWheelSteering = std::clamp(steering / 12.0f, -35.0f, 35.0f);
  pose.steeringDegrees = std::lerp(pose.steeringDegrees, roadWheelSteering, response);
  pose.steeringWheelDegrees =
      std::lerp(pose.steeringWheelDegrees, std::clamp(steering, -540.0f, 540.0f), response);
  pose.frontLeftSuspension = std::lerp(pose.frontLeftSuspension, suspension[0], response);
  pose.frontRightSuspension = std::lerp(pose.frontRightSuspension, suspension[1], response);
  pose.rearSuspension = std::lerp(pose.rearSuspension, suspension[2], response);
  const float accelerationRoll = accelerationCount > 0 ? -averageAcceleration[1] * 1.5f : 0.0f;
  const float accelerationPitch = accelerationCount > 0 ? averageAcceleration[0] * 1.2f : 0.0f;
  const float gyroRollLead = angularVelocityCount > 0 ? averageAngularVelocity[0] * 0.01f : 0.0f;
  const float gyroPitchLead = angularVelocityCount > 0 ? averageAngularVelocity[1] * 0.01f : 0.0f;
  const float roll = std::clamp(
      (suspension[1] - suspension[0]) * 12.0f + accelerationRoll + gyroRollLead, -4.0f, 4.0f);
  const float pitch = std::clamp((suspension[2] - (suspension[0] + suspension[1]) * 0.5f) * 8.0f +
                                     accelerationPitch + gyroPitchLead,
                                 -3.0f, 3.0f);
  pose.rollDegrees = std::lerp(pose.rollDegrees, roll, response);
  pose.pitchDegrees = std::lerp(pose.pitchDegrees, pitch, response);

  telemetry.hasSteering = hasSteering;
  telemetry.hasDriverInput = hasSteering || hasThrottle || hasBrake;
  telemetry.hasWheelSpeed = frontLeftRpmValid || frontRightRpmValid || hasRearRpm;
  telemetry.hasSuspension = hasSuspension[0] || hasSuspension[1] || hasSuspension[2];
  telemetry.hasImu = accelerationCount > 0 || angularVelocityCount > 0;
  telemetry.hasTireSlip = tireSlipAt(arena, cursor, telemetry.tireSlip);
}

void applyDynamicsJiggle(GUI::DynamicsTelemetry& telemetry, SceneObject& object) {
  if (!telemetry.jiggle) return;

  telemetry.jiggleTime = std::fmod(telemetry.jiggleTime + ImGui::GetIO().DeltaTime, 60.0f);
  const float phase = telemetry.jiggleTime * (2.0f * std::numbers::pi_v<float> / 5.0f);
  SceneDynamicsPose& pose = object.dynamics;
  pose.steeringDegrees = std::sin(phase) * 35.0f;
  pose.steeringWheelDegrees = std::sin(phase) * 540.0f;
  pose.frontLeftWheelDegrees = std::remainder(telemetry.jiggleTime * 360.0f, 360.0f);
  pose.frontRightWheelDegrees = std::remainder(telemetry.jiggleTime * 420.0f, 360.0f);
  pose.rearWheelDegrees = std::remainder(telemetry.jiggleTime * 480.0f, 360.0f);
  pose.frontLeftSuspension = std::sin(phase * 1.31f) * 0.12f;
  pose.frontRightSuspension = std::sin(phase * 1.31f + 2.1f) * 0.12f;
  pose.rearSuspension = std::sin(phase * 1.31f + 4.2f) * 0.12f;
  pose.rollDegrees = std::sin(phase * 0.73f) * 4.0f;
  pose.pitchDegrees = std::sin(phase * 1.07f + 1.2f) * 3.0f;
}

Position gpsPosition(double latitude, double longitude) {
  // Brainerd OSM centerline fitted to the Assetto Corsa track.glb coordinate system.
  constexpr double latitudeOrigin = 46.41613092075472;
  constexpr double longitudeOrigin = -94.27538689056604;
  constexpr double metersPerLatitudeDegree = 111320.0;
  constexpr double metersPerLongitudeDegree = 76745.74845956931;
  constexpr double eastX = 0.09952096955160591;
  constexpr double northX = 0.00019804275620237066;
  constexpr double eastY = -0.00019804275620237066;
  constexpr double northY = 0.09952096955160591;
  const double east = (longitude - longitudeOrigin) * metersPerLongitudeDegree;
  const double north = (latitude - latitudeOrigin) * metersPerLatitudeDegree;
  return {static_cast<float>(eastX * east + northX * north - 3.75936219),
          static_cast<float>(eastY * east + northY * north + 4.97846090), 0.3f};
}

struct GpsSpan {
  Position previous{};
  Position current{};
  Position next{};
  double previousTime{};
  double time{};
  double nextTime{};
  bool hasPrevious{};
  bool hasNext{};
};

bool normalizeGpsCoordinate(double rawValue, bool latitude, double& coordinate) {
  if (!std::isfinite(rawValue)) return false;

  const double limit = latitude ? 90.0 : 180.0;
  const double trackCoordinate = latitude ? 46.41613092075472 : -94.27538689056604;
  bool found = false;
  double bestDistance = std::numeric_limits<double>::max();
  const auto consider = [&](double candidate) {
    if (!std::isfinite(candidate) || std::abs(candidate) > limit) return;
    const double candidateDistance = std::abs(candidate - trackCoordinate);
    if (!found || candidateDistance < bestDistance) {
      coordinate = candidate;
      bestDistance = candidateDistance;
      found = true;
    }
  };

  // The direct-car DBC publishes signed degrees scaled by 1e-6, which the
  // decoder has already converted to degrees by the time it reaches here.
  consider(rawValue);

  // The AWS telemetry DBC describes the same four bytes as an unsigned
  // integer. Depending on the producer, those bytes contain either an IEEE
  // float or signed microdegrees. Evaluate both representations and select
  // the coordinate that is plausible for the configured Brainerd track.
  if (rawValue >= 0.0 && rawValue <= std::numeric_limits<uint32_t>::max() &&
      std::floor(rawValue) == rawValue) {
    const uint32_t bits = static_cast<uint32_t>(rawValue);
    consider(static_cast<double>(std::bit_cast<float>(bits)));
    consider(static_cast<double>(std::bit_cast<int32_t>(bits)) * 1e-6);
  }
  return found;
}

bool gpsPoint(const FrameView& latitude, const FrameView& longitude, int offset, Position& position,
              double& time) {
  const int latitudeIndex = static_cast<int>(latitude.index) + offset;
  const int longitudeIndex = static_cast<int>(longitude.index) + offset;
  if (latitudeIndex < 0 || longitudeIndex < 0 ||
      latitudeIndex >= static_cast<int>(latitude.count) ||
      longitudeIndex >= static_cast<int>(longitude.count) ||
      std::abs(latitude.times[latitudeIndex] - longitude.times[longitudeIndex]) > 1.0)
    return false;
  double lat = 0.0;
  double lon = 0.0;
  if (!normalizeGpsCoordinate(latitude.value(0, offset), true, lat) ||
      !normalizeGpsCoordinate(longitude.value(0, offset), false, lon) || (lat == 0.0 && lon == 0.0))
    return false;
  position = gpsPosition(lat, lon);
  time = std::max(latitude.times[latitudeIndex], longitude.times[longitudeIndex]);
  return true;
}

bool gpsSpanAt(const Arena& arena, double cursor, GpsSpan& gps) {
  struct GpsMessagePair {
    uint32_t latitude;
    uint32_t longitude;
  };
  constexpr GpsMessagePair pairs[]{{4288, 4289}, {5888, 5889}};

  bool found = false;
  for (const GpsMessagePair pair : pairs) {
    FrameView latitude{}, longitude{};
    GpsSpan candidate{};
    if (!frameAtOrLatest(arena, pair.latitude, cursor, latitude) ||
        !frameAtOrLatest(arena, pair.longitude, cursor, longitude) ||
        !gpsPoint(latitude, longitude, 0, candidate.current, candidate.time))
      continue;
    candidate.hasPrevious =
        gpsPoint(latitude, longitude, -1, candidate.previous, candidate.previousTime);
    candidate.hasNext = gpsPoint(latitude, longitude, 1, candidate.next, candidate.nextTime);
    if (!found || candidate.time > gps.time) {
      gps = candidate;
      found = true;
    }
  }
  return found;
}

uint32_t messageSampleCount(const Arena& arena, uint32_t id) {
  if (id >= arena.messages.size() || !arena.messages[id]) return 0;
  return arena.messages[id]->signalSize.value.load(std::memory_order_acquire) / sizeof(double);
}

struct GpsDebugInfo {
  bool locked{};
  uint32_t latitudeId{};
  uint32_t longitudeId{};
  uint32_t latitudeSamples{};
  uint32_t longitudeSamples{};
  double latitude{};
  double longitude{};
  double ageSeconds{};
};

GpsDebugInfo gpsDebugAt(const Arena& arena, double cursor) {
  GpsDebugInfo info{};
  struct GpsMessagePair {
    uint32_t latitude;
    uint32_t longitude;
  };
  constexpr GpsMessagePair pairs[]{{4288, 4289}, {5888, 5889}};
  for (const GpsMessagePair pair : pairs) {
    info.latitudeSamples = messageSampleCount(arena, pair.latitude);
    info.longitudeSamples = messageSampleCount(arena, pair.longitude);
    info.latitudeId = pair.latitude;
    info.longitudeId = pair.longitude;
    FrameView latitude{}, longitude{};
    if (!frameAtOrLatest(arena, pair.latitude, cursor, latitude) ||
        !frameAtOrLatest(arena, pair.longitude, cursor, longitude))
      continue;
    double lat = 0.0;
    double lon = 0.0;
    if (!normalizeGpsCoordinate(latitude.value(0), true, lat) ||
        !normalizeGpsCoordinate(longitude.value(0), false, lon))
      continue;
    info.locked = true;
    info.latitude = lat;
    info.longitude = lon;
    info.ageSeconds = cursor - std::max(latitude.time(), longitude.time());
    return info;
  }
  return info;
}

float distance(Position a, Position b) { return std::hypot(b.x - a.x, b.y - a.y); }
float direction(Position a, Position b) { return std::atan2(b.y - a.y, b.x - a.x); }

bool fusedMapPosition(const Arena& arena, double cursor, GUI::MapTracker& tracker,
                      Position& position, float& heading) {
  GpsSpan gps{};
  if (!gpsSpanAt(arena, cursor, gps)) return false;

  if (gps.hasNext && gps.nextTime > gps.time) {
    const float amount =
        static_cast<float>(std::clamp((cursor - gps.time) / (gps.nextTime - gps.time), 0.0, 1.0));
    position = {std::lerp(gps.current.x, gps.next.x, amount),
                std::lerp(gps.current.y, gps.next.y, amount), 0.3f};
    const float segmentDistance = distance(gps.current, gps.next);
    heading = segmentDistance > 0.05f ? direction(gps.current, gps.next) : tracker.heading;
    tracker = {position, cursor,
               gps.time, segmentDistance / static_cast<float>(gps.nextTime - gps.time),
               heading,  true};
    return true;
  }

  const bool reset = !tracker.valid || cursor < tracker.time || cursor - tracker.time > 0.5 ||
                     gps.time > tracker.gpsTime;
  if (reset) {
    tracker.position = gps.current;
    tracker.time = gps.time;
    tracker.gpsTime = gps.time;
    if (gps.hasPrevious && gps.time > gps.previousTime) {
      const float segmentDistance = distance(gps.previous, gps.current);
      if (segmentDistance > 0.05f) tracker.heading = direction(gps.previous, gps.current);
      tracker.speed = segmentDistance / static_cast<float>(gps.time - gps.previousTime);
    }
    tracker.valid = true;
  }

  const float dt = static_cast<float>(std::clamp(cursor - tracker.time, 0.0, 30.0));
  FrameView velocityFrame{};
  const bool hasVelocityFrame = frameAt(arena, 1059, cursor, velocityFrame);
  const uint32_t vehicleVelocitySignal =
      hasVelocityFrame ? dynamicsSignalIndex(*velocityFrame.message, "MC_VehicleVelocity")
                       : SIGNAL_MAX;
  const bool hasMapVelocity = hasVelocityFrame && vehicleVelocitySignal != SIGNAL_MAX &&
                              cursor - velocityFrame.time() < 1.0 &&
                              std::isfinite(velocityFrame.value(vehicleVelocitySignal));
  const float vehicleVelocity =
      hasMapVelocity ? static_cast<float>(velocityFrame.value(vehicleVelocitySignal)) : 0.0f;
  constexpr std::array<uint32_t, 3> accelerationIds{4560, 4512, 4528};
  constexpr std::array<uint32_t, 3> angularVelocityIds{4561, 4513, 4529};
  float yawRate = 0.0f;
  uint32_t yawRateCount = 0;
  for (size_t corner = 0; corner < accelerationIds.size(); ++corner) {
    FrameView acceleration{}, gyro{};
    if (!frameAt(arena, accelerationIds[corner], cursor, acceleration) ||
        !frameAt(arena, angularVelocityIds[corner], cursor, gyro))
      continue;
    const uint32_t accelX = dynamicsSignalIndex(*acceleration.message, "Accel_X");
    const uint32_t accelY = dynamicsSignalIndex(*acceleration.message, "Accel_Y");
    const uint32_t accelZ = dynamicsSignalIndex(*acceleration.message, "Accel_Z");
    const uint32_t gyroX = dynamicsSignalIndex(*gyro.message, "Gyro_X");
    const uint32_t gyroY = dynamicsSignalIndex(*gyro.message, "Gyro_Y");
    const uint32_t gyroZ = dynamicsSignalIndex(*gyro.message, "Gyro_Z");
    if (accelX == SIGNAL_MAX || accelY == SIGNAL_MAX || accelZ == SIGNAL_MAX ||
        gyroX == SIGNAL_MAX || gyroY == SIGNAL_MAX || gyroZ == SIGNAL_MAX)
      continue;
    if (cursor - acceleration.time() >= 0.25 || cursor - gyro.time() >= 0.25 ||
        std::abs(acceleration.time() - gyro.time()) >= 0.1)
      continue;
    // Project angular velocity onto measured gravity, independent of each IMU's mounting axes.
    const float ax = static_cast<float>(acceleration.value(accelX));
    const float ay = static_cast<float>(acceleration.value(accelY));
    const float az = static_cast<float>(acceleration.value(accelZ));
    const float gravity = std::sqrt(ax * ax + ay * ay + az * az);
    if (gravity <= 0.7f || gravity >= 1.3f) continue;
    yawRate += static_cast<float>((gyro.value(gyroX) * ax + gyro.value(gyroY) * ay +
                                   gyro.value(gyroZ) * az) /
                                  gravity) *
               std::numbers::pi_v<float> / 180.0f;
    ++yawRateCount;
  }
  if (yawRateCount > 0) yawRate /= static_cast<float>(yawRateCount);

  if (dt > 0.0f && (hasMapVelocity || yawRateCount > 0)) {
    const float nextHeading = tracker.heading + std::clamp(yawRate, -4.0f, 4.0f) * dt;
    float nextSpeed = tracker.speed;
    if (hasMapVelocity) {
      constexpr float metersToScene = 0.099521f;
      const float measured = std::clamp(std::abs(vehicleVelocity), 0.0f, 100.0f) * metersToScene;
      nextSpeed = std::lerp(nextSpeed, measured, 1.0f - std::exp(-8.0f * dt));
    }
    const float travel = (tracker.speed + nextSpeed) * 0.5f * dt;
    const float middleHeading = (tracker.heading + nextHeading) * 0.5f;
    tracker.position.x += std::cos(middleHeading) * travel;
    tracker.position.y += std::sin(middleHeading) * travel;
    tracker.position.z = 0.3f;
    tracker.heading = nextHeading;
    tracker.speed = nextSpeed;
  }
  tracker.time = cursor;
  position = tracker.position;
  heading = tracker.heading;
  return true;
}
#endif

void drawFpsOverlay() {
  const ImGuiViewport* viewport = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(
      {viewport->WorkPos.x + viewport->WorkSize.x - 10, viewport->WorkPos.y + 10}, ImGuiCond_Always,
      {1, 0});
  constexpr ImGuiWindowFlags flags =
      ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
      ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
      ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoInputs |
      ImGuiWindowFlags_NoBackground;
  if (ImGui::Begin("FPS Overlay", nullptr, flags))
    ImGui::Text("%.0f FPS", ImGui::GetIO().Framerate);
  ImGui::End();
}
}  // namespace

void GUI::init(GPU& gpu, Arena& arena, Network& network) {
  this->gpu = &gpu;
  this->arena = &arena;
  this->network = &network;
  GuiSettings::regster(&settings);
  settings.setStyle();
  setTabs();
  updater.queryReleaseInfoOnceAsync();
#if PHOTON_GUI_RENDER_ITEMS
  testShader.dispatchInit(gpu, (uint32_t*)custom_shader_vert_spv, custom_shader_vert_spv_size,
                          (uint32_t*)lens_frag_spv, lens_frag_spv_size);
  buttonShader.dispatchInit(gpu, (uint32_t*)custom_shader_vert_spv, custom_shader_vert_spv_size,
                            (uint32_t*)glowButton_frag_spv, glowButton_frag_spv_size);

  scene.addModel("track_glb", track_glb, track_glb_size, false);
  scene.addModel("newCar_glb", newCar_glb, newCar_glb_size, true);
  scene.dispatchInit(gpu);

  dynamicsObjectIndex = static_cast<int>(dynamicsScene.objects.size());
  dynamicsScene.addModel("new_dyn_glb", new_dyn_glb, new_dyn_glb_size, false);
  dynamicsScene.objects[dynamicsObjectIndex].dynamicsModel = true;
  dynamicsScene.dispatchInit(gpu);
#endif
}

void GUI::render() {
#if PHOTON_GUI_RENDER_ITEMS
  if (!testShader.initialized.load() && testShader.partInitialized.load())
    testShader.finishInit(*gpu);
  if (testShader.showing) {
    testShader.render(*gpu, gpu->commandBuffers[gpu->frameIndex]);
    testShader.showing = false;
  }

  if (!buttonShader.initialized.load() && buttonShader.partInitialized.load())
    buttonShader.finishInit(*gpu);
  if (buttonShader.showing) {
    buttonShader.render(*gpu, gpu->commandBuffers[gpu->frameIndex]);
    buttonShader.showing = false;
  }

  if (!scene.initialized.load() && scene.partInitialized.load()) scene.finishInit(*gpu);
  if (scene.showing) {
    scene.render(*gpu, gpu->commandBuffers[gpu->frameIndex]);
    scene.showing = false;
  }

  if (!dynamicsScene.initialized.load() && dynamicsScene.partInitialized.load())
    dynamicsScene.finishInit(*gpu);
  if (dynamicsScene.showing) {
    dynamicsScene.render(*gpu, gpu->commandBuffers[gpu->frameIndex]);
    dynamicsScene.showing = false;
  }
#endif
};

void GUI::destroy() {
  videoUi.stop();
  if (videoUi.videoTexture.Status != ImTextureStatus_Destroyed) {
    ImGui::UnregisterUserTexture(&videoUi.videoTexture);
    videoUi.videoTexture.SetStatus(ImTextureStatus_WantDestroy);
  }
  if (sideBar.backgroundTexture) {
    ImGui::UnregisterUserTexture(sideBar.backgroundTexture);
    sideBar.backgroundTexture->SetStatus(ImTextureStatus_WantDestroy);
    sideBar.backgroundTexture = nullptr;
  }
#if PHOTON_GUI_RENDER_ITEMS
  testShader.destroy();
  buttonShader.destroy();
  scene.destroy();
  dynamicsScene.destroy();
#endif
};

void GUI::setFont() {
  bool incFlag = false;
  bool decFlag = false;
  auto incSize = [&]() -> void {
    settings.fontSize += 1.0f;
    ImGui::GetStyle().FontSizeBase = settings.fontSize;
    ImGui::MarkIniSettingsDirty();
  };
  auto decSize = [&]() -> void {
    settings.fontSize = settings.fontSize > 1.0f ? settings.fontSize - 1.0f : 1.0f;
    ImGui::GetStyle().FontSizeBase = settings.fontSize;
    ImGui::MarkIniSettingsDirty();
  };
  ifKey(ImGuiKey_Equal, incFlag, incSize);
  ifKey(ImGuiKey_Minus, decFlag, decSize);
};

void GUI::settingsUI() {
  const bool open = PhotonUi::beginModal("Settings", {440.0f, 230.0f});
  if (open) {
    const PhotonUi::Palette palette = PhotonUi::palette();
    PhotonUi::label("Settings", palette);
    if (PhotonUi::modalCloseButton("CloseSettings", palette)) ImGui::CloseCurrentPopup();
  }
  PhotonUi::endModal(open);
};

void GUI::updateUI() { updater.drawUI(updateAvailable); };

void GUI::exportUI() {
  const bool open = PhotonUi::beginModal("Export", {420.0f, 220.0f});
  if (open) {
    const PhotonUi::Palette palette = PhotonUi::palette();
    PhotonUi::label("Export", palette);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 18.0f);
    const float actionWidth = ImGui::GetContentRegionAvail().x;
    const bool exporting = exporter.running.load();
    if (PhotonUi::rowButton("ExportCsv", "\uede9",
                            exporting ? "Exporting output.csv" : "Export output.csv",
                            {actionWidth, 42.0f}, palette, false, exporting)) {
      std::thread t(&Exporter::toFile, &exporter, std::ref(*arena), "output.csv");
      t.detach();
    }
    if (PhotonUi::modalCloseButton("CloseExport", palette)) ImGui::CloseCurrentPopup();
  }
  PhotonUi::endModal(open);
};

void GUI::plotTest(ImGuiWindowFlags flags) {
  flags &= ~(ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
  flags |= ImGuiWindowFlags_AlwaysVerticalScrollbar;
  if (ImGui::Begin("Page 1", NULL, flags)) {
    ArenaReadScope read(*arena);
    auto dim = ImGui::GetContentRegionAvail();
    dim.y = 0;
    ImPlotSpec spec = settings.plotLineSpec;
    for (const uint32_t id : arena->validIds) {
      if (id >= arena->messages.size() || !arena->messages[id]) continue;
      for (uint32_t signal = 0; signal < arena->messages[id]->signalCount; signal++)
        plots.signal(*arena, id, signal, dim, spec);
    }
  }
  ImGui::End();
};

void GUI::drawFrontCamEmbedded(ImVec2 size) { videoUi.drawContent(size); }

void GUI::drawLiveSceneEmbedded(ImVec2 size) {
#if PHOTON_GUI_RENDER_ITEMS
  scene.showing = false;
  const bool ready =
      scene.initialized.load() && !scene.frames.empty() && scene.frameIndex != nullptr;
  SceneFrame fallbackFrame{};
  SceneFrame& frame = ready ? scene.frames[*scene.frameIndex] : fallbackFrame;
  const PhotonUi::Palette palette = PhotonUi::palette();
  const ImVec2 drawSize{std::max(size.x, 1.0f), std::max(size.y, 1.0f)};

  if (ready) {
    const VkExtent2D nextExtent = quantizeContentExtent(drawSize, frame.extent);
    if (nextExtent.width != frame.extent.width || nextExtent.height != frame.extent.height) {
      frame.extent = nextExtent;
      scene.dirty = true;
    }
  }

  if (ready) {
    const bool sceneVisible = ImGui::IsRectVisible(drawSize);
    if (sceneVisible) scene.showing = true;
    ImGui::Image(frame.texture, drawSize);
    const bool sceneHovered = ImGui::IsItemHovered();
    const ImVec2 imageMin = ImGui::GetItemRectMin();
    const ImVec2 imageMax = ImGui::GetItemRectMax();
    const bool hasTrackedObject =
        scene.trackedObjectIndex >= 0 &&
        scene.trackedObjectIndex < static_cast<int>(scene.objects.size());
    if (hasTrackedObject && arena) {
      Position nextPosition{};
      float nextHeading{};
      const double mapCursor = plots.mapCursor();
      const bool smooth = mapTracker.valid && mapCursor >= mapTracker.time &&
                          mapCursor - mapTracker.time < 0.5;
      ArenaReadScope read(*arena);
      if (fusedMapPosition(*arena, mapCursor, mapTracker, nextPosition, nextHeading)) {
        SceneObject& car = scene.objects[scene.trackedObjectIndex];
        const float amount = smooth ? 1.0f - std::exp(-10.0f * ImGui::GetIO().DeltaTime) : 1.0f;
        car.position.x = std::lerp(car.position.x, nextPosition.x, amount);
        car.position.y = std::lerp(car.position.y, nextPosition.y, amount);
        car.position.z = nextPosition.z;
        const float rotation = nextHeading * 180.0f / std::numbers::pi_v<float> + 90.0f;
        car.rotationDegrees += std::remainder(rotation - car.rotationDegrees, 360.0f) * amount;
      }
    }
    const Position trackedPosition =
        hasTrackedObject ? scene.objects[scene.trackedObjectIndex].position : Position{};

    char positionLabel[192]{};
    if (arena) {
      ArenaReadScope read(*arena);
      const GpsDebugInfo gpsDebug = gpsDebugAt(*arena, plots.mapCursor());
      if (gpsDebug.locked)
        std::snprintf(positionLabel, sizeof(positionLabel),
                      "x: %.3f | y: %.3f | z: %.3f | GPS %u/%u lock %.6f, %.6f (%.1fs)",
                      trackedPosition.x, trackedPosition.y, trackedPosition.z, gpsDebug.latitudeId,
                      gpsDebug.longitudeId, gpsDebug.latitude, gpsDebug.longitude,
                      gpsDebug.ageSeconds);
      else
        std::snprintf(positionLabel, sizeof(positionLabel),
                      "x: %.3f | y: %.3f | z: %.3f | GPS %u/%u samples %u/%u (no lock)",
                      trackedPosition.x, trackedPosition.y, trackedPosition.z, gpsDebug.latitudeId,
                      gpsDebug.longitudeId, gpsDebug.latitudeSamples, gpsDebug.longitudeSamples);
    }
    const ImVec2 textPadding(10.0f, 6.0f);
    const ImVec2 textSize = ImGui::CalcTextSize(positionLabel);
    const ImVec2 textMin(imageMin.x + 12.0f,
                         imageMax.y - textSize.y - textPadding.y * 2.0f - 12.0f);
    const ImVec2 textMax(textMin.x + textSize.x + textPadding.x * 2.0f,
                         textMin.y + textSize.y + textPadding.y * 2.0f);
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->PushClipRect(imageMin, imageMax, true);
    drawList->AddRectFilled(textMin, textMax,
                            PhotonUi::colorU32(PhotonUi::withAlpha(palette.panel, 0.88f)),
                            PhotonUi::kFrameRounding);
    drawList->AddRect(textMin, textMax,
                      PhotonUi::colorU32(PhotonUi::withAlpha(palette.border, 0.42f)),
                      PhotonUi::kFrameRounding);
    drawList->AddText(ImVec2(textMin.x + textPadding.x, textMin.y + textPadding.y),
                      PhotonUi::colorU32(palette.text), positionLabel);
    drawList->PopClipRect();

    ImGuiIO& io = ImGui::GetIO();
    if (sceneHovered) {
      if (ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
        scene.camera.yaw -= io.MouseDelta.x * scene.camera.orbitSensitivity;
        scene.camera.pitch += io.MouseDelta.y * scene.camera.orbitSensitivity;
        scene.camera.pitch = std::clamp(scene.camera.pitch, -89.0f, 89.0f);
      }
      if (std::abs(io.MouseWheel) > 0.0f) {
        const float zoomScale =
            std::max(0.1f, 1.0f - io.MouseWheel * scene.camera.zoomSensitivity);
        scene.camera.distance *= zoomScale;
        scene.camera.distance =
            std::clamp(scene.camera.distance, scene.camera.minDistance, scene.camera.maxDistance);
      }
    }
  } else {
    ImGui::Dummy(drawSize);
    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    const ImVec2 textSize = ImGui::CalcTextSize("loading scene");
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->PushClipRect(min, max, true);
    drawList->AddText({(min.x + max.x - textSize.x) * 0.5f, (min.y + max.y - textSize.y) * 0.5f},
                      PhotonUi::colorU32(palette.text), "loading scene");
    drawList->PopClipRect();
  }
#else
  ImGui::Dummy(size);
  const ImVec2 min = ImGui::GetItemRectMin();
  const ImVec2 max = ImGui::GetItemRectMax();
  const char* text = "3D view requires Vulkan rendering";
  const ImVec2 textSize = ImGui::CalcTextSize(text);
  ImGui::GetWindowDrawList()->AddText(
      {(min.x + max.x - textSize.x) * 0.5f, (min.y + max.y - textSize.y) * 0.5f},
      ImGui::GetColorU32(ImGuiCol_TextDisabled), text);
#endif
}

void GUI::liveView(ImGuiWindowFlags flags) {
#if PHOTON_GUI_RENDER_ITEMS
  scene.showing = false;
  const PhotonUi::Palette palette = PhotonUi::palette();

  ImGui::SetNextWindowBgAlpha(0.0f);
  ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
  ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));

  if (ImGui::Begin("Live View", nullptr, flags)) {
    constexpr float controlHeight = 34.0f;
    constexpr float controlGap = 8.0f;
    constexpr const char* eyeIcon = "\uea9a";
    constexpr const char* eyeOffIcon = "\uecf0";
    if (PhotonUi::rowButton("LiveSceneVisibility", showLiveScene ? eyeIcon : eyeOffIcon, "3D model",
                            {126.0f, controlHeight}, palette, showLiveScene))
      showLiveScene = !showLiveScene;
    ImGui::SameLine(0.0f, controlGap);
    if (PhotonUi::rowButton("LiveVideoVisibility", showLiveVideo ? eyeIcon : eyeOffIcon,
                            "Video stream", {144.0f, controlHeight}, palette, showLiveVideo))
      showLiveVideo = !showLiveVideo;

    const ImVec2 available = ImGui::GetContentRegionAvail();
    const ImVec2 contentMin = ImGui::GetCursorScreenPos();
    const ImVec2 contentSize{std::max(available.x, 2.0f), std::max(available.y, 1.0f)};
    constexpr float splitterWidth = 10.0f;
    const bool split = showLiveScene && showLiveVideo && contentSize.x > splitterWidth * 2.0f;
    const float minPaneWidth = std::min(160.0f, contentSize.x * 0.25f);
    float videoWidth = showLiveVideo ? contentSize.x : 0.0f;
    float sceneWidth = showLiveScene ? contentSize.x : 0.0f;
    if (split) {
      videoWidth = std::clamp(contentSize.x * liveVideoFraction, minPaneWidth,
                              contentSize.x - minPaneWidth - splitterWidth);
      sceneWidth = contentSize.x - videoWidth - splitterWidth;
    }
    const ImVec2 drawSize{sceneWidth, contentSize.y};
    if (showLiveScene) drawLiveSceneEmbedded(drawSize);
    if (split) {
      ImGui::SetCursorScreenPos({contentMin.x + sceneWidth, contentMin.y});
      ImGui::InvisibleButton("##LiveViewSplitter", {splitterWidth, contentSize.y});
      const bool splitterHovered = ImGui::IsItemHovered();
      const bool splitterActive = ImGui::IsItemActive();
      if (splitterActive) {
        liveVideoFraction =
            std::clamp(liveVideoFraction - ImGui::GetIO().MouseDelta.x / contentSize.x,
                       minPaneWidth / contentSize.x,
                       (contentSize.x - minPaneWidth - splitterWidth) / contentSize.x);
      }
      const ImVec2 splitMin = ImGui::GetItemRectMin();
      const ImVec2 splitMax = ImGui::GetItemRectMax();
      const ImVec4 splitColor = splitterActive    ? palette.accent
                                : splitterHovered ? palette.text
                                                  : palette.border;
      ImGui::GetWindowDrawList()->AddRectFilled(
          {splitMin.x + 3.0f, splitMin.y + 4.0f}, {splitMax.x - 3.0f, splitMax.y - 4.0f},
          PhotonUi::colorU32(PhotonUi::withAlpha(splitColor, splitterActive ? 0.9f : 0.55f)), 3.0f);
      if (splitterHovered || splitterActive) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    }
    if (showLiveVideo) {
      ImGui::SetCursorScreenPos(
          {contentMin.x + (split ? sceneWidth + splitterWidth : 0.0f), contentMin.y});
      drawFrontCamEmbedded({videoWidth, contentSize.y});
    }
    if (!showLiveScene && !showLiveVideo) {
      const char* hiddenText = "3D model and video stream are hidden";
      const ImVec2 textSize = ImGui::CalcTextSize(hiddenText);
      ImGui::GetWindowDrawList()->AddText({contentMin.x + (contentSize.x - textSize.x) * 0.5f,
                                           contentMin.y + (contentSize.y - textSize.y) * 0.5f},
                                          PhotonUi::colorU32(palette.muted), hiddenText);
    }
  }
  ImGui::End();
  ImGui::PopStyleColor(2);
#else
  videoUi.videoController(flags);
#endif
};

void GUI::dynamicsView(ImGuiWindowFlags flags) {
#if PHOTON_GUI_RENDER_ITEMS
  dynamicsScene.showing = false;
  const bool ready = dynamicsScene.initialized.load() && !dynamicsScene.frames.empty() &&
                     dynamicsScene.frameIndex != nullptr;
  SceneFrame fallbackFrame{};
  SceneFrame& frame = ready ? dynamicsScene.frames[*dynamicsScene.frameIndex] : fallbackFrame;
  const PhotonUi::Palette palette = PhotonUi::palette();

  if (dynamicsObjectIndex >= 0 &&
      dynamicsObjectIndex < static_cast<int>(dynamicsScene.objects.size())) {
    ArenaReadScope read(*arena);
    updateDynamicsPose(*arena, plots.mapCursor(), dynamicsTelemetry,
                       dynamicsScene.objects[dynamicsObjectIndex]);
    applyDynamicsJiggle(dynamicsTelemetry, dynamicsScene.objects[dynamicsObjectIndex]);
  }

  ImGui::SetNextWindowBgAlpha(0.0f);
  ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
  ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
  if (ImGui::Begin("Dynamics", nullptr, flags)) {
    const ImVec2 available = ImGui::GetContentRegionAvail();
    ImVec2 sceneSize{std::max(available.x, 1.0f), std::max(available.y, 1.0f)};
    const ImVec2 sceneMin = ImGui::GetCursorScreenPos();
    constexpr float overlayMargin = 14.0f;
    const ImVec2 overlaySize{std::max(sceneSize.x - overlayMargin * 2.0f, 1.0f),
                             std::max(std::min(164.0f, sceneSize.y - overlayMargin * 2.0f), 1.0f)};
    const ImVec2 overlayMin{sceneMin.x + overlayMargin,
                            sceneMin.y + sceneSize.y - overlaySize.y - overlayMargin};
    const ImVec2 overlayMax{overlayMin.x + overlaySize.x, overlayMin.y + overlaySize.y};

    if (ready) {
      const VkExtent2D nextExtent = quantizeContentExtent(sceneSize, frame.extent);
      if (nextExtent.width != frame.extent.width || nextExtent.height != frame.extent.height) {
        frame.extent = nextExtent;
        dynamicsScene.dirty = true;
      }
      if (ImGui::IsRectVisible(sceneSize)) dynamicsScene.showing = true;
      ImGui::Image(frame.texture, sceneSize);
      const bool hovered =
          ImGui::IsItemHovered() && !ImGui::IsMouseHoveringRect(overlayMin, overlayMax, false);
      ImGuiIO& io = ImGui::GetIO();
      if (hovered) {
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
          dynamicsScene.camera.yaw -= io.MouseDelta.x * dynamicsScene.camera.orbitSensitivity;
          dynamicsScene.camera.pitch += io.MouseDelta.y * dynamicsScene.camera.orbitSensitivity;
          dynamicsScene.camera.pitch = std::clamp(dynamicsScene.camera.pitch, -89.0f, 89.0f);
        }
        if (std::abs(io.MouseWheel) > 0.0f) {
          const float zoomScale =
              std::max(0.1f, 1.0f - io.MouseWheel * dynamicsScene.camera.zoomSensitivity);
          dynamicsScene.camera.distance =
              std::clamp(dynamicsScene.camera.distance * zoomScale,
                         dynamicsScene.camera.minDistance, dynamicsScene.camera.maxDistance);
        }
      }
    } else {
      ImGui::Dummy(sceneSize);
      const ImVec2 textSize = ImGui::CalcTextSize("loading dynamics model");
      ImGui::GetWindowDrawList()->AddText({sceneMin.x + (sceneSize.x - textSize.x) * 0.5f,
                                           sceneMin.y + (sceneSize.y - textSize.y) * 0.5f},
                                          PhotonUi::colorU32(palette.text),
                                          "loading dynamics model");
    }

    ImGui::SetCursorScreenPos(overlayMin);
    constexpr float groupGap = 8.0f;
    constexpr float controlWidth = 88.0f;
    constexpr float imuWidthRatio = 2.0f;
    const float groupWidth =
        std::max((overlaySize.x - controlWidth - groupGap * 5.0f) / (4.0f + imuWidthRatio), 1.0f);
    const float imuWidth = groupWidth * imuWidthRatio;
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 4.0f));

    const auto beginGroup = [&](const char* id, const char* heading, bool active, float width) {
      ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 10.0f));
      ImGui::PushStyleColor(ImGuiCol_ChildBg, PhotonUi::withAlpha(palette.bg, 0.36f));
      const bool open = PhotonUi::beginPanel(
          id, {width, overlaySize.y}, palette, ImGuiChildFlags_AlwaysUseWindowPadding,
          ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar);
      ImGui::PopStyleColor();
      ImGui::PopStyleVar();
      if (open) {
        PhotonUi::label(heading, palette);
        const ImVec2 center{ImGui::GetWindowPos().x + ImGui::GetWindowWidth() - 14.0f,
                            ImGui::GetWindowPos().y + 14.0f};
        ImGui::GetWindowDrawList()->AddCircleFilled(
            center, 3.0f, PhotonUi::colorU32(active ? palette.accent : palette.muted));
      }
      return open;
    };

    const auto valueRow = [](const char* label, const char* format, auto... values) {
      const float rowStart = ImGui::GetCursorPosX();
      ImGui::TextUnformatted(label);
      ImGui::SameLine();
      ImGui::SetCursorPosX(rowStart + 62.0f);
      ImGui::Text(format, values...);
    };

    if (beginGroup("##DynamicsDriver", "DRIVER", dynamicsTelemetry.hasDriverInput, groupWidth)) {
      valueRow("Throttle", "%6.0f %%", dynamicsTelemetry.throttlePercent);
      valueRow("Brake", "%6.0f %%", dynamicsTelemetry.brakePercent);
      valueRow("Steer", "%7.1f deg", dynamicsTelemetry.steeringDegrees);
      valueRow("Road", "%7.1f deg",
               std::clamp(dynamicsTelemetry.steeringDegrees / 12.0f, -35.0f, 35.0f));
    }
    PhotonUi::endPanel();
    ImGui::SameLine(0.0f, groupGap);
    if (beginGroup("##DynamicsWheelSpeed", "WHEEL SPEED", dynamicsTelemetry.hasWheelSpeed,
                   groupWidth)) {
      valueRow("Front L", "%7.0f rpm", dynamicsTelemetry.frontLeftRpm);
      valueRow("Front R", "%7.0f rpm", dynamicsTelemetry.frontRightRpm);
      valueRow("Rear", "%7.0f rpm", dynamicsTelemetry.rearRpm);
    }
    PhotonUi::endPanel();
    ImGui::SameLine(0.0f, groupGap);
    if (beginGroup("##DynamicsSlip", "SLIP", dynamicsTelemetry.hasTireSlip, groupWidth)) {
      const TireSlipSample& slip = dynamicsTelemetry.tireSlip;
      if (slip.hasWheel)
        valueRow("Wheel", "%6.2f m/s", slip.wheelSpeedMps);
      else
        ImGui::TextUnformatted("Wheel      --");
      if (slip.hasGps)
        valueRow("GPS", "%6.2f m/s", slip.gpsSpeedMps);
      else
        ImGui::TextUnformatted("GPS        --");
      if (slip.hasMcVehicle)
        valueRow("MC", "%6.2f m/s", slip.mcVehicleSpeedMps);
      else
        ImGui::TextUnformatted("MC         --");
      if (slip.hasSlipGps)
        valueRow("vs GPS", "%6.1f %%", slip.slipVsGpsPercent);
      else
        ImGui::TextUnformatted("vs GPS     --");
      if (slip.hasSlipMc)
        valueRow("vs MC", "%6.1f %%", slip.slipVsMcPercent);
      else
        ImGui::TextUnformatted("vs MC      --");
    }
    PhotonUi::endPanel();
    ImGui::SameLine(0.0f, groupGap);
    if (beginGroup("##DynamicsSuspension", "SUSPENSION", dynamicsTelemetry.hasSuspension,
                   groupWidth)) {
      valueRow("Front L", "%7.0f", dynamicsTelemetry.suspensionRaw[0]);
      valueRow("Front R", "%7.0f", dynamicsTelemetry.suspensionRaw[1]);
      valueRow("Rear", "%7.0f", dynamicsTelemetry.suspensionRaw[2]);
      ImGui::TextDisabled("*uncalibrated ADC data");
    }
    PhotonUi::endPanel();
    ImGui::SameLine(0.0f, groupGap);
    if (beginGroup("##DynamicsWheelImu", "WHEEL IMU", dynamicsTelemetry.hasImu, imuWidth)) {
      constexpr std::array<const char*, 3> cornerLabels{"FL", "FR", "Rear"};
      for (size_t corner = 0; corner < cornerLabels.size(); ++corner) {
        char accelerationLabel[12]{};
        char gyroLabel[12]{};
        std::snprintf(accelerationLabel, sizeof(accelerationLabel), "%s A", cornerLabels[corner]);
        std::snprintf(gyroLabel, sizeof(gyroLabel), "%s G", cornerLabels[corner]);
        valueRow(accelerationLabel, "%6.2f %6.2f %6.2f g",
                 dynamicsTelemetry.acceleration[corner][0],
                 dynamicsTelemetry.acceleration[corner][1],
                 dynamicsTelemetry.acceleration[corner][2]);
        valueRow(gyroLabel, "%6.1f %6.1f %6.1f dps", dynamicsTelemetry.angularVelocity[corner][0],
                 dynamicsTelemetry.angularVelocity[corner][1],
                 dynamicsTelemetry.angularVelocity[corner][2]);
      }
    }
    PhotonUi::endPanel();
    ImGui::SameLine(0.0f, groupGap);
    if (beginGroup("##DynamicsControls", "TEST", dynamicsTelemetry.jiggle, controlWidth)) {
      const float buttonWidth = ImGui::GetContentRegionAvail().x;
      if (PhotonUi::button("##DynamicsJiggle", "Jiggle", {buttonWidth, 34.0f}, palette,
                           dynamicsTelemetry.jiggle,
                           "Exercise steering, wheels, suspension, roll, and pitch")) {
        dynamicsTelemetry.jiggle = !dynamicsTelemetry.jiggle;
        if (dynamicsTelemetry.jiggle) dynamicsTelemetry.jiggleTime = 0.0f;
      }
    }
    PhotonUi::endPanel();
    ImGui::PopStyleVar();
  }
  ImGui::End();
  ImGui::PopStyleColor(2);
#else
  if (ImGui::Begin("Dynamics", nullptr, flags))
    ImGui::TextUnformatted("The Dynamics 3D view is available with Vulkan rendering.");
  ImGui::End();
#endif
};

void GUI::shaderTest(ImGuiWindowFlags flags) {
#if PHOTON_GUI_RENDER_ITEMS
  testShader.showing = false;
  flags |= ImGuiWindowFlags_NoBackground;
  ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
  if (ImGui::Begin("shader test", nullptr, flags)) {
    const bool ready = testShader.initialized.load() && !testShader.frames.empty() &&
                       testShader.frameIndex != nullptr;
    shaderFrame fallbackFrame{};
    shaderFrame& frame = ready ? testShader.frames[*testShader.frameIndex] : fallbackFrame;
    if (ready) {
      const VkExtent2D nextExtent =
          quantizeContentExtent(ImGui::GetContentRegionAvail(), frame.extent);
      if (nextExtent.width != frame.extent.width || nextExtent.height != frame.extent.height) {
        frame.extent = nextExtent;
        testShader.dirty = true;
      }
      ImVec2 drawSize(frame.extent.width, frame.extent.height);
      drawSize.x = std::max(drawSize.x, 1.0f);
      drawSize.y = std::max(drawSize.y, 1.0f);
      if (ImGui::IsRectVisible(drawSize)) {
        testShader.showing = true;
        const ImVec2 imageMin = ImGui::GetCursorScreenPos();
        const ImVec2 imageMax(imageMin.x + drawSize.x, imageMin.y + drawSize.y);
        ImGui::GetWindowDrawList()->AddImage(frame.texture, imageMin, imageMax);
        ImGui::Dummy(drawSize);
      } else {
        ImGui::Dummy(drawSize);
      };
    } else
      ImGui::Text("loading shader");
  }
  ImGui::End();
  ImGui::PopStyleVar();
  ImGui::PopStyleColor();
#else
  (void)flags;
#endif
};

void GUI::drawButtonShaderOverlay(ImVec2 buttonMin, ImVec2 buttonMax) {
#if PHOTON_GUI_RENDER_ITEMS
  if (!updateAvailable) return;
  const bool ready = buttonShader.initialized.load() && !buttonShader.frames.empty() &&
                     buttonShader.frameIndex != nullptr;
  if (!ready) return;

  shaderFrame& frame = buttonShader.frames[*buttonShader.frameIndex];
  constexpr float expandX = 28.0f;
  constexpr float expandY = 22.0f;
  const ImVec2 overlayMin(buttonMin.x - expandX, buttonMin.y - expandY);
  const ImVec2 overlayMax(buttonMax.x + expandX, buttonMax.y + expandY);
  const ImVec2 overlaySize(overlayMax.x - overlayMin.x, overlayMax.y - overlayMin.y);

  const VkExtent2D nextExtent = quantizeContentExtent(overlaySize, frame.extent);
  if (nextExtent.width != frame.extent.width || nextExtent.height != frame.extent.height) {
    frame.extent = nextExtent;
    buttonShader.dirty = true;
  }

  if (ImGui::IsRectVisible(overlayMin, overlayMax)) {
    buttonShader.showing = true;
    ImGui::GetWindowDrawList()->AddImage(frame.texture, overlayMin, overlayMax);
  }
#else
  (void)buttonMin;
  (void)buttonMax;
#endif
}

void GUI::testFunc(ImGuiWindowFlags flags) {
  if (ImGui::Begin("test page", NULL, flags)) {
    ImGui::Text("wasldfkjasdlfkj");
    bool val1 = ImGui::Button("button1");
    bool val2 = ImGui::Button("button2");
    ImGui::PushID(0);
    Widget::animTextBox("some text here", val1);
    ImGui::PopID();
    ImGui::PushID(1);
    Widget::animTextBox("new text!", false);
    ImGui::PopID();
    ImGui::NewLine();
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::PushID(2);
    Widget::animLine(p, {p.x + 240, p.y}, val1);
    ImGui::PopID();
    ImGui::Text("something random");
    p = ImGui::GetCursorScreenPos();
    ImGui::PushID(3);
    Widget::animLine(p, {p.x + 240, p.y}, val2);
    ImGui::PopID();
    ImGui::PushID(4);
    Widget::animLine(p, {p.x + 240, p.y + 240}, val2);
    ImGui::PopID();
    ImGui::PushID(5);
    Widget::animLine(p, {p.x, p.y + 240}, val2);
    ImGui::PopID();
    ImGui::Text("let us see...");
    auto draw = ImGui::GetWindowDrawList();

    float s = 50;
    p = ImGui::GetCursorScreenPos();
    draw->AddRectFilledMultiColor(p, {p.x + s, p.y + s}, IM_COL32(255, 0, 0, 255),
                                  IM_COL32(0, 255, 0, 255), IM_COL32(0, 0, 255, 255),
                                  IM_COL32(255, 255, 255, 255));
    ImGui::NewLine();
    ImGui::NewLine();
  }
  ImGui::End();
};

void GUI::setTabs() {
  // Hot-reloaded UI modules own separate function-local singletons, so bind them lazily.
  PlotManager& plotMgr = plotManager();
  plotMgr.init(arena, network, &plots);
  customViewTab().init(arena, gpu ? gpu->window : nullptr, this);
  ui::dashboardTab().init(arena);
  tabs.list.clear();
  tabs.list.push_back(
      Tab::bind<CustomViewTab, &CustomViewTab::draw>(customViewTab(), "Custom Views"));
  tabs.list.push_back(Tab::bind<PlotManager, &PlotManager::draw>(plotMgr, "Plots"));
  tabs.list.push_back(Tab::bind<Arena, &Arena::statusUI>(*arena, "Arena"));
  tabs.list.push_back(Tab::bind<GUI, &GUI::networkPage>(*this, "Networks"));
  tabs.list.push_back(
      Tab::bind<ui::DashboardTab, &ui::DashboardTab::draw>(ui::dashboardTab(), "Dashboard"));
  tabs.list.push_back(Tab::bind<GUI, &GUI::liveView>(*this, "Live View"));
#if PHOTON_GUI_RENDER_ITEMS
  tabs.list.push_back(Tab::bind<GUI, &GUI::dynamicsView>(*this, "Dynamics"));
  tabs.list.push_back(Tab::bind<GUI, &GUI::shaderTest>(*this, "WIP"));
#endif
};

void GUI::buildUI() {
  /* Per-Frame state updates */
  updateAvailable = updater.updateAvailable.load();
  settings.setStyle();
  setFont();
  setTabs();
#if defined(APPLE) || defined(__APPLE__)
  ImGui_ImplSDL3_NewFrame();
#endif
  ImGui::NewFrame();
  iam_update_begin_frame();
  iam_clip_update(ImGui::GetIO().DeltaTime);

  /* Per-Frame UI building */
  updateNetworkStatus();
  titleBar.activePage = "Navigation";
  if (!tabs.list.empty() && tabs.index < tabs.list.size())
    titleBar.activePage = tabs.list[tabs.index].name;
  titleBar.draw();
  sideBar.draw(*this);
  canvas.draw(titleBar, sideBar, tabs, titleBar.height);
  plots.timeline(*arena, network,
                 titleBar.connectionConnected && titleBar.connectionProtocol == "DAQ Server",
                 {canvas.pos.x, canvas.pos.y + canvas.size.y}, {canvas.size.x, titleBar.height});

  /* stateful UI building */
  ifKey(ImGuiKey_F3, flags.showFPS, drawFpsOverlay);
  ImGui::Render();
  render();
};
