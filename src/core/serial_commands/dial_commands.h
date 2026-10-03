#pragma once

#include <SimpleCLI.h>

// Registers the `dial` serial commands (scan / launch / stop). No-op unless
// -DBRUCE_DIAL is set, so cli.cpp can call it unconditionally.
void createDialCommands(SimpleCLI *cli);
