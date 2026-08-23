#include "ble_config.h"

#include <ArduinoJson.h>
#include <NimBLEDevice.h>
#include <Preferences.h>
#include <WiFi.h>

#include "_preference.h"

namespace {
constexpr char DEVICE_NAME[] = "J-Calendar Config";
constexpr char SERVICE_UUID[] = "1d8b0c01-95b2-4fe8-b74d-8db8f9937e01";
constexpr char CONFIG_UUID[] = "1d8b0c02-95b2-4fe8-b74d-8db8f9937e01";
constexpr char VERSION_UUID[] = "1d8b0c03-95b2-4fe8-b74d-8db8f9937e01";
constexpr uint8_t FRAME_SET = 0x01, FRAME_NOTIFY = 0x02, FRAME_START = 0x01, FRAME_END = 0x02;
constexpr size_t FRAME_MAX = 180;

NimBLECharacteristic* configCharacteristic = nullptr;
String received;
bool active = false, activity = false, restart = false;
uint32_t passkey = 0;

String normalizedHost(String host) {
    host.trim(); host.toLowerCase();
    if (host.startsWith("https://")) host.remove(0, 8);
    if (host.startsWith("http://")) host.remove(0, 7);
    const int slash = host.indexOf('/');
    if (slash >= 0) host.remove(slash);
    while (host.endsWith(".")) host.remove(host.length() - 1);
    return host;
}

bool validHost(const String& host) {
    if (host.isEmpty() || host.length() > 63 || !host.endsWith(".qweatherapi.com")) return false;
    for (size_t i = 0; i < host.length(); ++i) {
        const char c = host.charAt(i);
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '-')) return false;
    }
    return true;
}

bool validIdentifier(const String& value) {
    if (value.isEmpty() || value.length() > 63) return false;
    for (size_t i = 0; i < value.length(); ++i) {
        const char c = value.charAt(i);
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '-' || c == '_')) return false;
    }
    return true;
}

void reply(const JsonDocument& doc) {
    String json; serializeJson(doc, json);
    for (size_t pos = 0; pos < json.length(); pos += FRAME_MAX) {
        const size_t length = min(FRAME_MAX, json.length() - pos);
        String frame; frame.reserve(length + 2);
        frame += static_cast<char>(FRAME_NOTIFY);
        frame += static_cast<char>((pos == 0 ? FRAME_START : 0) | (pos + length == json.length() ? FRAME_END : 0));
        frame += json.substring(pos, pos + length);
        configCharacteristic->setValue(frame.c_str());
        configCharacteristic->notify();
        delay(15);
    }
}

void failure(const char* message) {
    JsonDocument doc; doc["ok"] = false; doc["error"] = message; reply(doc);
}

void readConfig() {
    Preferences pref; pref.begin(PREF_NAMESPACE, true);
    JsonDocument doc;
    doc["ok"] = true; doc["op"] = "config";
    doc["host"] = pref.getString(PREF_QWEATHER_HOST, "");
    doc["location"] = pref.getString(PREF_QWEATHER_LOC, "");
    doc["type"] = pref.getString(PREF_QWEATHER_TYPE, "0").toInt();
    doc["kid"] = pref.getString(PREF_QW_JWT_KID, "");
    doc["sub"] = pref.getString(PREF_QW_JWT_SUB, "");
    doc["hasPrivateKey"] = !pref.getString(PREF_QW_JWT_PRIV, "").isEmpty();
    doc["wifiSsid"] = WiFi.SSID(); doc["wifiConnected"] = WiFi.isConnected();
    pref.end(); reply(doc);
}

void saveConfig(JsonDocument& request) {
    Preferences pref; pref.begin(PREF_NAMESPACE, false);
    String host = pref.getString(PREF_QWEATHER_HOST, "");
    String location = pref.getString(PREF_QWEATHER_LOC, "");
    String kid = pref.getString(PREF_QW_JWT_KID, "");
    String sub = pref.getString(PREF_QW_JWT_SUB, "");
    String privateKey = pref.getString(PREF_QW_JWT_PRIV, "");
    String type = pref.getString(PREF_QWEATHER_TYPE, "0");
    const bool weatherChange = request["host"].is<const char*>() || request["location"].is<const char*>() || request["kid"].is<const char*>() || request["sub"].is<const char*>() || request["privateKey"].is<const char*>();
    if (request["host"].is<const char*>()) host = normalizedHost(request["host"].as<String>());
    if (request["location"].is<const char*>()) { location = request["location"].as<String>(); location.trim(); }
    if (request["kid"].is<const char*>()) { kid = request["kid"].as<String>(); kid.trim(); }
    if (request["sub"].is<const char*>()) { sub = request["sub"].as<String>(); sub.trim(); }
    if (request["privateKey"].is<const char*>()) { privateKey = request["privateKey"].as<String>(); privateKey.trim(); }
    if (request["type"].is<int>()) type = request["type"].as<int>() == 1 ? "1" : "0";
    if (weatherChange && (!validHost(host) || location.isEmpty() ||
        !validIdentifier(kid) || !validIdentifier(sub) || privateKey.isEmpty())) {
        pref.end(); failure("Weather settings are incomplete or invalid"); return;
    }
    if (weatherChange) {
        pref.putString(PREF_QWEATHER_HOST, host.substring(0, 63));
        pref.putString(PREF_QWEATHER_LOC, location.substring(0, 63));
        pref.putString(PREF_QW_JWT_KID, kid.substring(0, 63));
        pref.putString(PREF_QW_JWT_SUB, sub.substring(0, 63));
        pref.putString(PREF_QW_JWT_PRIV, privateKey.substring(0, 255));
        pref.putString(PREF_QWEATHER_TYPE, type);
        pref.remove("QWEATHER_KEY"); // Remove the legacy API-key setting.
    }
    pref.end();
    if (request["wifiSsid"].is<const char*>()) {
        const String ssid = request["wifiSsid"].as<String>();
        const String password = request["wifiPassword"].is<const char*>() ? request["wifiPassword"].as<String>() : "";
        if (ssid.isEmpty() || ssid.length() > 32 || password.length() > 63) { failure("Wi-Fi SSID or password is invalid"); return; }
        WiFi.persistent(true); WiFi.begin(ssid.c_str(), password.c_str());
    }
    JsonDocument doc; doc["ok"] = true; doc["op"] = "saved"; reply(doc); restart = true;
}

