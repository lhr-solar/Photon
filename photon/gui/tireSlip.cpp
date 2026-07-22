#include "tireSlip.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numbers>
#include <string_view>

namespace {

constexpr double kWheelDiameterInches = 21.56;
constexpr double kInchesToMeters = 0.0254;
constexpr double kWheelDiameterMeters = kWheelDiameterInches * kInchesToMeters;
constexpr double kMinGroundSpeedMps = 0.5;
constexpr double kFreshnessSeconds = 5.0;
constexpr uint32_t kVelocityMessageId = 1059;

struct FrameView {
  const Message* message{};
  const double* times{};
  uint32_t count{};
  uint32_t index{};

  double time(int offset = 0) const { return times[index + offset]; }
  double value(uint32_t signal, int offset = 0) const {
    const Signal* candidate = message->signals[signal];
    return candidate ? static_cast<const double*>(candidate->data)[index + offset]
                     : std::numeric_limits<double>::quiet_NaN();
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

uint32_t signalIndex(const Message& message, std::string_view name) {
  for (uint32_t signal = 0; signal < message.signalCount; ++signal) {
    const Signal* candidate = message.signals[signal];
    if (candidate && candidate->name == name) return signal;
  }
  return SIGNAL_MAX;
}

bool signalNamedAt(const Arena& arena, uint32_t id, std::string_view signalName, double cursor,
                   float& value) {
  FrameView frame{};
  if (!frameAtOrLatest(arena, id, cursor, frame) || cursor - frame.time() > kFreshnessSeconds)
    return false;
  const uint32_t signal = signalIndex(*frame.message, signalName);
  if (signal == SIGNAL_MAX) return false;
  const double sample = frame.value(signal);
  if (!std::isfinite(sample)) return false;
  value = static_cast<float>(sample);
  return true;
}

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

  consider(rawValue);
  if (rawValue >= 0.0 && rawValue <= std::numeric_limits<uint32_t>::max() &&
      std::floor(rawValue) == rawValue) {
    const uint32_t bits = static_cast<uint32_t>(rawValue);
    consider(static_cast<double>(std::bit_cast<float>(bits)));
    consider(static_cast<double>(std::bit_cast<int32_t>(bits)) * 1e-6);
  }
  return found;
}

bool gpsLatLonAt(const FrameView& latitude, const FrameView& longitude, int offset, double& lat,
                 double& lon, double& time) {
  const int latitudeIndex = static_cast<int>(latitude.index) + offset;
  const int longitudeIndex = static_cast<int>(longitude.index) + offset;
  if (latitudeIndex < 0 || longitudeIndex < 0 ||
      latitudeIndex >= static_cast<int>(latitude.count) ||
      longitudeIndex >= static_cast<int>(longitude.count) ||
      std::abs(latitude.times[latitudeIndex] - longitude.times[longitudeIndex]) > 1.0)
    return false;
  if (!normalizeGpsCoordinate(latitude.value(0, offset), true, lat) ||
      !normalizeGpsCoordinate(longitude.value(0, offset), false, lon) || (lat == 0.0 && lon == 0.0))
    return false;
  time = std::max(latitude.times[latitudeIndex], longitude.times[longitudeIndex]);
  return true;
}

double metersBetween(double lat0, double lon0, double lat1, double lon1) {
  constexpr double metersPerLatitudeDegree = 111320.0;
  constexpr double metersPerLongitudeDegree = 76745.74845956931;  // ~Brainerd latitude
  const double north = (lat1 - lat0) * metersPerLatitudeDegree;
  const double east = (lon1 - lon0) * metersPerLongitudeDegree;
  return std::hypot(east, north);
}

float slipPercent(float wheelMps, float groundMps) {
  const float denom = std::max(std::abs(groundMps), static_cast<float>(kMinGroundSpeedMps));
  return 100.0f * (wheelMps - groundMps) / denom;
}

float wheelSpeedFromRpm(float rpm) {
  return static_cast<float>(rpm * (std::numbers::pi_v<double> * kWheelDiameterMeters) / 60.0);
}

bool gpsSpeedAt(const Arena& arena, double cursor, float& speedMps) {
  struct GpsMessagePair {
    uint32_t latitude;
    uint32_t longitude;
  };
  constexpr GpsMessagePair pairs[]{{4288, 4289}, {5888, 5889}};

  bool found = false;
  float bestSpeed = 0.0f;
  double bestTime = -1.0;
  for (const GpsMessagePair pair : pairs) {
    FrameView latitude{}, longitude{};
    if (!frameAtOrLatest(arena, pair.latitude, cursor, latitude) ||
        !frameAtOrLatest(arena, pair.longitude, cursor, longitude))
      continue;
    if (cursor - latitude.time() > kFreshnessSeconds || cursor - longitude.time() > kFreshnessSeconds)
      continue;

    double lat0 = 0.0;
    double lon0 = 0.0;
    double time0 = 0.0;
    double lat1 = 0.0;
    double lon1 = 0.0;
    double time1 = 0.0;
    // Prefer current←previous segment ending at/before cursor.
    if (!gpsLatLonAt(latitude, longitude, 0, lat0, lon0, time0) ||
        !gpsLatLonAt(latitude, longitude, -1, lat1, lon1, time1))
      continue;
    const double dt = time0 - time1;
    if (!(dt > 0.05) || dt > 5.0) continue;
    const double meters = metersBetween(lat1, lon1, lat0, lon0);
    if (!std::isfinite(meters)) continue;
    const float speed = static_cast<float>(meters / dt);
    if (!found || time0 > bestTime) {
      bestSpeed = speed;
      bestTime = time0;
      found = true;
    }
  }
  if (!found) return false;
  speedMps = bestSpeed;
  return true;
}

}  // namespace

bool tireSlipAt(const Arena& arena, double cursor, TireSlipSample& sample) {
  sample = {};
  float rpm = 0.0f;
  float mcVelocity = 0.0f;
  float gpsSpeed = 0.0f;
  sample.hasWheel = signalNamedAt(arena, kVelocityMessageId, "MC_MotorVelocity", cursor, rpm);
  sample.hasMcVehicle =
      signalNamedAt(arena, kVelocityMessageId, "MC_VehicleVelocity", cursor, mcVelocity);
  sample.hasGps = gpsSpeedAt(arena, cursor, gpsSpeed);

  if (sample.hasWheel) {
    sample.motorRpm = rpm;
    sample.wheelSpeedMps = wheelSpeedFromRpm(rpm);
  }
  if (sample.hasMcVehicle) sample.mcVehicleSpeedMps = std::abs(mcVelocity);
  if (sample.hasGps) sample.gpsSpeedMps = gpsSpeed;

  if (sample.hasWheel && sample.hasGps &&
      sample.gpsSpeedMps >= static_cast<float>(kMinGroundSpeedMps)) {
    sample.slipVsGpsPercent = slipPercent(sample.wheelSpeedMps, sample.gpsSpeedMps);
    sample.hasSlipGps = std::isfinite(sample.slipVsGpsPercent);
  }
  if (sample.hasWheel && sample.hasMcVehicle &&
      sample.mcVehicleSpeedMps >= static_cast<float>(kMinGroundSpeedMps)) {
    sample.slipVsMcPercent = slipPercent(sample.wheelSpeedMps, sample.mcVehicleSpeedMps);
    sample.hasSlipMc = std::isfinite(sample.slipVsMcPercent);
  }
  return sample.hasWheel || sample.hasGps || sample.hasMcVehicle;
}

size_t tireSlipSeries(const Arena& arena, double rangeStart, double rangeEnd, double* times,
                      double* slipGpsPercent, double* slipMcPercent, size_t capacity) {
  if (!times || !slipGpsPercent || !slipMcPercent || capacity == 0) return 0;
  FrameView motor{};
  if (!frameAt(arena, kVelocityMessageId, rangeEnd, motor) &&
      !frameLatest(arena, kVelocityMessageId, motor))
    return 0;
  const auto* motorTimes = static_cast<const double*>(motor.message->timeData);
  const uint32_t count =
      motor.message->signalSize.value.load(std::memory_order_acquire) / sizeof(double);
  if (!count) return 0;

  auto begin = std::lower_bound(motorTimes, motorTimes + count, rangeStart);
  auto end = std::upper_bound(motorTimes, motorTimes + count, rangeEnd);
  if (begin >= end) return 0;

  const size_t available = static_cast<size_t>(end - begin);
  const size_t step = std::max<size_t>(1, available / capacity);
  size_t written = 0;
  for (size_t i = 0; i < available && written < capacity; i += step) {
    const double t = begin[static_cast<std::ptrdiff_t>(i)];
    TireSlipSample sample{};
    if (!tireSlipAt(arena, t, sample)) continue;
    if (!sample.hasSlipGps && !sample.hasSlipMc) continue;
    times[written] = t;
    slipGpsPercent[written] = sample.hasSlipGps ? sample.slipVsGpsPercent : std::numeric_limits<double>::quiet_NaN();
    slipMcPercent[written] = sample.hasSlipMc ? sample.slipVsMcPercent : std::numeric_limits<double>::quiet_NaN();
    ++written;
  }
  return written;
}
