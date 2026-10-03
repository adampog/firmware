#ifndef __SERIAL_PINESCAN_CMD_H__
#define __SERIAL_PINESCAN_CMD_H__

#include <SimpleCLI.h>

// Registers the `pinescan` serial command. No-op unless -DBRUCE_PINESCAN is set,
// so this is always safe to call from cli.cpp.
void createPineScanCommands(SimpleCLI *cli);

#endif
