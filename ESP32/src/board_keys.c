/**
 * @file board_keys.c
 * @brief 实体按键模块实现
 */

#include "board_keys.h"

#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "board_keys";

/* 消抖参数 */
#define KEY_DEBOUNCE_MS 30    /* 消抖窗口 */
#define KEY_SCAN_PERIOD_MS 10 /* 轮询周期（调用方保证） */

typedef struct {
    int raw;              /* 当前原始电平 */
    int stable;           /* 稳定电平 */
    uint32_t stable_cnt;  /* 当前电平持续周期数 */
    bool short_evt;       /* 短按事件待消费 */
} key_state_t;

static key_state_t s_back;
static key_state_t s_ok;

esp_err_t board_keys_init(void)
{
    /* 外部已有 10k 上拉，仅配输入模式（不上拉不下拉，避免干扰外部上拉） */
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << BOARD_KEY_BACK_GPIO) | (1ULL << BOARD_KEY_OK_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);

    s_back.raw = s_back.stable = gpio_get_level(BOARD_KEY_BACK_GPIO);
    s_ok.raw = s_ok.stable = gpio_get_level(BOARD_KEY_OK_GPIO);

    ESP_LOGI(TAG, "keys ready: BACK=GPIO%d OK=GPIO%d (10k ext pullup, press=low)",
             BOARD_KEY_BACK_GPIO, BOARD_KEY_OK_GPIO);
    return ESP_OK;
}

/* 按键状态机：消抖 + 短按（稳定按下后释放 → 短按事件） */
static void scan_key(key_state_t *ks, int raw)
{
    if (raw != ks->raw)
    {
        /* 电平变化：重置稳定计数 */
        ks->raw = raw;
        ks->stable_cnt = 0;
        return;
    }

    /* 电平未变：累计稳定周期 */
    ks->stable_cnt++;
    if (ks->stable_cnt >= KEY_DEBOUNCE_MS / KEY_SCAN_PERIOD_MS)
    {
        if (raw == 1 && ks->stable == 0)
        {
            /* 稳定从按下释放 → 短按事件 */
            ks->short_evt = true;
        }
        ks->stable = raw;
    }
}

board_key_event_t board_keys_scan(void)
{
    board_key_event_t evt = BOARD_KEY_NONE;

    int raw_back = gpio_get_level(BOARD_KEY_BACK_GPIO);
    scan_key(&s_back, raw_back);
    if (s_back.short_evt)
    {
        s_back.short_evt = false;
        evt = BOARD_KEY_BACK;
    }

    if (evt == BOARD_KEY_NONE)
    {
        int raw_ok = gpio_get_level(BOARD_KEY_OK_GPIO);
        scan_key(&s_ok, raw_ok);
        if (s_ok.short_evt)
        {
            s_ok.short_evt = false;
            evt = BOARD_KEY_OK;
        }
    }

    return evt;
}
