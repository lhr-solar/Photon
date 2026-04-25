#pragma once

namespace kart {

struct Inputs {
    float steering;  // [-1, +1] negative = left
    float throttle;  // [ 0, +1]
    float brake;     // [ 0, +1]
    float drift;     // [ 0, +1]
};

// Call once per frame from the dashboard. Detects a chord of both bottom-left
// and bottom-right corner clicks within a short window; on chord, spawns the
// SMK child process. While SMK is running, streams `inputs` over UDP to it.
void update(const Inputs& inputs);

bool isRunning();

} // namespace kart
