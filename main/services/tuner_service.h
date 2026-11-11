#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "metronome.h"
#include "tone_engine.h"
#include "tuner_engine.h"

/*
 * M9 audio facade: owns the capture reader, tuner detector, tone generator,
 * metronome and the two playback lanes while the Tuner app is mounted.
 *
 * The UI thread only calls setters/getters; rendering and pitch analysis run in
 * dedicated tasks. The service must be started on app mount and stopped on
 * unmount.
 */
esp_err_t tuner_service_start(void);
void tuner_service_stop(void);
bool tuner_service_running(void);

/* Latest pitch result; false when no valid snapshot is available yet. */
bool tuner_service_get_result(tuner_result_t *out);
void tuner_service_set_a4_reference(float a4_hz);
int tuner_service_get_output_volume(void);
esp_err_t tuner_service_set_output_volume(int percent);

/* Recent mono input snapshot for the waveform graph (oldest first). */
void tuner_service_get_waveform(int16_t *out, size_t max_samples,
                                size_t *samples);

/* Tone generator. Enabling flushes the lane and fades in from silence. */
esp_err_t tuner_service_set_tone_enabled(bool enabled);
bool tuner_service_tone_enabled(void);
void tuner_service_set_tone_waveform(tone_wave_t waveform);
void tuner_service_set_tone_frequency(float frequency_hz);
void tuner_service_set_tone_amplitude(float amplitude);
esp_err_t tuner_service_start_sweep(tone_sweep_mode_t mode, float start_hz,
                                    float end_hz, float duration_s);
void tuner_service_stop_sweep(void);

/* Metronome. Enabling restarts at beat 1. */
esp_err_t tuner_service_set_metronome_enabled(bool enabled);
bool tuner_service_metronome_enabled(void);
void tuner_service_set_metronome_bpm(float bpm);
void tuner_service_set_metronome_meter(metronome_meter_t meter);
void tuner_service_set_metronome_accent(uint8_t beat);
void tuner_service_set_metronome_click(metronome_click_wave_t wave);
void tuner_service_get_metronome_state(uint32_t *beat_count,
                                       uint32_t *bar_count,
                                       uint8_t *beat_in_bar);
