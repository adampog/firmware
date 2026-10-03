// DIAL (DIscovery And Launch) / cast protocol core for the Bruce synthesis.
// Reimplemented from Ghost ESP's DIAL/cast feature (SYNTHESIS §3 port #6); Ghost
// is ESP-IDF/C, so this is a clean Arduino/C++ reimplementation of the open DIAL
// protocol, not a copy. Pure C++ (no Arduino/ESP/socket deps) so it is
// unit-testable on the host like RemoteIdDecoder / pinescan_detector.
//
// DIAL flow (see https://www.dial-multiscreen.org/ specification):
//   1. SSDP M-SEARCH (UDP multicast 239.255.255.250:1900,
//      ST: urn:dial-multiscreen-org:service:dial:1) -> each device replies with a
//      LOCATION header pointing at its UPnP device-description URL.
//   2. HTTP GET that LOCATION -> an Application-URL response header (the DIAL REST
//      base) plus a device-description XML body carrying <friendlyName>.
//   3. DIAL REST at <Application-URL>/<AppName>: GET = app status XML (<name>,
//      <state>), POST = launch (optional form body, e.g. YouTube "v=<id>"),
//      DELETE = stop.
//
// This header provides only the string/parse/build steps; the UDP + HTTP I/O is
// the glue in dial_cast.{h,cpp}.
#pragma once

#include <stddef.h>
#include <stdint.h>

// The DIAL SSDP search target and multicast endpoint (the glue uses these).
#define DIAL_SEARCH_TARGET "urn:dial-multiscreen-org:service:dial:1"
#define DIAL_MULTICAST_ADDR "239.255.255.250"
#define DIAL_MULTICAST_PORT 1900

// Reported application run-state, parsed from a DIAL app-status <state> element.
enum class DialAppState : uint8_t {
    Unknown,     // no/unrecognised <state>
    Stopped,     // installed but not running
    Running,     // currently running
    Hidden,      // running in background (DIAL 2.x)
    Installable, // not installed (state="installable=...")
};

// Build the SSDP M-SEARCH datagram for DIAL discovery into `out` (NUL-terminated).
// `mx` is the MX (max response delay, seconds) advertised to devices.
// Returns the number of bytes written (excluding the NUL), or 0 if `out` is too
// small / invalid.
size_t dialBuildMSearch(char *out, size_t outSize, uint8_t mx = 2);

// Case-insensitive extraction of an HTTP/SSDP header value. Scans the CRLF- or
// LF-delimited header block in `data` (length `len`) for a line "`name`: value",
// copies the trimmed value into `out` (NUL-terminated). Returns true on a match.
bool dialParseHeader(const char *data, size_t len, const char *name, char *out, size_t outSize);

// True if `data` looks like an SSDP response advertising the DIAL service (so the
// glue can ignore unrelated SSDP/NOTIFY traffic on 1900).
bool dialIsDialResponse(const char *data, size_t len);

// Extract the text content of the first <tag> ... </tag> in an XML document,
// namespace- and attribute-tolerant (matches "<tag>" or "<tag ...>"). Copies the
// trimmed inner text into `out` (NUL-terminated). Returns true on a match. Used
// for <friendlyName> (device description) and <name>/<state> (app status).
bool dialExtractTagText(const char *xml, size_t len, const char *tag, char *out, size_t outSize);

// Parse a DIAL app-status XML document's <state> into a DialAppState.
DialAppState dialParseAppState(const char *xml, size_t len);

// Join a DIAL Application-URL base and an app name into "<base>/<appName>" with
// exactly one separating slash, into `out` (NUL-terminated). Returns true on
// success, false on invalid/oversized input.
bool dialBuildAppUrl(const char *appBaseUrl, const char *appName, char *out, size_t outSize);

// Extract the href of the DIAL app instance from an app-status XML document's
// <link rel="run" href="..."/> element, into `out` (NUL-terminated). Returns
// true if found; when absent the caller should default the href to "run".
bool dialExtractRunHref(const char *xml, size_t len, char *out, size_t outSize);

// Build the DIAL "stop" target "<base>/<appName>/<runHref>" (the app instance URL
// that a DELETE request stops), into `out` (NUL-terminated). A NULL/empty
// `runHref` defaults to "run". Returns true on success.
bool dialBuildStopUrl(const char *appBaseUrl, const char *appName, const char *runHref, char *out,
                      size_t outSize);

// Resolve a possibly-relative Application-URL against the device-description base
// URL. If `appUrl` is absolute (starts with http:// or https://) it is copied
// through; otherwise scheme://host[:port] from `baseUrl` is prepended. Result is
// NUL-terminated in `out`. Returns true on success.
bool dialResolveUrl(const char *baseUrl, const char *appUrl, char *out, size_t outSize);

// Build the form-encoded POST body to launch YouTube with a given video id
// ("v=<videoId>"), into `out` (NUL-terminated). An empty/NULL `videoId` yields an
// empty body (bytes written 0) — a bare launch. Returns bytes written.
size_t dialBuildYouTubeLaunchBody(char *out, size_t outSize, const char *videoId);
