/*
 * K:04 Mini onboard status LED widget.
 *
 * Drives the single PWM LED (P0.30, same pin RMK uses) through the
 * Zephyr 'led' API. Indications, distinguished by blink count and pace:
 *
 *   boot          N slow blinks = own battery level, one blink per ~20%
 *   critical batt fast triple blink on every battery state change
 *   profile switch  N quick blinks (profile index + 1), then the state of
 *                 that profile: 1 long blink connected, 2 medium advertising,
 *                 3 fast disconnected
 *   USB transport  1 long blink (central)
 *   split link     1 long blink connected / 3 fast disconnected (peripheral)
 *   layers         optional, N quick blinks for highest layer index
 *
 * Modeled on caksoylar's zmk-rgbled-widget (v0.3-branch), reduced from a
 * 3-channel RGB LED to a single channel: patterns replace colors.
 */

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/led.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>

#include <zmk/battery.h>
#include <zmk/ble.h>
#include <zmk/endpoints.h>
#include <zmk/events/battery_state_changed.h>
#include <zmk/events/ble_active_profile_changed.h>
#include <zmk/events/endpoint_changed.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk/events/split_peripheral_status_changed.h>
#include <zmk/events/activity_state_changed.h>
#include <zmk/keymap.h>
#include <zmk/split/bluetooth/peripheral.h>

#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#define STATUS_LED_NODE DT_ALIAS(k04_status_led)

BUILD_ASSERT(DT_NODE_EXISTS(STATUS_LED_NODE),
             "alias k04-status-led is required by the K:04 status LED widget");

// 'led' API device is the pwm-leds parent; the LED itself is one child.
static const struct device *led_dev = DEVICE_DT_GET(DT_PARENT(STATUS_LED_NODE));
static const uint8_t led_idx = DT_NODE_CHILD_IDX(STATUS_LED_NODE);

// flag to suppress indications until the boot sequence has run
static bool initialized = false;

// a queued blink pattern: blink `count` times, each blink on for `on_ms`
// with `off_ms` gaps in between
struct blink_item {
    uint8_t count;
    uint16_t on_ms;
    uint16_t off_ms;
};

K_MSGQ_DEFINE(led_msgq, sizeof(struct blink_item), 16, 1);

static void queue_blink(uint8_t count, uint16_t on_ms, uint16_t off_ms) {
    struct blink_item blink = {.count = count, .on_ms = on_ms, .off_ms = off_ms};
    k_msgq_put(&led_msgq, &blink, K_NO_WAIT);
}

// ------------------------------------------------------------- connectivity

#if !IS_ENABLED(CONFIG_ZMK_SPLIT) || IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)

static void indicate_connectivity_internal(void) {
    switch (zmk_endpoints_selected().transport) {
    case ZMK_TRANSPORT_USB:
        if (IS_ENABLED(CONFIG_K04_STATUS_LED_CONN_SHOW_USB)) {
            LOG_INF("USB transport selected");
            queue_blink(1, 600, 0);
        }
        break;
    default: // ZMK_TRANSPORT_BLE
        if (IS_ENABLED(CONFIG_ZMK_BLE)) {
            uint8_t profile_index = zmk_ble_active_profile_index();
            if (zmk_ble_active_profile_is_connected()) {
                LOG_INF("Profile %d connected", profile_index);
                queue_blink(1, 600, 0);
            } else if (zmk_ble_active_profile_is_open()) {
                LOG_INF("Profile %d advertising", profile_index);
                queue_blink(2, 250, 250);
            } else {
                LOG_INF("Profile %d disconnected", profile_index);
                queue_blink(3, 150, 150);
            }
        }
        break;
    }
}

// profile switch: show the new profile index first, then its state
static void indicate_profile_internal(void) {
    uint8_t profile_index = zmk_ble_active_profile_index();
    LOG_INF("Profile %d selected", profile_index);
    queue_blink(profile_index + 1, 180, 180);
    indicate_connectivity_internal();
}

