/*
 * SPDX-FileCopyrightText: 2023 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include "lvgl.h"
#include <stdio.h>

#include "lv_example_pub.h"
#include "lv_example_image.h"
#include "bsp/esp-bsp.h"
#include "app_audio.h" // changed
#include "settings.h" // changed
#include "freertos/FreeRTOS.h" //changed
#include "freertos/queue.h" //changed
#include "freertos/event_groups.h"//changed
#include "freertos/task.h"//changed


static bool light_2color_layer_enter_cb(void* layer);
static bool light_2color_layer_exit_cb(void* layer);
static void light_2color_layer_timer_cb(lv_timer_t* tmr);
static EventGroupHandle_t light_event_group;
static EventGroupHandle_t schedule_event_group;
const EventBits_t EVENT_BRIGHTNESS_UPDATE = (1 << 5); // Adjust the bit position as needed
const EventBits_t SCHEDULE_RESUME = (1 << 0);
const EventBits_t SCHEDULE_PAUSE = (1 << 1);
// Event bits for brightness levels
#define EVENT_BRIGHTNESS_0    (1 << 0)
#define EVENT_BRIGHTNESS_25   (1 << 1)
#define EVENT_BRIGHTNESS_50   (1 << 2)
#define EVENT_BRIGHTNESS_75   (1 << 3)
#define EVENT_BRIGHTNESS_100  (1 << 4)

bool schedule_active = true;

typedef enum {
    LIGHT_CCK_WARM,
    LIGHT_CCK_COOL,
    LIGHT_CCK_MAX,
} LIGHT_CCK_TYPE;
typedef struct {
    uint8_t light_pwm;
    LIGHT_CCK_TYPE light_cck;
} light_set_attribute_t;
typedef struct {
    const lv_img_dsc_t* img_bg[2];

    const lv_img_dsc_t* img_pwm_25[2];
    const lv_img_dsc_t* img_pwm_50[2];
    const lv_img_dsc_t* img_pwm_75[2];
    const lv_img_dsc_t* img_pwm_100[2];
} ui_light_img_t;

typedef struct {
    uint8_t light_level;
    LIGHT_CCK_TYPE light_color;
} light_setting_t;

light_setting_t light_settings[] = {
    {50, LIGHT_CCK_WARM},
    {100, LIGHT_CCK_COOL},
    {75, LIGHT_CCK_COOL},
    {25, LIGHT_CCK_WARM},
    {0, LIGHT_CCK_COOL}
};

static lv_obj_t* page;
static time_out_count time_20ms, time_500ms;

static lv_obj_t* img_light_bg, * label_pwm_set;
static lv_obj_t* img_light_pwm_25, * img_light_pwm_50, * img_light_pwm_75, * img_light_pwm_100, * img_light_pwm_0;

static light_set_attribute_t light_set_conf, light_xor;

static const ui_light_img_t light_image = {
    {&light_warm_bg,     &light_cool_bg},
    {&light_warm_25,     &light_cool_25},
    {&light_warm_50,     &light_cool_50},
    {&light_warm_75,     &light_cool_75},
    {&light_warm_100,    &light_cool_100},
};

lv_layer_t light_2color_Layer = {
    .lv_obj_name = "light_2color_Layer",
    .lv_obj_parent = NULL,
    .lv_obj_layer = NULL,
    .lv_show_layer = NULL,
    .enter_cb = light_2color_layer_enter_cb,
    .exit_cb = light_2color_layer_exit_cb,
    .timer_cb = light_2color_layer_timer_cb,
};



static void light_2color_event_cb(lv_event_t* e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (LV_EVENT_FOCUSED == code) {
        lv_group_set_editing(lv_group_get_default(), true);
    }
    else if (LV_EVENT_KEY == code) {
        uint32_t key = lv_event_get_key(e);
        if (is_time_out(&time_500ms)) {
            if (LV_KEY_RIGHT == key) {
                if (light_set_conf.light_pwm < 100) {
                    light_set_conf.light_pwm += 25;
                }
            }
            else if (LV_KEY_LEFT == key) {
                if (light_set_conf.light_pwm > 0) {
                    light_set_conf.light_pwm -= 25;
                }
            }
        }
    }
    else if (LV_EVENT_CLICKED == code) {
        static uint32_t last_click_time = 0;
        uint32_t current_time = lv_tick_get();

        if (current_time - last_click_time <= 500) {
            // Double-click detected
            schedule_active = !schedule_active;

            // Signal the schedule task to pause or resume
            if (schedule_active) {
                xEventGroupSetBits(schedule_event_group, SCHEDULE_RESUME);
            }
            else {
                xEventGroupSetBits(schedule_event_group, SCHEDULE_PAUSE);
            }
        }
        else {
            // Single-click detected
            light_set_conf.light_cck = (LIGHT_CCK_WARM == light_set_conf.light_cck) ? (LIGHT_CCK_COOL) : (LIGHT_CCK_WARM);
            // Signal the main task to update the light settings
            xEventGroupSetBits(light_event_group, EVENT_BRIGHTNESS_UPDATE);
        }

        last_click_time = current_time;
    }
    else if (LV_EVENT_LONG_PRESSED == code) {
        lv_indev_wait_release(lv_indev_get_next(NULL));
        ui_remove_all_objs_from_encoder_group();
        lv_func_goto_layer(&menu_layer);
    }
}

void ui_light_2color_init(lv_obj_t* parent)
{
    light_xor.light_pwm = 0xFF;
    light_xor.light_cck = LIGHT_CCK_MAX;

    light_set_conf.light_pwm = 50;
    light_set_conf.light_cck = LIGHT_CCK_WARM;

    page = lv_obj_create(parent);
    lv_obj_set_size(page, LV_HOR_RES, LV_VER_RES);
    //lv_obj_set_size(page, lv_obj_get_width(lv_obj_get_parent(page)), lv_obj_get_height(lv_obj_get_parent(page)));

    lv_obj_set_style_border_width(page, 0, 0);
    lv_obj_set_style_radius(page, 0, 0);
    lv_obj_clear_flag(page, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(page);

    img_light_bg = lv_img_create(page);
    lv_img_set_src(img_light_bg, &light_warm_bg);
    lv_obj_align(img_light_bg, LV_ALIGN_CENTER, 0, 0);

    label_pwm_set = lv_label_create(page);
    lv_obj_set_style_text_font(label_pwm_set, &HelveticaNeue_Regular_24, 0);
    if (light_set_conf.light_pwm) {
        lv_label_set_text_fmt(label_pwm_set, "%d%%", light_set_conf.light_pwm);
    }
    else {
        lv_label_set_text(label_pwm_set, "--");
    }
    lv_obj_align(label_pwm_set, LV_ALIGN_CENTER, 0, 65);

    img_light_pwm_0 = lv_img_create(page);
    lv_img_set_src(img_light_pwm_0, &light_close_status);
    lv_obj_add_flag(img_light_pwm_0, LV_OBJ_FLAG_HIDDEN);
    lv_obj_align(img_light_pwm_0, LV_ALIGN_TOP_MID, 0, 0);

    img_light_pwm_25 = lv_img_create(page);
    lv_img_set_src(img_light_pwm_25, &light_warm_25);
    lv_obj_align(img_light_pwm_25, LV_ALIGN_TOP_MID, 0, 0);

    img_light_pwm_50 = lv_img_create(page);
    lv_img_set_src(img_light_pwm_50, &light_warm_50);
    lv_obj_align(img_light_pwm_50, LV_ALIGN_TOP_MID, 0, 0);

    img_light_pwm_75 = lv_img_create(page);
    lv_img_set_src(img_light_pwm_75, &light_warm_75);
    lv_obj_add_flag(img_light_pwm_75, LV_OBJ_FLAG_HIDDEN);
    lv_obj_align(img_light_pwm_75, LV_ALIGN_TOP_MID, 0, 0);

    img_light_pwm_100 = lv_img_create(page);
    lv_img_set_src(img_light_pwm_100, &light_warm_100);
    lv_obj_add_flag(img_light_pwm_100, LV_OBJ_FLAG_HIDDEN);
    lv_obj_align(img_light_pwm_100, LV_ALIGN_TOP_MID, 0, 0);

    lv_obj_add_event_cb(page, light_2color_event_cb, LV_EVENT_FOCUSED, NULL);
    lv_obj_add_event_cb(page, light_2color_event_cb, LV_EVENT_KEY, NULL);
    lv_obj_add_event_cb(page, light_2color_event_cb, LV_EVENT_LONG_PRESSED, NULL);
    lv_obj_add_event_cb(page, light_2color_event_cb, LV_EVENT_CLICKED, NULL);
    ui_add_obj_to_encoder_group(page);
}
static void voice_announcement_task(void* param) {
    while (1) {
        // Wait for any brightness event bit
        EventBits_t bits = xEventGroupWaitBits(
            light_event_group,
            EVENT_BRIGHTNESS_0 | EVENT_BRIGHTNESS_25 |
            EVENT_BRIGHTNESS_50 | EVENT_BRIGHTNESS_75 |
            EVENT_BRIGHTNESS_100,
            pdTRUE,         // Clear bits after they are read
            pdFALSE,        // Wait for any bit
            portMAX_DELAY   // Block indefinitely
        );

        // Determine which event bit was triggered
        if (bits & EVENT_BRIGHTNESS_0) {
            audio_handle_info(SOUND_TYPE_BRIGHTNESS_0);
        }
        else if (bits & EVENT_BRIGHTNESS_25) {
            audio_handle_info(SOUND_TYPE_BRIGHTNESS_25);
        }
        else if (bits & EVENT_BRIGHTNESS_50) {
            audio_handle_info(SOUND_TYPE_BRIGHTNESS_50);
        }
        else if (bits & EVENT_BRIGHTNESS_75) {
            audio_handle_info(SOUND_TYPE_BRIGHTNESS_75);
        }
        else if (bits & EVENT_BRIGHTNESS_100) {
            audio_handle_info(SOUND_TYPE_BRIGHTNESS_100);
        }
    }
}
const int num_settings = sizeof(light_settings) / sizeof(light_settings[0]);
int current_setting_index = 0;

void schedule_task(void* pvParameters) {
    EventBits_t uxBits;
   

    while (1) {
        uxBits = xEventGroupWaitBits(
            schedule_event_group,
            SCHEDULE_RESUME,
            pdTRUE,
            pdFALSE,
            portMAX_DELAY
        );

        if (uxBits & SCHEDULE_RESUME) {
            // Schedule is active, continue with normal operation
            while (schedule_active) {
                // Update the light settings
                light_set_conf.light_pwm = light_settings[current_setting_index].light_level;
                light_set_conf.light_cck = light_settings[current_setting_index].light_color;

                // Signal the main task to update the light settings
                xEventGroupSetBits(light_event_group, EVENT_BRIGHTNESS_UPDATE);

                // Move to the next setting or loop back to the beginning
                current_setting_index = (current_setting_index + 1) % num_settings;

                // Calculate the next wake-up time based on the current index
                if (current_setting_index == 1) {
                    vTaskDelay(10000 / portTICK_PERIOD_MS);
                }
                else if (current_setting_index == 2) {
                    vTaskDelay(15000 / portTICK_PERIOD_MS);
                }
                else if (current_setting_index == 3) {
                    vTaskDelay(15000 / portTICK_PERIOD_MS);
                }
                else if (current_setting_index == 4) {
                    vTaskDelay(15000 / portTICK_PERIOD_MS);
                }
                else {
                    vTaskDelay(20000 / portTICK_PERIOD_MS);
                }

            }
        }
        else {
            // Schedule is paused, wait for the resume signal
            xEventGroupWaitBits(
                schedule_event_group,
                SCHEDULE_RESUME,
                pdTRUE,
                pdFALSE,
                portMAX_DELAY
            );
        }
    }
}

static bool light_2color_layer_enter_cb(void* layer)
{
    bool ret = false;

    LV_LOG_USER("");
    lv_layer_t* create_layer = layer;
    if (NULL == create_layer->lv_obj_layer) {
        ret = true;
        create_layer->lv_obj_layer = lv_obj_create(lv_scr_act());
        lv_obj_remove_style_all(create_layer->lv_obj_layer);
        lv_obj_set_size(create_layer->lv_obj_layer, LV_HOR_RES, LV_VER_RES);

        ui_light_2color_init(create_layer->lv_obj_layer);
        set_time_out(&time_20ms, 20);
        set_time_out(&time_500ms, 200);
        light_event_group = xEventGroupCreate();
        schedule_event_group = xEventGroupCreate();
        schedule_active = true;
        xEventGroupSetBits(schedule_event_group, SCHEDULE_RESUME);

        if (light_event_group == NULL) {
            printf("Error: Failed to create event group.\n");
            return false; // Abort initialization if event group creation fails
        }

        // Create the voice announcement task
        xTaskCreate(
            voice_announcement_task,  // Task function
            "VoiceAnnouncementTask",  // Task name
            2048,                     // Stack size
            NULL,                     // Task parameters
            5,                        // Task priority
            NULL                      // Task handle
        );
        xTaskCreate(
            schedule_task,
            "ScheduleTask",
            2048,
            NULL,
            5,
            NULL
        );
    }

    return ret;
}

static bool light_2color_layer_exit_cb(void* layer)
{
    LV_LOG_USER("");
    bsp_led_rgb_set(0x00, 0x00, 0x00);
    return true;
}

static void light_2color_layer_timer_cb(lv_timer_t* tmr)
{
    uint32_t RGB_color = 0xFF;

    feed_clock_time();

    if (is_time_out(&time_20ms)) {

        if ((light_set_conf.light_pwm ^ light_xor.light_pwm) || (light_set_conf.light_cck ^ light_xor.light_cck)) {


            light_xor.light_pwm = light_set_conf.light_pwm;
            light_xor.light_cck = light_set_conf.light_cck;

            switch (light_xor.light_pwm) {
            case 0:
                xEventGroupSetBits(light_event_group, EVENT_BRIGHTNESS_0);
                break;
            case 25:
                xEventGroupSetBits(light_event_group, EVENT_BRIGHTNESS_25);
                break;
            case 50:
                xEventGroupSetBits(light_event_group, EVENT_BRIGHTNESS_50);
                break;
            case 75:
                xEventGroupSetBits(light_event_group, EVENT_BRIGHTNESS_75);
                break;
            case 100:
                xEventGroupSetBits(light_event_group, EVENT_BRIGHTNESS_100);
                break;
            default:
                break;
            }

            if (LIGHT_CCK_COOL == light_xor.light_cck) {
                RGB_color = (0xFF * light_xor.light_pwm / 100) << 16 | (0xFF * light_xor.light_pwm / 100) << 8 | (0xFF * light_xor.light_pwm / 100) << 0;
            }
            else {
                RGB_color = (0xFF * light_xor.light_pwm / 100) << 16 | (0xFF * light_xor.light_pwm / 100) << 8 | (0x33 * light_xor.light_pwm / 100) << 0;
            }
            bsp_led_rgb_set((RGB_color >> 16) & 0xFF, (RGB_color >> 8) & 0xFF, (RGB_color >> 0) & 0xFF);

            lv_obj_add_flag(img_light_pwm_100, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(img_light_pwm_75, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(img_light_pwm_50, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(img_light_pwm_25, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(img_light_pwm_0, LV_OBJ_FLAG_HIDDEN);

            if (light_set_conf.light_pwm) {
                lv_label_set_text_fmt(label_pwm_set, "%d%%", light_set_conf.light_pwm);
            }
            else {
                lv_label_set_text(label_pwm_set, "--");
            }

            uint8_t cck_set = (uint8_t)light_xor.light_cck;

            switch (light_xor.light_pwm) {
            case 100:
                lv_obj_clear_flag(img_light_pwm_100, LV_OBJ_FLAG_HIDDEN);
                lv_img_set_src(img_light_pwm_100, light_image.img_pwm_100[cck_set]);
            case 75:
                lv_obj_clear_flag(img_light_pwm_75, LV_OBJ_FLAG_HIDDEN);
                lv_img_set_src(img_light_pwm_75, light_image.img_pwm_75[cck_set]);
            case 50:
                lv_obj_clear_flag(img_light_pwm_50, LV_OBJ_FLAG_HIDDEN);
                lv_img_set_src(img_light_pwm_50, light_image.img_pwm_50[cck_set]);
            case 25:
                lv_obj_clear_flag(img_light_pwm_25, LV_OBJ_FLAG_HIDDEN);
                lv_img_set_src(img_light_pwm_25, light_image.img_pwm_25[cck_set]);
                lv_img_set_src(img_light_bg, light_image.img_bg[cck_set]);
                break;
            case 0:
                lv_obj_clear_flag(img_light_pwm_0, LV_OBJ_FLAG_HIDDEN);
                lv_img_set_src(img_light_bg, &light_close_bg);
                break;
            default:
                break;
            }
        }
    }
}
