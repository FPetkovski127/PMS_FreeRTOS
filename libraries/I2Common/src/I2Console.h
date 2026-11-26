#pragma once
// Serial command line. help/status/show work without login, anything that changes
// settings needs "login <code>" first (5 min, 3 wrong tries lock it for 30 s).
// "setup" starts a wizard that shows the current value of every setting.
#include <Arduino.h>

// the sketch can add its own commands; return true if the command was handled
typedef bool (*I2ConsoleExt)(const String& cmd, const String& args);
typedef void (*I2ConsoleHook)();

void i2consoleBegin(I2ConsoleExt ext, const char* extHelp,
                    I2ConsoleHook onStatus, I2ConsoleHook onSettingsChanged);
bool i2consoleAuthed();            // true while unlocked
bool i2consoleRequireAuth();       // prints a message and returns false if locked
