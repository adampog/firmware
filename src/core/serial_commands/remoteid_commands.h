#ifndef __SERIAL_REMOTEID_CMD_H__
#define __SERIAL_REMOTEID_CMD_H__

#include <SimpleCLI.h>

// Registers the `remoteid` serial command. No-op unless -DBRUCE_REMOTEID is set,
// so this is always safe to call from cli.cpp.
void createRemoteIdCommands(SimpleCLI *cli);

#endif
