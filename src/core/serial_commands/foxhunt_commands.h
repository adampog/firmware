#ifndef __SERIAL_FOXHUNT_CMD_H__
#define __SERIAL_FOXHUNT_CMD_H__

#include <SimpleCLI.h>

// Registers the `foxhunt` serial command. No-op unless -DBRUCE_FOXHUNT is set,
// so this is always safe to call from cli.cpp.
void createFoxHuntCommands(SimpleCLI *cli);

#endif
