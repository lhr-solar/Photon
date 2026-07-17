#pragma once

struct Arena;
struct CustomViewWidget;
struct Network;

namespace CustomViewCan {
void drawMonitor(Arena* arena, Network* network, CustomViewWidget& widget);
void drawControls(Arena* arena, Network* network, CustomViewWidget& widget);
}  // namespace CustomViewCan
