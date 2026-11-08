#ifndef __QWEATHER_JWT_H__
#define __QWEATHER_JWT_H__

#include <Arduino.h>

// Build a short-lived QWeather JWT signed with an Ed25519 PKCS#8 private key.
// The private key may be a PEM string, PKCS#8 DER in Base64, or a 32-byte seed
// in Base64. Returns false without exposing credential material in logs.
bool qweatherBuildJwt(const String& credentialId,
                      const String& projectId,
                      const String& privateKey,
                      time_t now,
                      String& token);

#endif
