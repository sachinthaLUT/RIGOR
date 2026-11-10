#pragma once
#include <stdint.h>

// ===========================================================================
//  WEBSERVER  --  a SoftAP + file browser for pulling logs off the rig.
//
//  LIFECYCLE: the access point runs ONLY while the Download dialog is open.
//  web_start() when it opens, web_stop() when it closes. Leaving an open AP
//  running on a lab instrument is not something to do by accident, and the
//  S3's WiFi shares memory bandwidth with the RGB panel -- expect mild
//  flicker while it is up, which is another reason to keep it brief.
//
//  THE PAGE OFFERS: every stored .csv with its size, sorted by name or by
//  newest, one-tap download, free-space indicator, and an erase-all button
//  behind a confirmation.
// ===========================================================================

// SSID / password. WPA2 needs at least 8 characters.
// (10/2026: SSID renamed from "ForceTester-V1" -- forget the old network on
//  your phone and join this one.)
#define WIFI_AP_SSID_DEFAULT  "RIGOR"
#define WIFI_AP_PASS_DEFAULT  "forcerig2026"

void        web_start();
void        web_stop();
bool        web_is_running();

// Dotted-quad address of the AP, for display on the dialog. "-" when stopped.
const char* web_ip();
const char* web_ssid();
const char* web_pass();

// Number of clients currently associated. Lets the dialog show that the
// operator's phone actually connected, instead of leaving them guessing.
int         web_client_count();

// Pump the HTTP server. Must be called regularly from the LVGL task while the
// AP is up, or connections hang. Cheap no-op when stopped.
void        web_poll();
