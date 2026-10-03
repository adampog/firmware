// Pure DIAL/cast protocol core. See dial_protocol.h. No Arduino/ESP/socket deps
// so the host unit tests exercise exactly the code the firmware compiles.
#include "dial_protocol.h"

#include <cstdio>
#include <cstring>

namespace {

char lower(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c; }

bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

// Case-insensitive test of whether NUL-terminated `s` begins with `prefix`
// (stops at s's NUL, so it never reads past the string).
bool ciStartsWith(const char *s, const char *prefix) {
    for (size_t i = 0; prefix[i] != '\0'; ++i) {
        if (s[i] == '\0' || lower(s[i]) != lower(prefix[i])) return false;
    }
    return true;
}

// Case-insensitive search for `needle` within data[0..len). Returns a pointer to
// the first match, or nullptr.
const char *ciFind(const char *data, size_t len, const char *needle) {
    const size_t n = std::strlen(needle);
    if (n == 0 || n > len) return nullptr;
    for (size_t i = 0; i + n <= len; ++i) {
        size_t j = 0;
        for (; j < n; ++j) {
            if (lower(data[i + j]) != lower(needle[j])) break;
        }
        if (j == n) return data + i;
    }
    return nullptr;
}

// Copy [begin,end) into out (NUL-terminated), trimming surrounding whitespace.
// Returns false if out is invalid or too small for the trimmed text.
bool copyTrimmed(const char *begin, const char *end, char *out, size_t outSize) {
    if (out == nullptr || outSize == 0) return false;
    while (begin < end && isSpace(*begin)) ++begin;
    while (end > begin && isSpace(end[-1])) --end;
    size_t n = (size_t)(end - begin);
    if (n + 1 > outSize) return false;
    std::memcpy(out, begin, n);
    out[n] = '\0';
    return true;
}

// Locate the trimmed inner text of the first <tag> ... </tag> in xml[0..len),
// setting [*outBegin,*outEnd) into the original buffer (no copy). Namespace- and
// attribute-tolerant; skips self-closing <tag/>. Returns false if not found.
bool findTagText(const char *xml, size_t len, const char *tag, const char **outBegin,
                 const char **outEnd) {
    if (xml == nullptr || tag == nullptr) return false;
    const size_t tagLen = std::strlen(tag);
    if (tagLen == 0) return false;
    const char *p = xml;
    const char *end = xml + len;
    while (p < end) {
        const char *lt = (const char *)std::memchr(p, '<', (size_t)(end - p));
        if (lt == nullptr) return false;
        const char *name = lt + 1;
        if ((size_t)(end - name) > tagLen && std::memcmp(name, tag, tagLen) == 0) {
            char boundary = name[tagLen];
            if (boundary == '>' || boundary == ' ' || boundary == '\t' || boundary == '/' ||
                boundary == '\r' || boundary == '\n') {
                const char *gt = (const char *)std::memchr(name, '>', (size_t)(end - name));
                if (gt == nullptr) return false;
                if (gt[-1] == '/') { // self-closing <tag/> has no text
                    p = gt + 1;
                    continue;
                }
                const char *content = gt + 1;
                const char *close = (const char *)std::memchr(content, '<', (size_t)(end - content));
                if (close == nullptr) close = end;
                while (content < close && isSpace(*content)) ++content;
                while (close > content && isSpace(close[-1])) --close;
                *outBegin = content;
                *outEnd = close;
                return true;
            }
        }
        p = lt + 1;
    }
    return false;
}

// Case-insensitive test of whether [begin,end) begins with `prefix` (bounded, so
// it classifies long values like "installable=<url>" without copying them).
bool ciStartsWithN(const char *begin, const char *end, const char *prefix) {
    for (size_t i = 0; prefix[i] != '\0'; ++i) {
        if (begin + i >= end || lower(begin[i]) != lower(prefix[i])) return false;
    }
    return true;
}

} // namespace

size_t dialBuildMSearch(char *out, size_t outSize, uint8_t mx) {
    if (out == nullptr || outSize == 0) return 0;
    if (mx == 0) mx = 1;
    if (mx > 5) mx = 5; // keep discovery responsive; 1-5s is the useful range
    int written = std::snprintf(
        out, outSize,
        "M-SEARCH * HTTP/1.1\r\n"
        "HOST: " DIAL_MULTICAST_ADDR ":%d\r\n"
        "MAN: \"ssdp:discover\"\r\n"
        "MX: %u\r\n"
        "ST: " DIAL_SEARCH_TARGET "\r\n"
        "\r\n",
        DIAL_MULTICAST_PORT, (unsigned)mx
    );
    if (written <= 0 || (size_t)written >= outSize) return 0; // truncated
    return (size_t)written;
}

bool dialParseHeader(const char *data, size_t len, const char *name, char *out, size_t outSize) {
    if (data == nullptr || name == nullptr || out == nullptr || outSize == 0) return false;
    const size_t nameLen = std::strlen(name);
    if (nameLen == 0) return false;

    const char *p = data;
    const char *end = data + len;
    while (p < end) {
        // [p, lineEnd) is one header line (without the newline).
        const char *lineEnd = p;
        while (lineEnd < end && *lineEnd != '\n') ++lineEnd;
        const char *trimEnd = (lineEnd > p && lineEnd[-1] == '\r') ? lineEnd - 1 : lineEnd;

        const char *s = p;
        while (s < trimEnd && isSpace(*s)) ++s;
        // Match the header name case-insensitively.
        if ((size_t)(trimEnd - s) >= nameLen) {
            size_t j = 0;
            for (; j < nameLen; ++j) {
                if (lower(s[j]) != lower(name[j])) break;
            }
            if (j == nameLen) {
                const char *c = s + nameLen;
                while (c < trimEnd && isSpace(*c)) ++c;
                if (c < trimEnd && *c == ':') {
                    return copyTrimmed(c + 1, trimEnd, out, outSize);
                }
            }
        }
        p = (lineEnd < end) ? lineEnd + 1 : end;
    }
    return false;
}

