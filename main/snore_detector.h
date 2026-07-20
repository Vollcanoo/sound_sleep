#ifndef SNORE_DETECTOR_H
#define SNORE_DETECTOR_H

#include <stdbool.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"

typedef struct {
    float probability;
    float window_seconds;
    bool detected;
} snore_reading_t;

/* Starts the INMP441 capture and ESP-DL inference task when a model is present. */
esp_err_t snore_detector_start(void);

/* Receives the latest completed inference window. */
bool snore_detector_receive(snore_reading_t *reading, TickType_t timeout);

/* Returns true only when the INT8 ESP-DL model was embedded at build time. */
bool snore_detector_is_available(void);

#endif /* SNORE_DETECTOR_H */
