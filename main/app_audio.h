/*
 * SPDX-FileCopyrightText: 2023 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#pragma once


 // Enumeration for sound types
typedef enum {
    SOUND_TYPE_KNOB,              // Sound for knob interaction
    SOUND_TYPE_SNORE,             // Sound for snore audio
    SOUND_TYPE_WASH_END_CN,       // Washing machine end (Chinese)
    SOUND_TYPE_WASH_END_EN,       // Washing machine end (English)
    SOUND_TYPE_FACTORY,           // Factory reset sound
    SOUND_TYPE_BRIGHTNESS_0,     // Brightness 0% sound
    SOUND_TYPE_BRIGHTNESS_25,     // Brightness 25% sound
    SOUND_TYPE_BRIGHTNESS_50,     // Brightness 50% sound
    SOUND_TYPE_BRIGHTNESS_75,     // Brightness 75% sound
    SOUND_TYPE_BRIGHTNESS_100     // Brightness 100% sound
} PDM_SOUND_TYPE;

// Function to force stop audio playback
esp_err_t audio_force_quite(bool ret);

// Function to play a specific sound type
esp_err_t audio_handle_info(PDM_SOUND_TYPE voice);

// Function to initialize and start audio playback
esp_err_t audio_play_start();
