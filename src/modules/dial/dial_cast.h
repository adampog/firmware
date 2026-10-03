// DIAL / cast network glue for Bruce (SYNTHESIS §3 port #6). Drives the pure
// dial_protocol core over WiFiUDP (SSDP discovery) + HTTPClient (device
// description, app status, launch, stop). Requires an active STA connection.
// Whole implementation is gated on BRUCE_DIAL.
#pragma once

#ifdef BRUCE_DIAL

#include <stdint.h>

// On-device menu entry (registered in WifiMenu): connect if needed, discover
// DIAL devices on the LAN, then launch/stop an app (default YouTube) on a chosen
// device.
void dialCastScreen();

// Headless (serial) discovery: scan for `seconds`, resolve each device's
// Application-URL + name + YouTube state, and print the list to Serial. Populates
// the shared device table that dialLaunchIndex()/dialStopIndex() act on.
void dialScanRun(uint32_t seconds);

// Number of devices found by the most recent scan.
int dialDeviceCount();

// Launch / stop an app on a device from the last scan (0-based index). `app`
// defaults to "YouTube" when NULL/empty; `videoId` (launch only) is an optional
// DIAL "v=<id>" body. Return true on an accepted request.
bool dialLaunchIndex(int index, const char *app, const char *videoId);
bool dialStopIndex(int index, const char *app);

#endif // BRUCE_DIAL
