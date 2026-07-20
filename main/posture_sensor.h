#ifndef POSTURE_SENSOR_H
#define POSTURE_SENSOR_H

#include <stdbool.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "snore_feature.h"

/*
 * Initializes GPIO4/5/6 ADC channels and starts the FSR sampling task.
 * Each FSR circuit is 3.3V -> FSR -> ADC pin -> 2k ohm -> GND.
 */
esp_err_t posture_sensor_start(void);

/* Waits for the next 1 Hz posture result. */
bool posture_sensor_receive(posture_data_t *result, TickType_t timeout);

/* Repeats unloaded-baseline calibration on the sampling task. */
void posture_sensor_request_calibration(void);

#endif /* POSTURE_SENSOR_H */
