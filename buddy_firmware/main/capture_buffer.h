#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* Copy one untrusted capture into a fixed-size slot.
 *
 * The length is committed only after all inputs and bounds have been checked,
 * so a rejected packet cannot make the consumer read stale or oversized data.
 * Kept header-only so the ESP32 capture path and host test exercise the same
 * implementation without pulling ESP-IDF into the host test.
 */
static inline bool capture_buffer_store(
    uint8_t* destination,
    size_t capacity,
    uint16_t* stored_length,
    const uint8_t* payload,
    uint16_t payload_length) {
    if(destination == NULL || stored_length == NULL || payload == NULL) return false;
    if(payload_length == 0 || payload_length > capacity) return false;

    memcpy(destination, payload, payload_length);
    *stored_length = payload_length;
    return true;
}
