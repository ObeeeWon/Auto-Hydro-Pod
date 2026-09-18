// Auto Hydro HMI — frozen constants from docs/INTEGRATION_GUIDE_en.md v1.2.
// Changing anything here changes the wire contract. Talk to Parker first.
#pragma once

#include <cstddef>
#include <cstdint>

namespace cfg {

// --- Link ---------------------------------------------------------------
constexpr uint32_t kUartBaud = 115200;   // 8N1, 3.3 V, panel UART0 (IO43 TX / IO44 RX)
constexpr int kUartRxPin = 44;
constexpr int kUartTxPin = 43;
constexpr size_t kMaxLineLen = 256;      // protocol caps a line at <256 bytes

// --- Moisture (capacitive probe, 12-bit ADC, higher = wetter) -----------
constexpr int kAdcMax = 4095;
constexpr int kMoistureRawLow = 1844;    // 45% — too-dry line
constexpr int kMoistureRawHigh = 2457;   // 60% — too-wet line
constexpr int kMoisturePctLow = 45;
constexpr int kMoisturePctHigh = 60;
constexpr int kMoisturePctTarget = 55;   // Parker's control target

// --- Temperature setpoint ----------------------------------------------
constexpr int kTempMinC = 15;
constexpr int kTempMaxC = 30;
constexpr int kTempDefaultC = 22;        // shown until the operator sets one

// --- Timing -------------------------------------------------------------
constexpr uint32_t kLinkTimeoutMs = 2000;    // no telemetry for 2 s => comms fault
constexpr uint32_t kAckTimeoutMs = 1000;     // ACK must arrive within 1 s
constexpr uint32_t kSetpointEchoGraceMs = 1000;  // let the controller catch up after an accepted setpoint
constexpr uint32_t kAlarmMinFlashMs = 3000;  // ignore touch while flashing (mist false-touch)
constexpr uint32_t kAlarmFlashPeriodMs = 500;
constexpr uint32_t kUiRefreshMs = 100;
constexpr uint32_t kTestLampHoldMs = 800;  // latch the test lamp so a short press is visible

}  // namespace cfg
