#include "dashboard_cache.h"

#include <FS.h>
#include <SPIFFS.h>
#include <memory>

#include "dashboard_client.h"

namespace dashboard {
namespace {

constexpr uint32_t CACHE_MAGIC = 0x4a434442; // JCDB
constexpr uint16_t CACHE_FORMAT_VERSION = 1;
constexpr const char* SLOT_A = "/dash-a.bin";
constexpr const char* SLOT_B = "/dash-b.bin";

struct CacheHeader {
    uint32_t magic;
    uint16_t formatVersion;
    uint16_t reserved;
    uint32_t sequence;
    uint32_t payloadLength;
    uint32_t checksum;
};

struct SlotData {
    bool valid = false;
    uint32_t sequence = 0;
    String json;
};

bool mounted = false;

uint32_t crc32(const uint8_t* data, size_t length) {
    uint32_t crc = 0xffffffffUL;
    for (size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ (0xedb88320UL & (0UL - (crc & 1UL)));
        }
    }
    return ~crc;
}

SlotData readSlot(const char* path) {
    SlotData slot;
    File file = SPIFFS.open(path, FILE_READ);
    if (!file) return slot;
    if (file.size() < sizeof(CacheHeader) || file.size() > sizeof(CacheHeader) + MAX_RESPONSE_BYTES) {
        file.close();
        return slot;
    }

    CacheHeader header = {};
    if (file.read(reinterpret_cast<uint8_t*>(&header), sizeof(header)) != sizeof(header)
        || header.magic != CACHE_MAGIC
        || header.formatVersion != CACHE_FORMAT_VERSION
        || header.payloadLength == 0
        || header.payloadLength > MAX_RESPONSE_BYTES
        || file.size() != sizeof(CacheHeader) + header.payloadLength) {
        file.close();
        return slot;
    }

    std::unique_ptr<char[]> buffer(new (std::nothrow) char[header.payloadLength + 1]);
    if (!buffer || file.read(reinterpret_cast<uint8_t*>(buffer.get()), header.payloadLength) != header.payloadLength) {
        file.close();
        return slot;
    }
    file.close();
    buffer[header.payloadLength] = '\0';
    if (crc32(reinterpret_cast<const uint8_t*>(buffer.get()), header.payloadLength) != header.checksum) return slot;

    Snapshot parsed;
    if (dashboard_parse_json(buffer.get(), header.payloadLength, parsed) != ClientResult::Ok) return slot;
    slot.valid = true;
    slot.sequence = header.sequence;
    slot.json = String(buffer.get(), header.payloadLength);
    return slot;
}

bool writeSlot(const char* path, const char* json, size_t length, uint32_t sequence) {
    File file = SPIFFS.open(path, FILE_WRITE);
    if (!file) return false;
    const CacheHeader header = {
        CACHE_MAGIC,
        CACHE_FORMAT_VERSION,
        0,
        sequence,
        static_cast<uint32_t>(length),
        crc32(reinterpret_cast<const uint8_t*>(json), length),
    };
    const bool ok = file.write(reinterpret_cast<const uint8_t*>(&header), sizeof(header)) == sizeof(header)
        && file.write(reinterpret_cast<const uint8_t*>(json), length) == length;
    file.flush();
    file.close();
    if (!ok) return false;
    const SlotData verification = readSlot(path);
    return verification.valid && verification.sequence == sequence;
}

} // namespace

bool dashboard_cache_begin() {
    if (mounted) return true;
    // Never auto-format here: a mount failure must not destroy the last cache.
    mounted = SPIFFS.begin(false);
    if (!mounted) Serial.println("[Dashboard Cache] SPIFFS mount failed");
    return mounted;
}

bool dashboard_cache_load(Snapshot& snapshot) {
    if (!dashboard_cache_begin()) return false;
    const SlotData a = readSlot(SLOT_A);
    const SlotData b = readSlot(SLOT_B);
    const SlotData* selected = nullptr;
    if (a.valid && b.valid) selected = static_cast<int32_t>(a.sequence - b.sequence) > 0 ? &a : &b;
    else if (a.valid) selected = &a;
    else if (b.valid) selected = &b;
    if (!selected) return false;
    return dashboard_parse_json(selected->json.c_str(), selected->json.length(), snapshot) == ClientResult::Ok;
}

bool dashboard_cache_store(const char* json, size_t length) {
    if (!dashboard_cache_begin() || json == nullptr || length == 0 || length > MAX_RESPONSE_BYTES) return false;
    Snapshot parsed;
    if (dashboard_parse_json(json, length, parsed) != ClientResult::Ok) return false;

    const SlotData a = readSlot(SLOT_A);
    const SlotData b = readSlot(SLOT_B);
    uint32_t sequence = 1;
    const char* target = SLOT_A;
    if (a.valid || b.valid) {
        const uint32_t newest = !a.valid ? b.sequence
            : !b.valid ? a.sequence
            : static_cast<int32_t>(a.sequence - b.sequence) > 0 ? a.sequence : b.sequence;
        sequence = newest + 1;
        // Always overwrite the older/invalid slot, preserving the newest slot
        // until the replacement has been closed and verified.
        if (!a.valid) target = SLOT_A;
        else if (!b.valid) target = SLOT_B;
        else target = static_cast<int32_t>(a.sequence - b.sequence) > 0 ? SLOT_B : SLOT_A;
    }
    return writeSlot(target, json, length, sequence);
}

} // namespace dashboard