#else

static void indicate_connectivity_internal(void) {
#if IS_ENABLED(CONFIG_ZMK_SPLIT_BLE)
    if (zmk_split_bt_peripheral_is_connected()) {
        LOG_INF("Split link connected");
        queue_blink(1, 600, 0);
    } else {
        LOG_INF("Split link disconnected");
        queue_blink(3, 150, 150);
    }
#endif
}

#endif // central / peripheral

// debouncing to ignore all but the last connectivity event
static struct k_work_delayable indicate_connectivity_work;

static void indicate_connectivity_cb(struct k_work *work) {
    ARG_UNUSED(work);
    indicate_connectivity_internal();
}
static void indicate_connectivity(void) {
    k_work_reschedule(&indicate_connectivity_work, K_MSEC(16));
}

static int led_output_listener_cb(const zmk_event_t *eh) {
    ARG_UNUSED(eh);
    if (initialized) {
        indicate_connectivity_internal();
    }
    return 0;
}

#if !IS_ENABLED(CONFIG_ZMK_SPLIT) || IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)

static int led_profile_listener_cb(const zmk_event_t *eh) {
    ARG_UNUSED(eh);
    if (initialized) {
        indicate_profile_internal();
    }
    return 0;
}

#endif

ZMK_LISTENER(led_output_listener, led_output_listener_cb);

#if !IS_ENABLED(CONFIG_ZMK_SPLIT) || IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
#if IS_ENABLED(CONFIG_K04_STATUS_LED_CONN_SHOW_USB)
ZMK_SUBSCRIPTION(led_output_listener, zmk_endpoint_changed);
#endif
ZMK_LISTENER(led_profile_listener, led_profile_listener_cb);
ZMK_SUBSCRIPTION(led_profile_listener, zmk_ble_active_profile_changed);
#else
ZMK_SUBSCRIPTION(led_output_listener, zmk_split_peripheral_status_changed);
#endif

// ----------------------------------------------------------------- battery

#if IS_ENABLED(CONFIG_ZMK_BATTERY_REPORTING)

static uint8_t battery_blink_count(uint8_t level) {
    if (level == 0) {
        return 1; // level not reported yet
    }
    return (level + 19) / 20; // ceil(level / 20): 1..5 blinks
}

static void indicate_battery(void) {
    uint8_t level = zmk_battery_state_of_charge();
    int retry = 0;
    while (level == 0 && retry++ < 10) {
        k_sleep(K_MSEC(100));
        level = zmk_battery_state_of_charge();
    }

    LOG_INF("Battery %d%%, blinking %d times", level, battery_blink_count(level));
    queue_blink(battery_blink_count(level), CONFIG_K04_STATUS_LED_BATTERY_BLINK_MS,
                CONFIG_K04_STATUS_LED_BATTERY_BLINK_MS);
}

static int led_battery_listener_cb(const zmk_event_t *eh) {
    if (!initialized) {
        return 0;
    }

    // warn when dropping to critical levels at runtime
    uint8_t level = as_zmk_battery_state_changed(eh)->state_of_charge;
    if (level > 0 && level <= CONFIG_K04_STATUS_LED_BATTERY_LEVEL_CRITICAL) {
        LOG_INF("Battery %d%% is critical", level);
        queue_blink(3, 120, 120);
    }
    return 0;
}

ZMK_LISTENER(led_battery_listener, led_battery_listener_cb);
ZMK_SUBSCRIPTION(led_battery_listener, zmk_battery_state_changed);

#endif // IS_ENABLED(CONFIG_ZMK_BATTERY_REPORTING)

// ------------------------------------------------------------------ layers

#if IS_ENABLED(CONFIG_K04_STATUS_LED_SHOW_LAYERS) &&                                       \
    (!IS_ENABLED(CONFIG_ZMK_SPLIT) || IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL))

static struct k_work_delayable layer_indicate_work;

