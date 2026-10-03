#include "remoteid_commands.h"

#include <globals.h>

#ifdef BRUCE_REMOTEID
#include "modules/remoteid/remote_id_scan.h"

// `remoteid scan [seconds]` — passive OpenDroneID WiFi scan; prints detections.
static uint32_t remoteIdScanCallback(cmd *c) {
    Command cmd(c);
    Argument arg = cmd.getArgument(0);
    String args = arg.getValue();
    args.trim();

    uint32_t seconds = args.length() ? static_cast<uint32_t>(args.toInt()) : 30;
    if (seconds == 0 || seconds > 600) seconds = 30; // clamp to a sane range

    remoteIdScanRun(seconds * 1000);
    return true;
}
#endif // BRUCE_REMOTEID

void createRemoteIdCommands(SimpleCLI *cli) {
#ifdef BRUCE_REMOTEID
    Command remoteid = cli->addCompositeCmd("remoteid");
    remoteid.addSingleArgumentCommand("scan", remoteIdScanCallback);
#else
    (void)cli;
#endif
}