void process() {
    JsonDocument request; const DeserializationError error = deserializeJson(request, received); received = "";
    if (error) { failure("Invalid JSON request"); return; }
    const String op = request["op"] | "";
    if (op == "get") readConfig(); else if (op == "set") saveConfig(request); else failure("Unsupported operation");
}

class ConfigCallbacks : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* characteristic, NimBLEConnInfo&) override {
        const std::string value = characteristic->getValue();
        if (value.size() < 2 || static_cast<uint8_t>(value[0]) != FRAME_SET) { failure("Invalid frame"); return; }
        activity = true; const uint8_t flags = static_cast<uint8_t>(value[1]);
        if (flags & FRAME_START) received = "";
        if (received.length() + value.size() - 2 > 1024) { received = ""; failure("Request is too large"); return; }
        received.concat(value.data() + 2, value.size() - 2);
        if (flags & FRAME_END) process();
    }
};

class ServerCallbacks : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer*, NimBLEConnInfo& connection) override {
        activity = true;
        // Trigger passkey pairing immediately so Web Bluetooth does not first
        // encounter an opaque encrypted-characteristic permission error.
        NimBLEDevice::startSecurity(connection.getConnHandle());
    }
    void onDisconnect(NimBLEServer*, NimBLEConnInfo&, int) override { activity = true; NimBLEDevice::startAdvertising(); }
    uint32_t onPassKeyDisplay() override { return passkey; }
    void onAuthenticationComplete(NimBLEConnInfo& connection) override {
        if (!connection.isEncrypted()) {
            NimBLEDevice::getServer()->disconnect(connection.getConnHandle());
            Serial.println("BLE pairing failed");
            return;
        }
        Serial.println("BLE pairing completed");
    }
};
} // namespace

void bleConfigStart() {
    if (active) return;
    passkey = 100000 + (esp_random() % 900000);
    NimBLEDevice::init(DEVICE_NAME);
    NimBLEDevice::setMTU(247);
    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_DISPLAY_ONLY);
    NimBLEDevice::setSecurityAuth(true, true, true);
    NimBLEDevice::setSecurityPasskey(passkey);
    Serial.printf("BLE config advertising as %s; pairing PIN: %06u\n", DEVICE_NAME, passkey);
    NimBLEServer* server = NimBLEDevice::createServer(); server->setCallbacks(new ServerCallbacks());
    NimBLEService* service = server->createService(SERVICE_UUID);
    configCharacteristic = service->createCharacteristic(CONFIG_UUID, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::NOTIFY | NIMBLE_PROPERTY::WRITE_ENC);
    configCharacteristic->setCallbacks(new ConfigCallbacks());
    NimBLECharacteristic* version = service->createCharacteristic(VERSION_UUID, NIMBLE_PROPERTY::READ);
    version->setValue("1");
    NimBLEAdvertising* advertising = NimBLEDevice::getAdvertising();
    advertising->enableScanResponse(true);
    advertising->setName(DEVICE_NAME);
    advertising->addServiceUUID(SERVICE_UUID);
    if (!advertising->start()) {
        Serial.println("BLE config advertising failed to start");
        NimBLEDevice::deinit(true);
        configCharacteristic = nullptr;
        return;
    }
    active = true; activity = true;
}

void bleConfigStop() {
    if (!active) return;
    NimBLEDevice::stopAdvertising(); NimBLEDevice::deinit(true); configCharacteristic = nullptr; received = ""; active = false;
}
bool bleConfigTakeActivity() { const bool result = activity; activity = false; return result; }
bool bleConfigShouldRestart() { return restart; }
