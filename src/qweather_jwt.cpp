#include "qweather_jwt.h"

#include <Ed25519.h>
#include <mbedtls/base64.h>

namespace {

bool isValidIdentifier(const String& value) {
    if (value.isEmpty() || value.length() > 63) return false;
    for (size_t i = 0; i < value.length(); ++i) {
        const char c = value.charAt(i);
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '-' || c == '_')) {
            return false;
        }
    }
    return true;
}

String base64UrlEncode(const uint8_t* data, size_t length) {
    const size_t capacity = 4 * ((length + 2) / 3) + 1;
    uint8_t* encoded = static_cast<uint8_t*>(malloc(capacity));
    if (encoded == nullptr) return "";

    size_t encodedLength = 0;
    const int result = mbedtls_base64_encode(encoded, capacity, &encodedLength, data, length);
    if (result != 0) {
        free(encoded);
        return "";
    }

    String output;
    output.reserve(encodedLength);
    for (size_t i = 0; i < encodedLength; ++i) {
        const char c = static_cast<char>(encoded[i]);
        if (c == '+') output += '-';
        else if (c == '/') output += '_';
        else if (c != '=') output += c;
    }
    free(encoded);
    return output;
}

bool decodePrivateSeed(String value, uint8_t seed[32]) {
    value.replace("-----BEGIN PRIVATE KEY-----", "");
    value.replace("-----END PRIVATE KEY-----", "");
    value.replace("\r", "");
    value.replace("\n", "");
    value.replace(" ", "");
    value.replace("\t", "");
    value.trim();
    if (value.isEmpty()) return false;

    const size_t capacity = (value.length() * 3) / 4 + 4;
    uint8_t* decoded = static_cast<uint8_t*>(malloc(capacity));
    if (decoded == nullptr) return false;

    size_t decodedLength = 0;
    const int result = mbedtls_base64_decode(
        decoded, capacity, &decodedLength,
        reinterpret_cast<const uint8_t*>(value.c_str()), value.length());
    if (result != 0) {
        free(decoded);
        return false;
    }

    bool valid = false;
    if (decodedLength == 32) {
        memcpy(seed, decoded, 32);
        valid = true;
    } else {
        // RFC 8410 Ed25519 PKCS#8: SEQUENCE { version, algorithm 1.3.101.112,
        // privateKey OCTET STRING containing a nested 32-byte OCTET STRING }.
        static const uint8_t prefix[] = {
            0x30, 0x2e, 0x02, 0x01, 0x00, 0x30, 0x05, 0x06,
            0x03, 0x2b, 0x65, 0x70, 0x04, 0x22, 0x04, 0x20
        };
        if (decodedLength == sizeof(prefix) + 32 &&
            memcmp(decoded, prefix, sizeof(prefix)) == 0) {
            memcpy(seed, decoded + sizeof(prefix), 32);
            valid = true;
        }
    }

    memset(decoded, 0, capacity);
    free(decoded);
    return valid;
}

} // namespace

bool qweatherBuildJwt(const String& credentialId,
                      const String& projectId,
                      const String& privateKey,
                      time_t now,
                      String& token) {
    token = "";
    if (!isValidIdentifier(credentialId) || !isValidIdentifier(projectId) ||
        privateKey.isEmpty() || now < 1609459200) {
        return false;
    }

    uint8_t privateSeed[32];
    if (!decodePrivateSeed(privateKey, privateSeed)) return false;

    const int64_t issuedAt = static_cast<int64_t>(now) - 30;
    const int64_t expiresAt = issuedAt + 900;
    const String header = String("{\"alg\":\"EdDSA\",\"kid\":\"") + credentialId + "\"}";
    const String payload = String("{\"sub\":\"") + projectId +
        "\",\"iat\":" + String(issuedAt) + ",\"exp\":" + String(expiresAt) + "}";

    const String headerEncoded = base64UrlEncode(
        reinterpret_cast<const uint8_t*>(header.c_str()), header.length());
    const String payloadEncoded = base64UrlEncode(
        reinterpret_cast<const uint8_t*>(payload.c_str()), payload.length());
    if (headerEncoded.isEmpty() || payloadEncoded.isEmpty()) {
        memset(privateSeed, 0, sizeof(privateSeed));
        return false;
    }

    const String signingInput = headerEncoded + "." + payloadEncoded;
    uint8_t publicKey[32];
    uint8_t signature[64];
    Ed25519::derivePublicKey(publicKey, privateSeed);
    Ed25519::sign(signature, privateSeed, publicKey, signingInput.c_str(), signingInput.length());
    memset(privateSeed, 0, sizeof(privateSeed));
    memset(publicKey, 0, sizeof(publicKey));

    const String signatureEncoded = base64UrlEncode(signature, sizeof(signature));
    memset(signature, 0, sizeof(signature));
    if (signatureEncoded.isEmpty()) return false;

    token = signingInput + "." + signatureEncoded;
    return true;
}
