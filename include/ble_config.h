#ifndef __BLE_CONFIG_H__
#define __BLE_CONFIG_H__

// BLE configuration is started only by the physical double-click configuration
// mode. Writes require an encrypted, passkey-authenticated BLE connection.
void bleConfigStart();
void bleConfigStop();
bool bleConfigTakeActivity();
bool bleConfigShouldRestart();

#endif
