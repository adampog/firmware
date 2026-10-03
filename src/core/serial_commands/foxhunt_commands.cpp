#include "foxhunt_commands.h"

#include <globals.h>

#ifdef BRUCE_FOXHUNT
#include "core/net_utils.h" // stringToMAC
#include "modules/foxhunt/fox_hunt.h"

// `foxhunt hunt <aa:bb:cc:dd:ee:ff> [seconds]` — lock onto a target MAC and
// stream its RSSI + warmer/colder proximity to Serial.
static uint32_t foxHuntHuntCallback(cmd *c) {
    Command cmd(c);
    String macStr = cmd.getArgument(0).getValue();
    macStr.trim();
    if (macStr.length() == 0) {
        Serial.println("usage: foxhunt hunt <aa:bb:cc:dd:ee:ff> [seconds]");
        return false;
    }

    uint8_t target[6] = {0};
    stringToMAC(std::string(macStr.c_str()), target);

    uint32_t seconds = 60;
    if (cmd.countArgs() > 1) {
        String secStr = cmd.getArgument(1).getValue();
        secStr.trim();
        if (secStr.length()) seconds = static_cast<uint32_t>(secStr.toInt());
    }
    if (seconds == 0 || seconds > 600) seconds = 60; // clamp to a sane range

    foxHuntRun(target, seconds * 1000);
    return true;
}
#endif // BRUCE_FOXHUNT

void createFoxHuntCommands(SimpleCLI *cli) {
#ifdef BRUCE_FOXHUNT
    Command foxhunt = cli->addCompositeCmd("foxhunt");
    foxhunt.addBoundlessCommand("hunt", foxHuntHuntCallback);
#else
    (void)cli;
#endif
}
