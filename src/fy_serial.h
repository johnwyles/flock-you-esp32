// fy_serial.h — Serial debug command parser for flock-you-esp32
#pragma once

#include <Arduino.h>

// Process pending serial commands. Call once per loop() iteration.
void fySerialProcess();
