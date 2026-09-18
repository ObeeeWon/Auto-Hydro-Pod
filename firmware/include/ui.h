#pragma once

#include <cstdint>

class AppState;

namespace ui {

// The UI never writes to the UART itself; main.cpp owns the link.
struct Callbacks {
  void (*sendPump)(bool on) = nullptr;
  void (*sendTemperature)(int celsius) = nullptr;
  void (*dismissAlarm)() = nullptr;
};

void init(const Callbacks& cb, const AppState& state);

// Idempotent: pushes current state into the widgets. Safe to call every frame.
void render(const AppState& state, uint32_t now_ms);

// On-screen log. UART0 is shared with the USB serial port and carries Parker's
// traffic, so this is the only place debug output can go.
void log(const char* fmt, ...);

}  // namespace ui
