#ifndef LLM_PERIODIC_H
#define LLM_PERIODIC_H

#include <stdbool.h>
#include "snore_feature.h"
#include "cloud_llm_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#ifdef __cplusplus
extern "C" {
#endif

void llm_periodic_init(QueueHandle_t cmd_queue);
void llm_periodic_on_frame(const snore_features_t *feat);
bool llm_periodic_override_active(void);
void llm_periodic_reset(void);

#ifdef __cplusplus
}
#endif

#endif /* LLM_PERIODIC_H */