static void indicate_layer(void) {
    uint8_t index = zmk_keymap_highest_layer_active();
    if (index > 0) {
        LOG_INF("Layer %d active, blinking %d times", index, index);
        queue_blink(index, 150, 150);
    }
}

static int led_layer_listener_cb(const zmk_event_t *eh) {
    // ignore layer-off events and pre-init events
    if (initialized && as_zmk_layer_state_changed(eh)->state) {
        k_work_reschedule(&layer_indicate_work,
                          K_MSEC(CONFIG_K04_STATUS_LED_LAYER_DEBOUNCE_MS));
    }
    return 0;
}

static void indicate_layer_cb(struct k_work *work) {
    ARG_UNUSED(work);
    indicate_layer();
}

ZMK_LISTENER(led_layer_listener, led_layer_listener_cb);
ZMK_SUBSCRIPTION(led_layer_listener, zmk_layer_state_changed);

#endif // K04_STATUS_LED_SHOW_LAYERS

// -------------------------------------------------------------- LED thread

// turn the LED off when the keyboard goes to sleep
static int led_activity_listener_cb(const zmk_event_t *eh) {
    struct zmk_activity_state_changed *ev = as_zmk_activity_state_changed(eh);
    if (ev != NULL && ev->state == ZMK_ACTIVITY_SLEEP) {
        led_off(led_dev, led_idx);
    }
    return 0;
}

ZMK_LISTENER(led_activity_listener, led_activity_listener_cb);
ZMK_SUBSCRIPTION(led_activity_listener, zmk_activity_state_changed);

extern void led_process_thread(void *d0, void *d1, void *d2) {
    ARG_UNUSED(d0);
    ARG_UNUSED(d1);
    ARG_UNUSED(d2);

    k_work_init_delayable(&indicate_connectivity_work, indicate_connectivity_cb);

#if IS_ENABLED(CONFIG_K04_STATUS_LED_SHOW_LAYERS) &&                                       \
    (!IS_ENABLED(CONFIG_ZMK_SPLIT) || IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL))
    k_work_init_delayable(&layer_indicate_work, indicate_layer_cb);
#endif

    while (true) {
        // wait until a blink pattern is received, then play it out
        struct blink_item blink;
        k_msgq_get(&led_msgq, &blink, K_FOREVER);

        for (uint8_t i = 0; i < blink.count; i++) {
            led_on(led_dev, led_idx);
            k_sleep(K_MSEC(blink.on_ms));
            led_off(led_dev, led_idx);
            if (i + 1 < blink.count && blink.off_ms > 0) {
                k_sleep(K_MSEC(blink.off_ms));
            }
        }

        // separate multi-blink patterns from the next indication
        if (blink.off_ms > 0) {
            k_sleep(K_MSEC(CONFIG_K04_STATUS_LED_INTERVAL_MS));
        }
    }
}

// pattern-processing thread, 1024 stack, started 100 ms after boot
K_THREAD_DEFINE(led_process_tid, 1024, led_process_thread, NULL, NULL, NULL,
                K_LOWEST_APPLICATION_THREAD_PRIO, 0, 100);

extern void led_init_thread(void *d0, void *d1, void *d2) {
    ARG_UNUSED(d0);
    ARG_UNUSED(d1);
    ARG_UNUSED(d2);

#if IS_ENABLED(CONFIG_ZMK_BATTERY_REPORTING)
    // indicate own battery level first
    indicate_battery();
    k_sleep(K_MSEC(6 * CONFIG_K04_STATUS_LED_BATTERY_BLINK_MS +
                  CONFIG_K04_STATUS_LED_INTERVAL_MS));
#endif

    // then the profile / split link state
    indicate_connectivity_internal();

    initialized = true;
    LOG_INF("K:04 status LED widget initialized");
}

// boot sequence thread, started 400 ms after boot
K_THREAD_DEFINE(led_init_tid, 1024, led_init_thread, NULL, NULL, NULL,
                K_LOWEST_APPLICATION_THREAD_PRIO, 0, 400);
