// flock-you-esp32 — Simple web server for detection data
// Serves JSON detections + status when toggled from promiscuous mode

#ifndef FY_WEBSERVER_H
#define FY_WEBSERVER_H

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>

extern WebServer gWebServer;

// Start AP and web server
void fyWebServerStart();

// Stop web server and resume scanning
void fyWebServerStop();

// Handle pending requests (call from loop)
void fyWebServerTick();

// Check if web server is active
bool fyWebServerActive();

#endif /* FY_WEBSERVER_H */
