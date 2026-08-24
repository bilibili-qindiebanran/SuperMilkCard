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
    int stable;           /* 稳定电平（0=按下，1=释放） */
    uint32_t stable_cnt;  /* 当前电平持续周期数 */
    uint32_t press_ms;    /* 本次按下累计毫秒（仅按下时累加） */
    bool short_evt;       /* 短按事件待消费 */
    bool long_evt;        /* 长按事件待消费 */
    bool long_fired;      /* 本次按下是否已触发过长按 */
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

    ESP_LOGI(TAG, "keys ready: BACK=GPIO%d OK=GPIO%d (10k ext pullup, press=low, long=%dms)",
             BOARD_KEY_BACK_GPIO, BOARD_KEY_OK_GPIO, BOARD_KEY_LONG_PRESS_MS);
    return ESP_OK;
}

/* 按键状态机：消抖 + 短按（稳定释放 → 短按）+ 长按（按下持续 ≥ 阈值） */
static void scan_key(key_state_t *ks, int raw)
{
    if (raw != ks->raw)
    {
        /* 电平变化：重置稳定计数与按下计时 */
        ks->raw = raw;
        ks->stable_cnt = 0;
        ks->press_ms = 0;
        ks->long_fired = false;
        return;
    }

    /* 电平未变：累计稳定周期 */
    ks->stable_cnt++;
    if (ks->stable_cnt < KEY_DEBOUNCE_MS / KEY_SCAN_PERIOD_MS)
    {
        return;
    }

    if (raw == 0)
    {
        /* 稳定按下：累计按下时长，超过阈值触发一次长按 */
        ks->press_ms += KEY_SCAN_PERIOD_MS;
        if (!ks->long_fired && ks->press_ms >= BOARD_KEY_LONG_PRESS_MS)
        {
            ks->long_evt = true;
            ks->long_fired = true; /* 长按只触发一次 */
        }
    }
    else
    {
        /* 稳定释放：若未触发过长按 → 短按事件 */
        if (ks->stable == 0)
        {
            if (!ks->long_fired)
            {
                ks->short_evt = true;
            }
            ks->press_ms = 0;
        }
        ks->stable = raw;
    }
}

board_key_event_t board_keys_scan(void)
{
    board_key_event_t evt = BOARD_KEY_NONE;

    int raw_back = gpio_get_level(BOARD_KEY_BACK_GPIO);
    scan_key(&s_back, raw_back);
    if (s_back.long_evt)
    {
        s_back.long_evt = false;
        evt = BOARD_KEY_BACK_LONG;
    }
    else if (s_back.short_evt)
    {
        s_back.short_evt = false;
        evt = BOARD_KEY_BACK;
    }

    if (evt == BOARD_KEY_NONE)
    {
        int raw_ok = gpio_get_level(BOARD_KEY_OK_GPIO);
        scan_key(&s_ok, raw_ok);
        if (s_ok.long_evt)
        {
            s_ok.long_evt = false;
            evt = BOARD_KEY_OK_LONG;
        }
        else if (s_ok.short_evt)
        {
            s_ok.short_evt = false;
            evt = BOARD_KEY_OK;
        }
    }

    return evt;
}
