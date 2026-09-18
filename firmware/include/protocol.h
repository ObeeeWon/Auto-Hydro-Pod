#pragma once

#include <cstddef>

#include "types.h"

enum class MessageKind : uint8_t {
  Unknown = 0,
  Telemetry,
  Ack,
};

struct InboundMessage {
  MessageKind kind = MessageKind::Unknown;
  Telemetry telemetry{};
  bool ack_ok = false;
};

// Parses one NDJSON line. Returns false on invalid JSON or an unknown `type`;
// callers drop the line and keep running.
bool parseMessage(const char* json, size_t len, InboundMessage& out);

// Command builders. Both append the trailing '\n'. Return the byte count
// written, or 0 if the buffer is too small.
// Pump uses the original 1 = on / 2 = off encoding (not telemetry's 0/1).
size_t buildPumpCommand(char* out, size_t cap, bool on);
size_t buildTemperatureCommand(char* out, size_t cap, int celsius);
