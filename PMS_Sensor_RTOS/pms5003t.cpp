#include "pms5003t.h"
#include "app_config.h"

static inline uint16_t be16(const uint8_t* p) { return ((uint16_t)p[0] << 8) | p[1]; }

bool PmsParser::feed(uint8_t b, PmsRaw& out) {
  if (_idx == 0) {                         // hunting for 0x42
    if (b == 0x42) _buf[_idx++] = b;
    return false;
  }
  if (_idx == 1) {                         // expecting 0x4D
    if (b == 0x4D) _buf[_idx++] = b;
    else _idx = (b == 0x42) ? (_buf[0] = 0x42, 1) : 0;
    return false;
  }
  _buf[_idx++] = b;
  if (_idx < sizeof(_buf)) return false;

  _idx = 0;                                // frame complete - decode or resync
  uint16_t sum = 0;
  for (int i = 0; i < 30; i++) sum += _buf[i];
  if (sum != be16(&_buf[30]) || be16(&_buf[2]) != 28) { badFrames++; return false; }

#if PMS_USE_ATMOSPHERIC
  out.pm2_5 = be16(&_buf[12]);
  out.pm10  = be16(&_buf[14]);
#else
  out.pm2_5 = be16(&_buf[6]);
  out.pm10  = be16(&_buf[8]);
#endif
  out.temp  = (int16_t)be16(&_buf[24]) / 10.0f;
  out.humid = be16(&_buf[26]) / 10.0f;
  return true;
}
