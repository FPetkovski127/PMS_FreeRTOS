#pragma once
#include "app_config.h"

struct SlmStatus {
  bool  running;       // measuring now
  bool  valid;         // at least one 1-second value exists
  float leq1s;         // LAeq over the last second [dBA]
  float lshort;        // LAeq over the last 125 ms block [dBA]
  bool  overload;      // last second above the acoustic overload point
  bool  low;           // last second below the microphone noise floor
};

#if ENABLE_SLM
void      slmBegin();
void      slmSetActive(bool on);
bool      slmIsActive();
SlmStatus slmStatus();
// Energy average (LAeq) and maximum 125 ms level since the previous call.
// Returns false when no valid second was measured in the window.
bool      slmTakeWindow(float* laeq, float* lamax);
void      slmRequestFft();         // prints one spectrum (START/END format of inmp441.py)
void      slmPrint(Print& out);    // statistics for the console
void      slmDiag(Print& out);     // 'slm raw': raw microphone data statistics
#else
inline void      slmBegin() {}
inline void      slmSetActive(bool) {}
inline bool      slmIsActive() { return false; }
inline SlmStatus slmStatus() { return SlmStatus{false, false, 0, 0, false, false}; }
inline bool      slmTakeWindow(float*, float*) { return false; }
inline void      slmRequestFft() {}
inline void      slmPrint(Print& out) { out.println("SLM disabled (ENABLE_SLM 0)"); }
inline void      slmDiag(Print& out) { out.println("SLM disabled (ENABLE_SLM 0)"); }
#endif
