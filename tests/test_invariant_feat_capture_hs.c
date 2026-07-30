#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "capture_buffer.h"

#define SLOT_DATA_SIZE 256U

static void test_valid_payloads_are_copied(void) {
    uint8_t payload[SLOT_DATA_SIZE];
    uint8_t destination[SLOT_DATA_SIZE];
    uint16_t stored_length = 0;

    for(size_t i = 0; i < sizeof(payload); ++i) payload[i] = (uint8_t)i;
    memset(destination, 0, sizeof(destination));

    assert(capture_buffer_store(
        destination, sizeof(destination), &stored_length, payload, sizeof(payload)));
    assert(stored_length == SLOT_DATA_SIZE);
    assert(memcmp(destination, payload, sizeof(payload)) == 0);
}

static void test_oversized_payload_is_rejected_without_writing(void) {
    uint8_t payload[SLOT_DATA_SIZE + 1U];
    uint8_t destination[SLOT_DATA_SIZE];
    uint8_t original[SLOT_DATA_SIZE];
    uint16_t stored_length = 17;

    memset(payload, 0xA5, sizeof(payload));
    memset(destination, 0x5A, sizeof(destination));
    memcpy(original, destination, sizeof(original));

    assert(!capture_buffer_store(
        destination, sizeof(destination), &stored_length, payload, sizeof(payload)));
    assert(stored_length == 17);
    assert(memcmp(destination, original, sizeof(destination)) == 0);
}

static void test_invalid_inputs_are_rejected(void) {
    uint8_t payload[1] = {0x42};
    uint8_t destination[1] = {0};
    uint16_t stored_length = 9;

    assert(!capture_buffer_store(
        destination, sizeof(destination), &stored_length, payload, 0));
    assert(!capture_buffer_store(
        NULL, sizeof(destination), &stored_length, payload, sizeof(payload)));
    assert(!capture_buffer_store(
        destination, sizeof(destination), NULL, payload, sizeof(payload)));
    assert(!capture_buffer_store(
        destination, sizeof(destination), &stored_length, NULL, sizeof(payload)));
    assert(stored_length == 9);
    assert(destination[0] == 0);
}

int main(void) {
    test_valid_payloads_are_copied();
    test_oversized_payload_is_rejected_without_writing();
    test_invalid_inputs_are_rejected();
    return 0;
}
