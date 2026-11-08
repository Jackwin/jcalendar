#include "_sntp.h"

#include <Arduino.h>
#include <WiFi.h>
#include "esp_sntp.h"

namespace {
TaskHandle_t handler = nullptr;
volatile int status = SYNC_STATUS_IDLE;

void sntpTask(void*) {
    setenv("TZ", "CST-8", 1);
    tzset();
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, "ntp.aliyun.com");
    sntp_setservername(1, "pool.ntp.org");
    sntp_set_sync_status(SNTP_SYNC_STATUS_RESET);
    sntp_init();

    const uint32_t started = millis();
    bool synchronized = false;
    while (millis() - started < SNTP_SYNC_TIMEOUT_MS && WiFi.isConnected()) {
        // Reading COMPLETED resets the SDK status. Preserve that result rather
        // than reading it again when deciding whether synchronization succeeded.
        if (sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED) {
            synchronized = true;
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    status = synchronized ? SYNC_STATUS_OK : SYNC_STATUS_NOK;
    handler = nullptr;
    vTaskDelete(nullptr);
}
} // namespace

int _sntp_status() {
    return status;
}

void _sntp_exec(int initialStatus) {
    status = initialStatus;
    if (status > 0) return;
    if (!WiFi.isConnected()) {
        status = SYNC_STATUS_NOK;
        return;
    }
    if (handler != nullptr) return;
    status = SYNC_STATUS_IN_PROGRESS;
    if (xTaskCreate(sntpTask, "SNTP", 4096, nullptr, 4, &handler) != pdPASS) {
        handler = nullptr;
        status = SYNC_STATUS_NOK;
    }
}

void _sntp_stop() {
    sntp_stop();
    if (handler != nullptr) {
        vTaskDelete(handler);
        handler = nullptr;
    }
    if (status == SYNC_STATUS_IN_PROGRESS) status = SYNC_STATUS_NOK;
}
