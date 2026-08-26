#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t music_player_init(void);
esp_err_t music_player_start(void);
esp_err_t music_player_write(const uint8_t *data, size_t len);
esp_err_t music_player_finish(void);
void music_player_stop(void);

#ifdef __cplusplus
}
#endif
