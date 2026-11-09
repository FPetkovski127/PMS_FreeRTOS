#pragma once
// ============================================================================
//  I2Console - serial command line + guided setup wizard
//
//  Read-only commands work for everybody:  help, status, show
//  Anything that changes a parameter needs the ACCESS CODE first:
//      login <code>      (unlocked for 5 min, 3 wrong tries -> 30 s lockout)
//  Wizard:  setup   -> every parameter is shown with its CURRENT value;
//           press ENTER to keep it, type a new value to change it,
//           '-' clears optional values, 'cancel' aborts.
// ============================================================================
#include <Arduino.h>

// Extension: return true when the command was handled.
typedef bool (*I2ConsoleExt)(const String& cmd, const String& args);
typedef void (*I2ConsoleHook)();

void i2consoleBegin(I2ConsoleExt ext, const char* extHelp,
                    I2ConsoleHook onStatus, I2ConsoleHook onSettingsChanged);
bool i2consoleAuthed();            // true while the access code is unlocked
bool i2consoleRequireAuth();       // prints a message and returns false if locked
