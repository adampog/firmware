#include "dial_commands.h"

#include <globals.h>

#ifdef BRUCE_DIAL
#include "modules/dial/dial_cast.h"

// `dial scan [seconds]` — discover DIAL devices and list them (also resolves
// each device's Application-URL + YouTube state).
static uint32_t dialScanCallback(cmd *c) {
    Command cmd(c);
    uint32_t seconds = 0;
    if (cmd.countArgs() > 0) {
        String s = cmd.getArgument(0).getValue();
        s.trim();
        if (s.length()) seconds = static_cast<uint32_t>(s.toInt());
    }
    dialScanRun(seconds);
    return true;
}

// `dial launch <idx> [app] [videoId]` — launch an app (default YouTube) on a
// device from the last scan; optional YouTube video id.
static uint32_t dialLaunchCallback(cmd *c) {
    Command cmd(c);
    if (cmd.countArgs() < 1) {
        Serial.println("usage: dial launch <idx> [app] [videoId]");
        return false;
    }
    int idx = static_cast<int>(cmd.getArgument(0).getValue().toInt());
    String app = cmd.countArgs() > 1 ? cmd.getArgument(1).getValue() : String("");
    String vid = cmd.countArgs() > 2 ? cmd.getArgument(2).getValue() : String("");
    app.trim();
    vid.trim();
    dialLaunchIndex(idx, app.length() ? app.c_str() : nullptr, vid.length() ? vid.c_str() : nullptr);
    return true;
}

// `dial stop <idx> [app]` — stop an app (default YouTube) on a scanned device.
static uint32_t dialStopCallback(cmd *c) {
    Command cmd(c);
    if (cmd.countArgs() < 1) {
        Serial.println("usage: dial stop <idx> [app]");
        return false;
    }
    int idx = static_cast<int>(cmd.getArgument(0).getValue().toInt());
    String app = cmd.countArgs() > 1 ? cmd.getArgument(1).getValue() : String("");
    app.trim();
    dialStopIndex(idx, app.length() ? app.c_str() : nullptr);
    return true;
}
#endif // BRUCE_DIAL

void createDialCommands(SimpleCLI *cli) {
#ifdef BRUCE_DIAL
    Command dial = cli->addCompositeCmd("dial");
    dial.addBoundlessCommand("scan", dialScanCallback);
    dial.addBoundlessCommand("launch", dialLaunchCallback);
    dial.addBoundlessCommand("stop", dialStopCallback);
#else
    (void)cli;
#endif
}
