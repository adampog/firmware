#include "pinescan_commands.h"

#include <globals.h>

#ifdef BRUCE_PINESCAN
#include "modules/pinescan/pine_scan.h"

// `pinescan scan [seconds]` — passive WiFi Pineapple / evil-twin scan; prints hits.
static uint32_t pineScanScanCallback(cmd *c) {
    Command cmd(c);
    Argument arg = cmd.getArgument(0);
    String args = arg.getValue();
    args.trim();

    uint32_t seconds = args.length() ? static_cast<uint32_t>(args.toInt()) : 30;
    if (seconds == 0 || seconds > 600) seconds = 30; // clamp to a sane range

    pineScanRun(seconds * 1000);
    return true;
}
#endif // BRUCE_PINESCAN

void createPineScanCommands(SimpleCLI *cli) {
#ifdef BRUCE_PINESCAN
    Command pinescan = cli->addCompositeCmd("pinescan");
    pinescan.addSingleArgumentCommand("scan", pineScanScanCallback);
#else
    (void)cli;
#endif
}
