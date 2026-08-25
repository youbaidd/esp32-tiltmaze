#pragma once

#include <stdbool.h>

// Reboots the device into a different app partition of the shared
// multi-slot table (see partitions_launcher.csv) - the mechanism the
// launcher uses to switch games, and each game uses to return to it.

// ota_slot: 0..7 for ota_0..ota_7, or a negative value for "factory" (the
// launcher's own slot). Does not return on success. Returns false only if
// the requested slot doesn't exist in the partition table or the boot
// pointer couldn't be written - in which case nothing has changed and the
// caller is still running normally.
bool slot_switch_boot_into(int ota_slot);