bool dialIsDialResponse(const char *data, size_t len) {
    if (data == nullptr) return false;
    return ciFind(data, len, "urn:dial-multiscreen-org") != nullptr;
}

bool dialExtractTagText(const char *xml, size_t len, const char *tag, char *out, size_t outSize) {
    if (out == nullptr || outSize == 0) return false;
    const char *begin = nullptr;
    const char *end = nullptr;
    if (!findTagText(xml, len, tag, &begin, &end)) return false;
    return copyTrimmed(begin, end, out, outSize);
}

DialAppState dialParseAppState(const char *xml, size_t len) {
    const char *begin = nullptr;
    const char *end = nullptr;
    if (!findTagText(xml, len, "state", &begin, &end)) return DialAppState::Unknown;
    // Classify by prefix in place — the "installable=<url>" value is unbounded.
    if (ciStartsWithN(begin, end, "running")) return DialAppState::Running;
    if (ciStartsWithN(begin, end, "stopped")) return DialAppState::Stopped;
    if (ciStartsWithN(begin, end, "hidden")) return DialAppState::Hidden;
    if (ciStartsWithN(begin, end, "installable")) return DialAppState::Installable;
    return DialAppState::Unknown;
}

bool dialBuildAppUrl(const char *appBaseUrl, const char *appName, char *out, size_t outSize) {
    if (appBaseUrl == nullptr || appName == nullptr || out == nullptr || outSize == 0) return false;
    size_t baseLen = std::strlen(appBaseUrl);
    if (baseLen == 0) return false;
    while (baseLen > 0 && appBaseUrl[baseLen - 1] == '/') --baseLen; // drop trailing slash(es)
    while (*appName == '/') ++appName;                              // drop leading slash(es)
    if (baseLen == 0 || *appName == '\0') return false;
    int written = std::snprintf(out, outSize, "%.*s/%s", (int)baseLen, appBaseUrl, appName);
    return written > 0 && (size_t)written < outSize;
}

bool dialExtractRunHref(const char *xml, size_t len, char *out, size_t outSize) {
    if (xml == nullptr || out == nullptr || outSize == 0) return false;
    const char *p = xml;
    const char *end = xml + len;
    while (p < end) {
        const char *lt = (const char *)std::memchr(p, '<', (size_t)(end - p));
        if (lt == nullptr) return false;
        // Only consider <link ...> elements.
        if ((size_t)(end - lt) > 5 && std::memcmp(lt, "<link", 5) == 0) {
            const char *gt = (const char *)std::memchr(lt, '>', (size_t)(end - lt));
            const char *elemEnd = gt ? gt : end;
            // Require rel="run" somewhere in this element, then read its href.
            if (ciFind(lt, (size_t)(elemEnd - lt), "rel=\"run\"") != nullptr) {
                const char *href = ciFind(lt, (size_t)(elemEnd - lt), "href=\"");
                if (href != nullptr) {
                    const char *valStart = href + 6; // past href="
                    const char *valEnd = (const char *)std::memchr(valStart, '"', (size_t)(elemEnd - valStart));
                    if (valEnd != nullptr) return copyTrimmed(valStart, valEnd, out, outSize);
                }
            }
            p = elemEnd;
        } else {
            p = lt + 1;
        }
    }
    return false;
}

bool dialBuildStopUrl(const char *appBaseUrl, const char *appName, const char *runHref, char *out,
                      size_t outSize) {
    if (runHref == nullptr || *runHref == '\0') runHref = "run";
    char appUrl[256];
    if (!dialBuildAppUrl(appBaseUrl, appName, appUrl, sizeof(appUrl))) return false;
    while (*runHref == '/') ++runHref;
    if (*runHref == '\0') return false;
    int written = std::snprintf(out, outSize, "%s/%s", appUrl, runHref);
    return written > 0 && (size_t)written < outSize;
}

bool dialResolveUrl(const char *baseUrl, const char *appUrl, char *out, size_t outSize) {
    if (appUrl == nullptr || out == nullptr || outSize == 0) return false;
    // Absolute URL: pass through.
    if (ciStartsWith(appUrl, "http://") || ciStartsWith(appUrl, "https://")) {
        size_t n = std::strlen(appUrl);
        if (n + 1 > outSize) return false;
        std::memcpy(out, appUrl, n + 1);
        return true;
    }
    if (baseUrl == nullptr) return false;
    // Extract scheme://authority from baseUrl (up to the path's first '/').
    const char *schemeEnd = std::strstr(baseUrl, "://");
    if (schemeEnd == nullptr) return false;
    const char *authStart = schemeEnd + 3;
    const char *authEnd = std::strchr(authStart, '/');
    size_t authoritySpan = authEnd ? (size_t)(authEnd - baseUrl) : std::strlen(baseUrl);
    int written = std::snprintf(
        out, outSize, "%.*s%s%s", (int)authoritySpan, baseUrl, (*appUrl == '/') ? "" : "/", appUrl
    );
    return written > 0 && (size_t)written < outSize;
}

size_t dialBuildYouTubeLaunchBody(char *out, size_t outSize, const char *videoId) {
    if (out == nullptr || outSize == 0) return 0;
    if (videoId == nullptr || *videoId == '\0') {
        out[0] = '\0';
        return 0;
    }
    int written = std::snprintf(out, outSize, "v=%s", videoId);
    if (written <= 0 || (size_t)written >= outSize) {
        out[0] = '\0';
        return 0;
    }
    return (size_t)written;
}
