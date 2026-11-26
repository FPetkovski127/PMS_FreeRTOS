#pragma once
// PMS5003T frame parser, returns the raw values (calibration is done in task_pms.cpp).
// Frame, 32 bytes: 42 4D | length | 13 data words | checksum
//   bytes  4..9    PM1.0 / PM2.5 / PM10, CF=1
//   bytes 10..15   PM1.0 / PM2.5 / PM10, atmospheric
//   bytes 16..23   particle counts
//   bytes 24..25   temperature, 0.1 C, signed
//   bytes 26..27   humidity, 0.1 %
//   bytes 28..29   reserved
//   bytes 30..31   checksum = sum of bytes 0..29
#include <Arduino.h>

struct PmsRaw { float pm2_5, pm10, temp, humid; };

class PmsParser {
 public:
  void reset() { _idx = 0; }
  // feed one byte, returns true when a complete frame with a correct checksum was decoded
  bool feed(uint8_t b, PmsRaw& out);
  uint32_t badFrames = 0;
 private:
  uint8_t _buf[32];
  uint8_t _idx = 0;
};
