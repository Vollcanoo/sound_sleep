#include <string.h>
#include <stdint.h>
#include "esp_log.h"
#include "pump_rules.h"

static const char *TAG = "PUMP_RULE";

#define LOCAL_CLEAR_FRAME_COUNT     15
#define LOCAL_INFLATE_DURATION_SEC  8
#define LOCAL_DEFLATE_DURATION_SEC  8

#define LOCAL_ZONE_LEFT   (1U << 0)
#define LOCAL_ZONE_RIGHT  (1U << 1)

typedef enum {
    SNORE_MILD,
    SNORE_MODERATE,
    SNORE_SEVERE,
    SNORE_VERY_SEVERE,
} snore_severity_t;

/* Command state only; this is not a pressure measurement. */
static uint8_t s_inflated_zones;
static uint8_t s_clear_frame_count;

static void set_string(char *destination, size_t destination_size, const char *source)
{
    strncpy(destination, source, destination_size - 1);
    destination[destination_size - 1] = '\0';
}

static void cmd_hold(pump_command_t *cmd)
{
    set_string(cmd->action, sizeof(cmd->action), "hold");
    set_string(cmd->zone, sizeof(cmd->zone), "both");
    cmd->intensity = 0;
    cmd->duration_sec = 0;
}

static void cmd_inflate(pump_command_t *cmd, const char *zone, int intensity)
{
    set_string(cmd->action, sizeof(cmd->action), "inflate");
    set_string(cmd->zone, sizeof(cmd->zone), zone);
    cmd->intensity = intensity;
    cmd->duration_sec = LOCAL_INFLATE_DURATION_SEC;
}

static void cmd_deflate(pump_command_t *cmd, const char *zone)
{
    set_string(cmd->action, sizeof(cmd->action), "deflate");
    set_string(cmd->zone, sizeof(cmd->zone), zone);
    cmd->intensity = 0;
    cmd->duration_sec = LOCAL_DEFLATE_DURATION_SEC;
}

static bool cmd_deflate_active_zones(pump_command_t *cmd)
{
    if (s_inflated_zones == 0) return false;

    if (s_inflated_zones == (LOCAL_ZONE_LEFT | LOCAL_ZONE_RIGHT)) {
        cmd_deflate(cmd, "both");
    } else if (s_inflated_zones & LOCAL_ZONE_LEFT) {
        cmd_deflate(cmd, "left");
    } else {
        cmd_deflate(cmd, "right");
    }
    return true;
}

void pump_evaluate_local_rule(const snore_features_t *feat, pump_command_t *cmd_out)
{
    cmd_hold(cmd_out);

    if (!feat->snore_detected) {
        if (s_inflated_zones != 0 && ++s_clear_frame_count >= LOCAL_CLEAR_FRAME_COUNT) {
            cmd_deflate_active_zones(cmd_out);
            s_clear_frame_count = 0;
            ESP_LOGI(TAG, "No snore for %d frames -> deflate %s",
                     LOCAL_CLEAR_FRAME_COUNT, cmd_out->zone);
        } else {
            ESP_LOGI(TAG, "No snore -> hold");
        }
        return;
    }
    s_clear_frame_count = 0;

    posture_t posture = feat->posture.posture;
    if (posture == POSTURE_MOVING || posture == POSTURE_NO_HEAD) {
        if (cmd_deflate_active_zones(cmd_out)) {
            ESP_LOGI(TAG, "%s -> deflate %s", posture_name(posture), cmd_out->zone);
        } else {
            ESP_LOGI(TAG, "%s -> hold", posture_name(posture));
        }
        return;
    }

    snore_severity_t severity;
    if (feat->max_probability > 0.9f && feat->positive_window_ratio > 0.1f) {
        severity = SNORE_VERY_SEVERE;
    } else if (feat->snore_minutes_per_hour >= 4.0f) {
        severity = SNORE_SEVERE;
    } else if (feat->snore_minutes_per_hour >= 2.0f) {
        severity = SNORE_MODERATE;
    } else {
        severity = SNORE_MILD;
    }

    if (severity == SNORE_MILD) {
        ESP_LOGI(TAG, "%s + mild snore -> hold", posture_name(posture));
        return;
    }

    switch (posture) {
    case POSTURE_SUPINE:
        if (severity == SNORE_MODERATE) cmd_inflate(cmd_out, "right", 40);
        else if (severity == SNORE_SEVERE) cmd_inflate(cmd_out, "right", 70);
        else if (severity == SNORE_VERY_SEVERE) cmd_inflate(cmd_out, "right", 80);
        break;
    case POSTURE_LEFT_SIDE:
        if (severity >= SNORE_SEVERE) cmd_inflate(cmd_out, "left", 60);
        break;
    case POSTURE_RIGHT_SIDE:
        if (severity >= SNORE_SEVERE) cmd_inflate(cmd_out, "right", 60);
        break;
    default:
        break;
    }

    if (feat->posture.confidence < 0.5f && cmd_out->intensity > 0) {
        cmd_out->intensity = (int)(cmd_out->intensity * 0.7f);
    }

    ESP_LOGI(TAG, "Decision: posture=%s confidence=%.2f snore=%.1f max=%.2f ratio=%.2f -> %s %s %d%% %ds",
             posture_name(posture), feat->posture.confidence,
             feat->snore_minutes_per_hour, feat->max_probability,
             feat->positive_window_ratio, cmd_out->action, cmd_out->zone,
             cmd_out->intensity, cmd_out->duration_sec);
}

void pump_rules_record_queued_command(const pump_command_t *cmd)
{
    if (strcmp(cmd->action, "inflate") == 0) {
        if (strcmp(cmd->zone, "left") == 0 || strcmp(cmd->zone, "both") == 0) {
            s_inflated_zones |= LOCAL_ZONE_LEFT;
        }
        if (strcmp(cmd->zone, "right") == 0 || strcmp(cmd->zone, "both") == 0) {
            s_inflated_zones |= LOCAL_ZONE_RIGHT;
        }
    } else if (strcmp(cmd->action, "deflate") == 0) {
        if (strcmp(cmd->zone, "left") == 0 || strcmp(cmd->zone, "both") == 0) {
            s_inflated_zones &= ~LOCAL_ZONE_LEFT;
        }
        if (strcmp(cmd->zone, "right") == 0 || strcmp(cmd->zone, "both") == 0) {
            s_inflated_zones &= ~LOCAL_ZONE_RIGHT;
        }
    }
}

void pump_rules_reset(void)
{
    s_inflated_zones = 0;
    s_clear_frame_count = 0;
}
