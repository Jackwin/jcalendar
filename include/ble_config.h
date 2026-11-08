#ifndef __BLE_CONFIG_H__
#define __BLE_CONFIG_H__

#include <stdint.h>

// BLE configuration is started by the button's configuration gesture.
// Writes require an encrypted, passkey-authenticated BLE connection.
// Returns the six-digit pairing PIN, or 0 if advertising could not start.
uint32_t bleConfigStart();
// Process a completed request on the Arduino loop task, not the NimBLE host task.
void bleConfigPoll();
void bleConfigStop();
bool bleConfigTakeActivity();
bool bleConfigShouldRestart();

#endif
