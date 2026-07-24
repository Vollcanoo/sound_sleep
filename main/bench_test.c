#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "driver/usb_serial_jtag.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "pump_controller.h"

static const char *TAG = "PUMP_BENCH";
static const int kMaxDurationSec = 5;

static void print_help(void)
{
    printf("\nCommands:\n");
    printf("  left_inflate [1-5]  GPIO7 LOW, GPIO9 LOW\n");
    printf("  left_deflate [1-5]  GPIO7 HIGH, GPIO9 HIGH\n");
    printf("  left_idle            GPIO7 HIGH, GPIO9 LOW\n");
    printf("  help\n\n");
}

static int parse_duration(const char *text)
{
    int seconds = 2;
    if (text && text[0] != '\0' && sscanf(text, "%d", &seconds) != 1) {
        return -1;
    }
    return (seconds >= 1 && seconds <= kMaxDurationSec) ? seconds : -1;
}

static void execute_left_command(const char *action, int seconds)
{
    pump_command_t command = {
        .intensity = 100,
        .duration_sec = seconds,
    };
    strncpy(command.action, action, sizeof(command.action) - 1);
    strncpy(command.zone, "left", sizeof(command.zone) - 1);

    ESP_LOGW(TAG, "Executing %s for %d second(s)", action, seconds);
    pump_execute_command(&command);
    pump_controller_stop_all();
    ESP_LOGI(TAG, "Idle restored: GPIO7=HIGH GPIO9=LOW");
}

static void process_line(char *line)
{
    char *end = line + strlen(line);
    while (end > line && isspace((unsigned char)end[-1])) {
        *--end = '\0';
    }

    char *argument = line;
    while (*argument && !isspace((unsigned char)*argument)) {
        argument++;
    }
    if (*argument) {
        *argument++ = '\0';
        while (isspace((unsigned char)*argument)) {
            argument++;
        }
    }

    if (strcmp(line, "help") == 0 || line[0] == '\0') {
        print_help();
    } else if (strcmp(line, "left_idle") == 0 && argument[0] == '\0') {
        pump_controller_stop_all();
        ESP_LOGI(TAG, "Idle: GPIO7=HIGH GPIO9=LOW");
    } else if (strcmp(line, "left_inflate") == 0 || strcmp(line, "left_deflate") == 0) {
        int seconds = parse_duration(argument);
        if (seconds < 0) {
            ESP_LOGE(TAG, "Duration must be an integer from 1 to %d", kMaxDurationSec);
            return;
        }
        execute_left_command(strcmp(line, "left_inflate") == 0 ? "inflate" : "deflate",
                             seconds);
    } else {
        ESP_LOGE(TAG, "Unknown command: %s", line);
        print_help();
    }
}

static void serial_command_task(void *arg)
{
    char line[64];
    size_t length = 0;
    uint8_t byte;

    while (true) {
        int read = usb_serial_jtag_read_bytes(&byte, 1, pdMS_TO_TICKS(100));
        if (read != 1) {
            continue;
        }
        if (byte == '\r' || byte == '\n') {
            if (length > 0) {
                line[length] = '\0';
                process_line(line);
                length = 0;
            }
        } else if (byte >= 32 && byte <= 126) {
            if (length < sizeof(line) - 1) {
                line[length++] = (char)byte;
            } else {
                ESP_LOGE(TAG, "Command too long");
                length = 0;
            }
        }
    }
}

void app_main(void)
{
    ESP_LOGW(TAG, "Airbag bench-test firmware: sensor and network tasks are disabled");
    pump_controller_init();

    if (!usb_serial_jtag_is_driver_installed()) {
        usb_serial_jtag_driver_config_t config = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
        esp_err_t err = usb_serial_jtag_driver_install(&config);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "USB Serial/JTAG driver setup failed: %s", esp_err_to_name(err));
            return;
        }
    }

    print_help();
    xTaskCreate(serial_command_task, "pump_bench_serial", 4096, NULL, 5, NULL);
}
