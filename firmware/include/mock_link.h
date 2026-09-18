#pragma once

#include <cstddef>
#include <cstdint>

// Stands in for Parker's controller so the panel can be developed and demoed
// without the box. It emits real NDJSON lines through the real parser, so the
// only untested piece is the wire itself.
//
// The scenario is time-compressed: moisture steps every 5 s instead of 30 s.
class MockLink {
 public:
  void begin(uint32_t now_ms);

  // Produces at most one line per call. Returns true when `out` holds one.
  bool poll(uint32_t now_ms, char* out, size_t cap, size_t& len);

  // Feed whatever the panel wrote to the link; queues an ACK.
  void onCommand(const char* line, size_t len, uint32_t now_ms);

 private:
  int alertFor(int raw) const;
  bool waterLowAt(uint32_t elapsed_ms) const;
  bool testButtonAt(uint32_t elapsed_ms) const;

  uint32_t start_ms_ = 0;
  uint32_t next_telemetry_ms_ = 0;
  uint32_t next_sample_ms_ = 0;
  size_t step_ = 0;

  int raw_ = 2300;
  int water_ = 0;
  int pump_ = 0;
  int setpoint_ = 24;  // deliberately not the panel default, so the echo is visible
  int test_button_ = 0;
  int test_count_ = 0;

  bool ack_queued_ = false;
  uint32_t ack_due_ms_ = 0;
};
