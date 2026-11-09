#pragma once
// ============================================================================
//  PMS5003T frame parser (RAW values - calibration is applied elsewhere).
//
//  Frame = 32 bytes:  42 4D | len(2) | 13 data words | checksum(2)
//   word 0-2  PM1.0 / PM2.5 / PM10  (CF=1, factory)      bytes  4..9
//   word 3-5  PM1.0 / PM2.5 / PM10  (atmospheric)        bytes 10..15
//   word 6-9  particle counts 0.3 / 0.5 / 1.0 / 2.5 um   bytes 16..23
//   word 10   temperature  [0.1 C, signed]               bytes 24..25
//   word 11   humidity     [0.1 %RH]                     bytes 26..27
//   word 12   reserved                                   bytes 28..29
//   checksum  = sum of bytes 0..29                       bytes 30..31
//  NOTE: verify offsets against your PMS5003T datasheet / old PMS5003T.h with
//  a known reading (the old header was not part of the workspace).
// ============================================================================
#include <Arduino.h>

struct PmsRaw { float pm2_5, pm10, temp, humid; };

class PmsParser {
 public:
  void reset() { _idx = 0; }
  // Feed one byte. Returns true when a complete, checksum-valid frame was decoded.
  bool feed(uint8_t b, PmsRaw& out);
  uint32_t badFrames = 0;
 private:
  uint8_t _buf[32];
  uint8_t _idx = 0;
};
