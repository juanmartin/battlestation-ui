#include "apps.h"

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bsp/display.h"
#include "bsp/esp-bsp.h"
#include "esp_log.h"
#include "tuner_service.h"
#include "ui_theme.h"

static const char *TAG = "tuner";

#define COLOR_BG 0x0B0F14
#define COLOR_BUTTON 0x1B2430
#define COLOR_BUTTON_PRESSED 0x243040
#define COLOR_BORDER 0x2A3542
#define COLOR_TEXT 0xE2E8F0
#define COLOR_MUTED 0x94A3B8
#define COLOR_DIM 0x64748B
#define COLOR_CYAN 0x22D3EE
#define COLOR_GREEN 0x4ADE80
#define COLOR_AMBER 0xFBBF24
#define COLOR_MAGENTA 0xE879F9
#define COLOR_RED 0xF87171

#define TUNER_TIMER_MS 33
#define WAVE_POINTS 128
#define WAVE_WIDTH 420
#define WAVE_HEIGHT 300

#define PAGE_Y 136
#define PAGE_W 1024
#define PAGE_H 440

#define NEEDLE_TRACK_X 24
#define NEEDLE_TRACK_W 432
#define NEEDLE_WIDTH 4
#define NEEDLE_Y 270
#define NEEDLE_HEIGHT 24

typedef enum {
    TAB_TUNER = 0,
    TAB_TONE,
    TAB_METRONOME,
    TAB_COUNT,
} tuner_tab_t;

typedef struct {
    lv_obj_t *btn;
    lv_obj_t *label;
} tuner_button_t;

typedef struct {
    lv_obj_t *root;
    lv_obj_t *pages[TAB_COUNT];
    tuner_button_t tabs[TAB_COUNT];
    tuner_tab_t tab;

    /* Tuner page. */
    lv_obj_t *note;
    lv_obj_t *frequency;
    lv_obj_t *cents;
    lv_obj_t *status;
    lv_obj_t *confidence;
    lv_obj_t *confidence_bar;
    lv_obj_t *needle;
    lv_obj_t *wave_line;
    lv_obj_t *volume_value;
    lv_obj_t *volume_slider;

    /* Tone page. */
    tuner_button_t tone_play;
    lv_obj_t *tone_state;
    tuner_button_t tone_wave[4];
    lv_obj_t *tone_freq_value;
    lv_obj_t *tone_freq_slider;
    lv_obj_t *tone_amp_value;
    lv_obj_t *tone_amp_slider;
    lv_obj_t *tone_big;
    tuner_button_t tone_sweep;

    /* Metronome page. */
    tuner_button_t metro_play;
    lv_obj_t *metro_state;
    lv_obj_t *metro_bpm_value;
    lv_obj_t *metro_bpm_slider;
    lv_obj_t *metro_bpm_big;
    tuner_button_t metro_step[2];
    tuner_button_t metro_meter[4];
    tuner_button_t metro_accent;
    lv_obj_t *metro_info;
    lv_obj_t *metro_count;
    lv_obj_t *metro_circle[6];
    lv_obj_t *metro_circle_text[6];
    tuner_button_t metro_click[2];

    /* Local mirror of the service settings. */
    int tone_hz;
    int tone_amp;
    tone_wave_t tone_waveform;
    bool sweep_on;
    uint32_t sweep_end_ms;
    int bpm;
    int bpm_step;
    metronome_meter_t meter;
    int accent;
    metronome_click_wave_t click_wave;
    int visual_beat;
    bool applying;
} tuner_ui_t;

static tuner_ui_t s_ui;
static lv_timer_t *s_timer;
static lv_point_precise_t s_wave_points[WAVE_POINTS];

static void update_tuner_view(void);
static void update_waveform(void);
static void update_metronome_visual(void);
static void tone_refresh(void);
static void metro_refresh(void);

static void volume_cb(lv_event_t *e)
{
    lv_obj_t *slider = lv_event_get_target(e);
    const int value = (int)lv_slider_get_value(slider);

    tuner_service_set_output_volume(value);
    if (s_ui.volume_value != NULL) {
        lv_label_set_text_fmt(s_ui.volume_value, "%d%%", value);
    }
}

static void label_set(lv_obj_t *label, const char *text)
{
    if (label == NULL) {
        return;
    }
    const char *current = lv_label_get_text(label);
    if (current == NULL || strcmp(current, text) != 0) {
        lv_label_set_text(label, text);
    }
}

static void button_style(tuner_button_t *button, bool selected, uint32_t accent)
{
    if (button->btn == NULL) {
        return;
    }
    lv_obj_set_style_bg_color(button->btn,
                              lv_color_hex(selected ? accent : COLOR_BUTTON), 0);
    lv_obj_set_style_bg_opa(button->btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(button->btn,
                                  lv_color_hex(selected ? accent : COLOR_BORDER), 0);
    lv_obj_set_style_text_color(button->label,
                                lv_color_hex(selected ? COLOR_BG : COLOR_TEXT), 0);
}

static lv_obj_t *make_button(lv_obj_t *parent, const char *text, int x, int y,
                             int w, int h, const lv_font_t *font,
                             lv_event_cb_t cb, void *user_data,
                             tuner_button_t *out)
{
    lv_obj_t *btn = ui_theme_card(parent, x, y, w, h);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(btn, lv_color_hex(COLOR_BUTTON), 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(COLOR_BUTTON_PRESSED),
                              LV_STATE_PRESSED);
    lv_obj_set_style_radius(btn, 10, 0);

    lv_obj_t *label = ui_theme_label(btn, text, font, COLOR_TEXT, 0, 0);
    lv_obj_center(label);

    if (cb != NULL) {
        lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, user_data);
    }
    if (out != NULL) {
        out->btn = btn;
        out->label = label;
    }
    return btn;
}

static lv_obj_t *make_slider(lv_obj_t *parent, int x, int y, int w, int h,
                             int min, int max, int value,
                             lv_event_cb_t cb, void *user_data)
{
    lv_obj_t *slider = lv_slider_create(parent);
    lv_obj_set_pos(slider, x, y);
    lv_obj_set_size(slider, w, h);
    lv_slider_set_range(slider, min, max);
    lv_slider_set_value(slider, value, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(slider, lv_color_hex(COLOR_BG), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(slider, 6, LV_PART_MAIN);
    lv_obj_set_style_bg_color(slider, lv_color_hex(COLOR_CYAN),
                              LV_PART_INDICATOR);
    lv_obj_set_style_radius(slider, 6, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider, lv_color_hex(COLOR_TEXT), LV_PART_KNOB);
    lv_obj_remove_flag(slider, LV_OBJ_FLAG_SCROLLABLE);
    if (cb != NULL) {
        lv_obj_add_event_cb(slider, cb, LV_EVENT_VALUE_CHANGED, user_data);
    }
    return slider;
}

static lv_obj_t *make_page(lv_obj_t *parent, tuner_tab_t tab)
{
    lv_obj_t *page = lv_obj_create(parent);
    lv_obj_set_pos(page, 0, PAGE_Y);
    lv_obj_set_size(page, PAGE_W, PAGE_H);
    lv_obj_remove_flag(page, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(page, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(page, 0, 0);
    lv_obj_set_style_radius(page, 0, 0);
    lv_obj_set_style_pad_all(page, 0, 0);
    s_ui.pages[tab] = page;
    return page;
}

static void tab_clicked_cb(lv_event_t *e)
{
    const tuner_tab_t tab = (tuner_tab_t)(intptr_t)lv_event_get_user_data(e);

    for (int i = 0; i < TAB_COUNT; i++) {
        if (s_ui.pages[i] != NULL) {
            if ((tuner_tab_t)i == tab) {
                lv_obj_remove_flag(s_ui.pages[i], LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_add_flag(s_ui.pages[i], LV_OBJ_FLAG_HIDDEN);
            }
        }
        button_style(&s_ui.tabs[i], (tuner_tab_t)i == tab, COLOR_CYAN);
    }
    s_ui.tab = tab;

    if (tab == TAB_TUNER) {
        update_tuner_view();
        update_waveform();
    } else if (tab == TAB_TONE) {
        tone_refresh();
    } else {
        update_metronome_visual();
        metro_refresh();
    }
}

/* ------------------------------------------------------------------------- */
/* Tuner page                                                                */
/* ------------------------------------------------------------------------- */

static void set_needle(float cents, bool valid)
{
    float clamped = cents;
    if (!isfinite(clamped) || clamped < -50.0f) {
        clamped = -50.0f;
    } else if (clamped > 50.0f) {
        clamped = 50.0f;
    }

    const int travel = (NEEDLE_TRACK_W - NEEDLE_WIDTH) / 2;
    int offset = (int)lroundf(clamped / 50.0f * (float)travel);
    if (offset < -travel) {
        offset = -travel;
    } else if (offset > travel) {
        offset = travel;
    }
    lv_obj_set_x(s_ui.needle,
                 NEEDLE_TRACK_X + (NEEDLE_TRACK_W - NEEDLE_WIDTH) / 2 + offset);

    uint32_t color = COLOR_DIM;
    if (valid) {
        const float magnitude = fabsf(cents);
        if (magnitude <= 5.0f) {
            color = COLOR_GREEN;
        } else if (magnitude <= 20.0f) {
            color = COLOR_AMBER;
        } else {
            color = COLOR_RED;
        }
    }
    lv_obj_set_style_bg_color(s_ui.needle, lv_color_hex(color), 0);
}

static void update_tuner_view(void)
{
    tuner_result_t result;
    const bool have = tuner_service_get_result(&result);
    const bool analyzed = have && result.updates > 0u;
    const bool valid = analyzed && result.valid && isfinite(result.frequency_hz);
    char buf[48];

    if (valid) {
        snprintf(buf, sizeof(buf), "%s%d", result.note_name,
                 (int)result.octave);
        label_set(s_ui.note, buf);
        snprintf(buf, sizeof(buf), "%.1f Hz", result.frequency_hz);
        label_set(s_ui.frequency, buf);
        snprintf(buf, sizeof(buf), "%+.1f cents", result.cents);
        label_set(s_ui.cents, buf);
        label_set(s_ui.status, "tracking");
        lv_obj_set_style_text_color(s_ui.status, lv_color_hex(COLOR_GREEN), 0);

        int percent = (int)lroundf(result.confidence * 100.0f);
        if (percent < 0) {
            percent = 0;
        } else if (percent > 100) {
            percent = 100;
        }
        snprintf(buf, sizeof(buf), "%d%%", percent);
        label_set(s_ui.confidence, buf);
        lv_bar_set_value(s_ui.confidence_bar, percent, LV_ANIM_OFF);

        set_needle(result.cents, true);
    } else {
        label_set(s_ui.note, "--");
        label_set(s_ui.frequency, "-- Hz");
        label_set(s_ui.cents, "-- cents");
        /* No analysis yet means the first window is still filling; an
         * analyzed but invalid result means silence or an unclear pitch. */
        const char *status = analyzed ? "no signal" : "listening...";
        label_set(s_ui.status, status);
        lv_obj_set_style_text_color(
            s_ui.status, lv_color_hex(analyzed ? COLOR_MUTED : COLOR_AMBER), 0);

        int percent = 0;
        if (analyzed && isfinite(result.confidence)) {
            percent = (int)lroundf(result.confidence * 100.0f);
            if (percent < 0) {
                percent = 0;
            } else if (percent > 100) {
                percent = 100;
            }
        }
        snprintf(buf, sizeof(buf), "%d%%", percent);
        label_set(s_ui.confidence, buf);
        lv_bar_set_value(s_ui.confidence_bar, percent, LV_ANIM_OFF);
        set_needle(0.0f, false);
    }
}

static void update_waveform(void)
{
    int16_t samples[WAVE_POINTS];
    size_t count = 0;

    tuner_service_get_waveform(samples, WAVE_POINTS, &count);
    if (count > WAVE_POINTS) {
        count = WAVE_POINTS;
    }

    for (size_t i = 0; i < WAVE_POINTS; i++) {
        const int16_t sample = (i < count) ? samples[i] : 0;
        int32_t y = (WAVE_HEIGHT / 2) -
                    ((int32_t)sample * (WAVE_HEIGHT / 2 - 5)) / 32768;
        if (y < 1) {
            y = 1;
        } else if (y > WAVE_HEIGHT - 2) {
            y = WAVE_HEIGHT - 2;
        }
        s_wave_points[i].x =
            (lv_value_precise_t)(i * (WAVE_WIDTH - 1) / (WAVE_POINTS - 1));
        s_wave_points[i].y = (lv_value_precise_t)y;
    }
    lv_line_set_points(s_ui.wave_line, s_wave_points, WAVE_POINTS);
}

static void build_tuner_page(lv_obj_t *page)
{
    lv_obj_t *pitch = ui_theme_card(page, 32, 0, 480, PAGE_H);
    ui_theme_label(pitch, "PITCH", &lv_font_montserrat_20, COLOR_CYAN, 24, 18);

    s_ui.note = ui_theme_label(pitch, "--", &lv_font_montserrat_48,
                               COLOR_TEXT, 24, 44);
    s_ui.frequency = ui_theme_label(pitch, "-- Hz", &lv_font_montserrat_20,
                                    COLOR_TEXT, 24, 112);
    s_ui.cents = ui_theme_label(pitch, "-- cents", &lv_font_montserrat_20,
                                COLOR_MUTED, 24, 142);

    ui_theme_label(pitch, "CONFIDENCE", &lv_font_montserrat_14, COLOR_MUTED,
                   24, 188);
    s_ui.confidence = ui_theme_label(pitch, "0%", &lv_font_montserrat_14,
                                     COLOR_TEXT, 404, 188);
    lv_obj_set_width(s_ui.confidence, 52);
    lv_obj_set_style_text_align(s_ui.confidence, LV_TEXT_ALIGN_RIGHT, 0);

    s_ui.confidence_bar = lv_bar_create(pitch);
    lv_obj_set_pos(s_ui.confidence_bar, 24, 210);
    lv_obj_set_size(s_ui.confidence_bar, 432, 12);
    lv_bar_set_range(s_ui.confidence_bar, 0, 100);
    lv_bar_set_value(s_ui.confidence_bar, 0, LV_ANIM_OFF);
    lv_obj_remove_flag(s_ui.confidence_bar, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(s_ui.confidence_bar, lv_color_hex(COLOR_BG),
                              LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_ui.confidence_bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(s_ui.confidence_bar, 6, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_ui.confidence_bar, lv_color_hex(COLOR_CYAN),
                              LV_PART_INDICATOR);
    lv_obj_set_style_radius(s_ui.confidence_bar, 6, LV_PART_INDICATOR);

    ui_theme_label(pitch, "CENTS", &lv_font_montserrat_14, COLOR_MUTED, 24, 246);

    lv_obj_t *track = lv_obj_create(pitch);
    lv_obj_set_pos(track, NEEDLE_TRACK_X, 282);
    lv_obj_set_size(track, NEEDLE_TRACK_W, 4);
    lv_obj_remove_flag(track, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(track, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(track, lv_color_hex(COLOR_BORDER), 0);
    lv_obj_set_style_bg_opa(track, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(track, 0, 0);
    lv_obj_set_style_radius(track, 2, 0);
    lv_obj_set_style_pad_all(track, 0, 0);

    lv_obj_t *center = lv_obj_create(pitch);
    lv_obj_set_pos(center, NEEDLE_TRACK_X + NEEDLE_TRACK_W / 2 - 1, 274);
    lv_obj_set_size(center, 2, 20);
    lv_obj_remove_flag(center, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(center, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(center, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_set_style_bg_opa(center, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(center, 0, 0);
    lv_obj_set_style_radius(center, 1, 0);
    lv_obj_set_style_pad_all(center, 0, 0);

    s_ui.needle = lv_obj_create(pitch);
    lv_obj_set_pos(s_ui.needle,
                   NEEDLE_TRACK_X + (NEEDLE_TRACK_W - NEEDLE_WIDTH) / 2,
                   NEEDLE_Y);
    lv_obj_set_size(s_ui.needle, NEEDLE_WIDTH, NEEDLE_HEIGHT);
    lv_obj_remove_flag(s_ui.needle, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(s_ui.needle, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(s_ui.needle, lv_color_hex(COLOR_DIM), 0);
    lv_obj_set_style_bg_opa(s_ui.needle, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_ui.needle, 0, 0);
    lv_obj_set_style_radius(s_ui.needle, 2, 0);
    lv_obj_set_style_pad_all(s_ui.needle, 0, 0);

    lv_obj_t *neg = ui_theme_label(pitch, "-50", &lv_font_montserrat_14,
                                   COLOR_DIM, 24, 300);
    lv_obj_set_width(neg, 36);
    lv_obj_set_style_text_align(neg, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_t *mid = ui_theme_label(pitch, "0", &lv_font_montserrat_14,
                                   COLOR_DIM, 225, 300);
    lv_obj_set_width(mid, 30);
    lv_obj_set_style_text_align(mid, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_t *pos = ui_theme_label(pitch, "+50", &lv_font_montserrat_14,
                                   COLOR_DIM, 420, 300);
    lv_obj_set_width(pos, 36);
    lv_obj_set_style_text_align(pos, LV_TEXT_ALIGN_RIGHT, 0);

    ui_theme_label(pitch, "A4 = 440 Hz", &lv_font_montserrat_20, COLOR_TEXT,
                   24, 334);
    s_ui.status = ui_theme_label(pitch, "listening...", &lv_font_montserrat_20,
                                 COLOR_AMBER, 24, 372);
    ui_theme_label(pitch, "ES7210 / 16 kHz capture / chromatic",
                   &lv_font_montserrat_14, COLOR_DIM, 24, 406);

    lv_obj_t *wave = ui_theme_card(page, 532, 0, 460, PAGE_H);
    ui_theme_label(wave, "INPUT WAVEFORM", &lv_font_montserrat_20, COLOR_CYAN,
                   20, 18);
    ui_theme_label(wave, "30 fps / last 128 samples", &lv_font_montserrat_14,
                   COLOR_MUTED, 20, 48);

    lv_obj_t *graph = lv_obj_create(wave);
    lv_obj_set_pos(graph, 20, 76);
    lv_obj_set_size(graph, WAVE_WIDTH, WAVE_HEIGHT);
    lv_obj_remove_flag(graph, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(graph, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(graph, lv_color_hex(COLOR_BG), 0);
    lv_obj_set_style_bg_opa(graph, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(graph, 1, 0);
    lv_obj_set_style_border_color(graph, lv_color_hex(COLOR_BORDER), 0);
    lv_obj_set_style_radius(graph, 8, 0);
    lv_obj_set_style_pad_all(graph, 0, 0);

    lv_obj_t *baseline = lv_obj_create(graph);
    lv_obj_set_pos(baseline, 0, WAVE_HEIGHT / 2);
    lv_obj_set_size(baseline, WAVE_WIDTH, 1);
    lv_obj_remove_flag(baseline, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(baseline, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(baseline, lv_color_hex(COLOR_BORDER), 0);
    lv_obj_set_style_bg_opa(baseline, LV_OPA_50, 0);
    lv_obj_set_style_border_width(baseline, 0, 0);
    lv_obj_set_style_radius(baseline, 0, 0);
    lv_obj_set_style_pad_all(baseline, 0, 0);

    s_ui.wave_line = lv_line_create(graph);
    lv_obj_set_pos(s_ui.wave_line, 0, 0);
    lv_obj_set_size(s_ui.wave_line, WAVE_WIDTH, WAVE_HEIGHT);
    lv_obj_remove_flag(s_ui.wave_line, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(s_ui.wave_line, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_ui.wave_line, 0, 0);
    lv_obj_set_style_pad_all(s_ui.wave_line, 0, 0);
    lv_obj_set_style_line_width(s_ui.wave_line, 2, 0);
    lv_obj_set_style_line_color(s_ui.wave_line, lv_color_hex(COLOR_CYAN), 0);
    lv_obj_set_style_line_rounded(s_ui.wave_line, true, 0);

    ui_theme_label(wave, "mono mix / rolling capture snapshot",
                   &lv_font_montserrat_14, COLOR_DIM, 20, 390);
}

/* ------------------------------------------------------------------------- */
/* Tone page                                                                 */
/* ------------------------------------------------------------------------- */

static void tone_update_values(void)
{
    char buf[32];

    snprintf(buf, sizeof(buf), "%d Hz", s_ui.tone_hz);
    label_set(s_ui.tone_freq_value, buf);

    snprintf(buf, sizeof(buf), "%d%%", s_ui.tone_amp);
    label_set(s_ui.tone_amp_value, buf);

    if (s_ui.sweep_on) {
        label_set(s_ui.tone_big, "100-1000 Hz");
    } else {
        snprintf(buf, sizeof(buf), "%d Hz", s_ui.tone_hz);
        label_set(s_ui.tone_big, buf);
    }
}

static void tone_update_state(void)
{
    if (s_ui.sweep_on) {
        if (tuner_service_tone_enabled()) {
            label_set(s_ui.tone_state, "SWEEPING 100 > 1000 Hz");
            lv_obj_set_style_text_color(s_ui.tone_state,
                                        lv_color_hex(COLOR_MAGENTA), 0);
        } else {
            label_set(s_ui.tone_state, "SWEEP ARMED / TONE OFF");
            lv_obj_set_style_text_color(s_ui.tone_state,
                                        lv_color_hex(COLOR_AMBER), 0);
        }
    } else if (tuner_service_tone_enabled()) {
        char buf[32];
        snprintf(buf, sizeof(buf), "PLAYING %d Hz", s_ui.tone_hz);
        label_set(s_ui.tone_state, buf);
        lv_obj_set_style_text_color(s_ui.tone_state,
                                    lv_color_hex(COLOR_GREEN), 0);
    } else {
        label_set(s_ui.tone_state, "OUTPUT OFF");
        lv_obj_set_style_text_color(s_ui.tone_state,
                                    lv_color_hex(COLOR_MUTED), 0);
    }
}

static void tone_refresh(void)
{
    const bool enabled = tuner_service_tone_enabled();

    button_style(&s_ui.tone_play, enabled, COLOR_GREEN);
    label_set(s_ui.tone_play.label, enabled ? "STOP" : "PLAY");
    tone_update_state();

    for (int i = 0; i < 4; i++) {
        button_style(&s_ui.tone_wave[i],
                     (tone_wave_t)i == s_ui.tone_waveform, COLOR_CYAN);
    }
    button_style(&s_ui.tone_sweep, s_ui.sweep_on, COLOR_MAGENTA);
    tone_update_values();
}

static void tone_toggle_cb(lv_event_t *e)
{
    (void)e;
    const bool enable = !tuner_service_tone_enabled();
    if (!enable && s_ui.sweep_on) {
        tuner_service_stop_sweep();
        tuner_service_set_tone_frequency((float)s_ui.tone_hz);
        s_ui.sweep_on = false;
    }
    const esp_err_t err = tuner_service_set_tone_enabled(enable);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "tone enable failed: %s", esp_err_to_name(err));
    }
    tone_refresh();
}

static void tone_wave_cb(lv_event_t *e)
{
    const tone_wave_t wave =
        (tone_wave_t)(intptr_t)lv_event_get_user_data(e);
    tuner_service_set_tone_waveform(wave);
    s_ui.tone_waveform = wave;
    tone_refresh();
}

static void tone_freq_cb(lv_event_t *e)
{
    lv_obj_t *slider = lv_event_get_target(e);
    const int value = (int)lv_slider_get_value(slider);

    s_ui.tone_hz = value;
    tuner_service_set_tone_frequency((float)value);
    tone_update_state();
    tone_update_values();
}

static void tone_amp_cb(lv_event_t *e)
{
    lv_obj_t *slider = lv_event_get_target(e);
    const int value = (int)lv_slider_get_value(slider);

    s_ui.tone_amp = value;
    tuner_service_set_tone_amplitude((float)value / 100.0f);
    tone_update_state();
    tone_update_values();
}

static void tone_sweep_cb(lv_event_t *e)
{
    (void)e;
    if (!s_ui.sweep_on) {
        const esp_err_t err = tuner_service_start_sweep(
            TONE_SWEEP_LINEAR, 100.0f, 1000.0f, 5.0f);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "sweep start failed: %s", esp_err_to_name(err));
            return;
        }
        tuner_service_set_tone_enabled(true);
        s_ui.sweep_on = true;
        s_ui.sweep_end_ms = lv_tick_get() + 5000u;
    } else {
        /* One-shot sweep: always end in silence. */
        tuner_service_set_tone_enabled(false);
        tuner_service_stop_sweep();
        tuner_service_set_tone_frequency((float)s_ui.tone_hz);
        s_ui.sweep_on = false;
    }
    tone_refresh();
}

static void build_tone_page(lv_obj_t *page)
{
    lv_obj_t *card = ui_theme_card(page, 32, 0, 560, PAGE_H);
    ui_theme_label(card, "TONE GENERATOR", &lv_font_montserrat_20, COLOR_CYAN,
                   24, 18);
    s_ui.tone_state = ui_theme_label(card, "OUTPUT OFF",
                                     &lv_font_montserrat_14, COLOR_MUTED,
                                     24, 46);

    make_button(card, "PLAY", 24, 76, 200, 64, &lv_font_montserrat_28,
                tone_toggle_cb, NULL, &s_ui.tone_play);

    ui_theme_label(card, "WAVEFORM", &lv_font_montserrat_14, COLOR_MUTED,
                   24, 152);
    static const char *const wave_names[4] = {"SINE", "SQUARE", "TRIANGLE",
                                              "SAW"};
    for (int i = 0; i < 4; i++) {
        make_button(card, wave_names[i], 24 + i * 130, 178, 122, 44,
                    &lv_font_montserrat_14, tone_wave_cb,
                    (void *)(intptr_t)i, &s_ui.tone_wave[i]);
    }

    ui_theme_label(card, "FREQUENCY", &lv_font_montserrat_14, COLOR_MUTED,
                   24, 236);
    s_ui.tone_freq_value = ui_theme_label(card, "440 Hz",
                                          &lv_font_montserrat_20, COLOR_TEXT,
                                          412, 232);
    lv_obj_set_width(s_ui.tone_freq_value, 124);
    lv_obj_set_style_text_align(s_ui.tone_freq_value, LV_TEXT_ALIGN_RIGHT, 0);
    s_ui.tone_freq_slider = make_slider(card, 24, 266, 512, 16, 40, 1200,
                                        440, tone_freq_cb, NULL);

    ui_theme_label(card, "AMPLITUDE", &lv_font_montserrat_14, COLOR_MUTED,
                   24, 298);
    s_ui.tone_amp_value = ui_theme_label(card, "30%", &lv_font_montserrat_20,
                                         COLOR_TEXT, 412, 294);
    lv_obj_set_width(s_ui.tone_amp_value, 124);
    lv_obj_set_style_text_align(s_ui.tone_amp_value, LV_TEXT_ALIGN_RIGHT, 0);
    s_ui.tone_amp_slider = make_slider(card, 24, 328, 512, 16, 0, 100, 30,
                                       tone_amp_cb, NULL);

    make_button(card, "SWEEP 100 > 1000 Hz / 5 s", 24, 358, 512, 44,
                &lv_font_montserrat_14, tone_sweep_cb, NULL,
                &s_ui.tone_sweep);
    ui_theme_label(card, "ES8311 output / tone + metronome share the mixer",
                   &lv_font_montserrat_14, COLOR_DIM, 24, 410);

    lv_obj_t *info = ui_theme_card(page, 612, 0, 380, PAGE_H);
    ui_theme_label(info, "OUTPUT", &lv_font_montserrat_20, COLOR_CYAN, 20, 18);
    s_ui.tone_big = ui_theme_label(info, "440 Hz", &lv_font_montserrat_48,
                                   COLOR_TEXT, 20, 50);
    lv_obj_set_width(s_ui.tone_big, 340);
    lv_label_set_long_mode(s_ui.tone_big, LV_LABEL_LONG_CLIP);
    ui_theme_label(info, "ES8311 SPEAKER", &lv_font_montserrat_20,
                   COLOR_GREEN, 20, 124);
    ui_theme_label(info,
                   "Tone and metronome share one output mixer.\n"
                   "The tone lane has priority and ducks the\n"
                   "metronome by 9 dB when both are active.\n"
                   "5 ms ramps keep frequency, waveform and\n"
                   "level changes click-free.",
                   &lv_font_montserrat_14, COLOR_MUTED, 20, 160);
    ui_theme_label(info, "Sweep: 100 -> 1000 Hz over 5 s.",
                   &lv_font_montserrat_14, COLOR_CYAN, 20, 280);
    ui_theme_label(info,
                   "The tone stops when the app is closed;\n"
                   "switching tabs leaves it running.",
                   &lv_font_montserrat_14, COLOR_DIM, 20, 316);
}

/* ------------------------------------------------------------------------- */
/* Metronome page                                                            */
/* ------------------------------------------------------------------------- */

static const char *meter_name(metronome_meter_t meter)
{
    switch (meter) {
    case METRONOME_METER_2_4:
        return "2/4";
    case METRONOME_METER_3_4:
        return "3/4";
    case METRONOME_METER_6_8:
        return "6/8";
    case METRONOME_METER_4_4:
    default:
        return "4/4";
    }
}

static void metro_update_labels(void)
{
    char buf[40];

    snprintf(buf, sizeof(buf), "%d BPM", s_ui.bpm);
    label_set(s_ui.metro_bpm_value, buf);
    label_set(s_ui.metro_bpm_big, buf);

    snprintf(buf, sizeof(buf), "%s  /  accent %d", meter_name(s_ui.meter),
             s_ui.accent);
    label_set(s_ui.metro_info, buf);

    snprintf(buf, sizeof(buf), "BEAT %d", s_ui.accent);
    label_set(s_ui.metro_accent.label, buf);

    if (tuner_service_metronome_enabled()) {
        snprintf(buf, sizeof(buf), "RUNNING %d BPM", s_ui.bpm);
        label_set(s_ui.metro_state, buf);
    } else {
        label_set(s_ui.metro_state, "STOPPED");
    }
}

static void metro_apply_meter(void)
{
    static const metronome_meter_t meters[4] = {
        METRONOME_METER_2_4, METRONOME_METER_3_4,
        METRONOME_METER_4_4, METRONOME_METER_6_8,
    };
    const int beats = (int)s_ui.meter;

    for (int i = 0; i < 4; i++) {
        button_style(&s_ui.metro_meter[i], meters[i] == s_ui.meter,
                     COLOR_CYAN);
    }
    for (int i = 0; i < 6; i++) {
        if (i < beats) {
            lv_obj_remove_flag(s_ui.metro_circle[i], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_ui.metro_circle[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
    s_ui.visual_beat = -2;
    metro_update_labels();
}

static void metro_refresh(void)
{
    const bool running = tuner_service_metronome_enabled();

    button_style(&s_ui.metro_play, running, COLOR_GREEN);
    label_set(s_ui.metro_play.label, running ? "STOP" : "START");
    lv_obj_set_style_text_color(s_ui.metro_state,
                                lv_color_hex(running ? COLOR_GREEN
                                                     : COLOR_MUTED), 0);

    for (int i = 0; i < 2; i++) {
        button_style(&s_ui.metro_step[i],
                     (i == 0 ? 1 : 5) == s_ui.bpm_step, COLOR_CYAN);
    }
    for (int i = 0; i < 2; i++) {
        button_style(&s_ui.metro_click[i],
                     (metronome_click_wave_t)i == s_ui.click_wave,
                     COLOR_CYAN);
    }
    metro_apply_meter();
}

static void metro_play_cb(lv_event_t *e)
{
    (void)e;
    const bool enable = !tuner_service_metronome_enabled();
    const esp_err_t err = tuner_service_set_metronome_enabled(enable);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "metronome enable failed: %s", esp_err_to_name(err));
    }
    s_ui.visual_beat = -2;
    metro_refresh();
}

static void metro_bpm_apply(int bpm)
{
    if (bpm < 30) {
        bpm = 30;
    } else if (bpm > 240) {
        bpm = 240;
    }
    s_ui.bpm = bpm;
    tuner_service_set_metronome_bpm((float)bpm);
    metro_update_labels();
}

static void metro_bpm_slider_cb(lv_event_t *e)
{
    if (s_ui.applying) {
        return;
    }
    lv_obj_t *slider = lv_event_get_target(e);
    metro_bpm_apply((int)lv_slider_get_value(slider));
}

static void metro_step_cb(lv_event_t *e)
{
    const int direction = (int)(intptr_t)lv_event_get_user_data(e);

    metro_bpm_apply(s_ui.bpm + direction * s_ui.bpm_step);
    s_ui.applying = true;
    lv_slider_set_value(s_ui.metro_bpm_slider, s_ui.bpm, LV_ANIM_OFF);
    s_ui.applying = false;
}

static void metro_step_size_cb(lv_event_t *e)
{
    const int step = (int)(intptr_t)lv_event_get_user_data(e);
    s_ui.bpm_step = step;
    metro_refresh();
}

static void metro_meter_cb(lv_event_t *e)
{
    const metronome_meter_t meter =
        (metronome_meter_t)(intptr_t)lv_event_get_user_data(e);
    const int beats = (int)meter;

    s_ui.meter = meter;
    if (s_ui.accent > beats) {
        s_ui.accent = beats;
    }
    tuner_service_set_metronome_meter(meter);
    tuner_service_set_metronome_accent((uint8_t)s_ui.accent);
    metro_apply_meter();
}

static void metro_accent_cb(lv_event_t *e)
{
    (void)e;
    s_ui.accent++;
    if (s_ui.accent > (int)s_ui.meter) {
        s_ui.accent = 1;
    }
    tuner_service_set_metronome_accent((uint8_t)s_ui.accent);
    s_ui.visual_beat = -2;
    metro_update_labels();
}

static void metro_click_cb(lv_event_t *e)
{
    const metronome_click_wave_t wave =
        (metronome_click_wave_t)(intptr_t)lv_event_get_user_data(e);
    tuner_service_set_metronome_click(wave);
    s_ui.click_wave = wave;
    metro_refresh();
}

static void metro_style_circle(int index, int beat, bool accent)
{
    const bool active = (index + 1) == beat;
    uint32_t bg = COLOR_BG;
    uint32_t border = COLOR_BORDER;
    uint32_t text = COLOR_MUTED;

    if (active) {
        bg = accent ? COLOR_AMBER : COLOR_CYAN;
        border = bg;
        text = COLOR_BG;
    }
    lv_obj_set_style_bg_color(s_ui.metro_circle[index], lv_color_hex(bg), 0);
    lv_obj_set_style_border_color(s_ui.metro_circle[index],
                                  lv_color_hex(border), 0);
    lv_obj_set_style_text_color(s_ui.metro_circle_text[index],
                                lv_color_hex(text), 0);
}

static void update_metronome_visual(void)
{
    const int beats = (int)s_ui.meter;
    uint32_t bars = 0;
    uint8_t in_bar = 0;
    char buf[40];

    tuner_service_get_metronome_state(NULL, &bars, &in_bar);
    const bool running = tuner_service_metronome_enabled();
    const bool active = running && in_bar >= 1 && in_bar <= beats;

    if (active) {
        snprintf(buf, sizeof(buf), "beat %u / bar %u", (unsigned)in_bar,
                 (unsigned)(bars + 1u));
        label_set(s_ui.metro_count, buf);
    } else {
        label_set(s_ui.metro_count, "beat - / bar -");
    }

    const int beat = active ? (int)in_bar : 0;
    if (beat != s_ui.visual_beat) {
        for (int i = 0; i < 6; i++) {
            metro_style_circle(i, beat, (i + 1) == s_ui.accent);
        }
        s_ui.visual_beat = beat;
    }
}

static void build_metronome_page(lv_obj_t *page)
{
    lv_obj_t *card = ui_theme_card(page, 32, 0, 560, PAGE_H);
    ui_theme_label(card, "METRONOME", &lv_font_montserrat_20, COLOR_CYAN,
                   24, 18);
    s_ui.metro_state = ui_theme_label(card, "STOPPED",
                                      &lv_font_montserrat_14, COLOR_MUTED,
                                      24, 46);

    make_button(card, "START", 24, 76, 200, 64, &lv_font_montserrat_28,
                metro_play_cb, NULL, &s_ui.metro_play);

    ui_theme_label(card, "TEMPO", &lv_font_montserrat_14, COLOR_MUTED,
                   24, 158);
    s_ui.metro_bpm_value = ui_theme_label(card, "120 BPM",
                                          &lv_font_montserrat_20, COLOR_TEXT,
                                          412, 152);
    lv_obj_set_width(s_ui.metro_bpm_value, 124);
    lv_obj_set_style_text_align(s_ui.metro_bpm_value, LV_TEXT_ALIGN_RIGHT, 0);

    make_button(card, "-", 24, 184, 56, 44, &lv_font_montserrat_20,
                metro_step_cb, (void *)(intptr_t)-1, NULL);
    s_ui.metro_bpm_slider = make_slider(card, 92, 198, 376, 16, 30, 240, 120,
                                        metro_bpm_slider_cb, NULL);
    make_button(card, "+", 480, 184, 56, 44, &lv_font_montserrat_20,
                metro_step_cb, (void *)(intptr_t)1, NULL);

    ui_theme_label(card, "STEP", &lv_font_montserrat_14, COLOR_MUTED,
                   24, 240);
    make_button(card, "1", 88, 234, 56, 36, &lv_font_montserrat_14,
                metro_step_size_cb, (void *)(intptr_t)1,
                &s_ui.metro_step[0]);
    make_button(card, "5", 152, 234, 56, 36, &lv_font_montserrat_14,
                metro_step_size_cb, (void *)(intptr_t)5,
                &s_ui.metro_step[1]);
    ui_theme_label(card, "BPM step for - / +", &lv_font_montserrat_14,
                   COLOR_DIM, 220, 242);

    ui_theme_label(card, "METER", &lv_font_montserrat_14, COLOR_MUTED,
                   24, 282);
    static const char *const meter_names[4] = {"2/4", "3/4", "4/4", "6/8"};
    static const int meter_values[4] = {2, 3, 4, 6};
    for (int i = 0; i < 4; i++) {
        make_button(card, meter_names[i], 24 + i * 130, 310, 122, 40,
                    &lv_font_montserrat_14, metro_meter_cb,
                    (void *)(intptr_t)meter_values[i],
                    &s_ui.metro_meter[i]);
    }

    ui_theme_label(card, "ACCENT BEAT", &lv_font_montserrat_14, COLOR_MUTED,
                   24, 364);
    make_button(card, "BEAT 1", 24, 382, 160, 40, &lv_font_montserrat_14,
                metro_accent_cb, NULL, &s_ui.metro_accent);
    ui_theme_label(card, "cycles 1 .. beats per bar", &lv_font_montserrat_14,
                   COLOR_DIM, 196, 392);

    lv_obj_t *visual = ui_theme_card(page, 612, 0, 380, PAGE_H);
    ui_theme_label(visual, "VISUAL BEAT", &lv_font_montserrat_20, COLOR_CYAN,
                   20, 18);

    for (int i = 0; i < 6; i++) {
        lv_obj_t *circle = lv_obj_create(visual);
        lv_obj_set_pos(circle, 20 + i * 56, 58);
        lv_obj_set_size(circle, 48, 48);
        lv_obj_remove_flag(circle, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_remove_flag(circle, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_bg_color(circle, lv_color_hex(COLOR_BG), 0);
        lv_obj_set_style_bg_opa(circle, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(circle, 1, 0);
        lv_obj_set_style_border_color(circle, lv_color_hex(COLOR_BORDER), 0);
        lv_obj_set_style_radius(circle, 24, 0);
        lv_obj_set_style_pad_all(circle, 0, 0);

        lv_obj_t *number = lv_label_create(circle);
        lv_label_set_text_fmt(number, "%d", i + 1);
        lv_obj_set_style_text_font(number, &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_color(number, lv_color_hex(COLOR_MUTED), 0);
        lv_obj_center(number);

        s_ui.metro_circle[i] = circle;
        s_ui.metro_circle_text[i] = number;
    }

    s_ui.metro_count = ui_theme_label(visual, "beat - / bar -",
                                      &lv_font_montserrat_20, COLOR_TEXT,
                                      20, 122);
    s_ui.metro_bpm_big = ui_theme_label(visual, "120 BPM",
                                        &lv_font_montserrat_28, COLOR_TEXT,
                                        20, 158);
    s_ui.metro_info = ui_theme_label(visual, "4/4  /  accent 1",
                                     &lv_font_montserrat_14, COLOR_MUTED,
                                     20, 200);

    ui_theme_label(visual, "CLICK", &lv_font_montserrat_14, COLOR_MUTED,
                   20, 250);
    make_button(visual, "SINE", 20, 274, 120, 40, &lv_font_montserrat_14,
                metro_click_cb, (void *)(intptr_t)METRONOME_CLICK_SINE,
                &s_ui.metro_click[0]);
    make_button(visual, "SQUARE", 148, 274, 120, 40, &lv_font_montserrat_14,
                metro_click_cb, (void *)(intptr_t)METRONOME_CLICK_SQUARE,
                &s_ui.metro_click[1]);

    ui_theme_label(visual,
                   "Click and visuals share the audio clock.\n"
                   "Accent clicks are brighter and higher.",
                   &lv_font_montserrat_14, COLOR_DIM, 20, 336);
    ui_theme_label(visual, "ES8311 output / shared mixer",
                   &lv_font_montserrat_14, COLOR_DIM, 20, 392);
}

/* ------------------------------------------------------------------------- */
/* App lifecycle                                                             */
/* ------------------------------------------------------------------------- */

static void tuner_timer_cb(lv_timer_t *timer)
{
    (void)timer;

    /* One-shot sweep: when the ramp finishes, go silent instead of leaving
     * the tone parked at the final frequency. */
    if (s_ui.sweep_on &&
        (int32_t)(lv_tick_get() - s_ui.sweep_end_ms) >= 0) {
        tuner_service_set_tone_enabled(false);
        tuner_service_stop_sweep();
        tuner_service_set_tone_frequency((float)s_ui.tone_hz);
        s_ui.sweep_on = false;
        tone_refresh();
    }

    if (s_ui.tab == TAB_TUNER) {
        update_tuner_view();
        update_waveform();
    } else if (s_ui.tab == TAB_METRONOME) {
        update_metronome_visual();
    }
}

static void show_mount_error(lv_obj_t *parent, esp_err_t err)
{
    char text[96];

    snprintf(text, sizeof(text), "Tuner service failed to start: %s",
             esp_err_to_name(err));
    lv_obj_t *label = ui_theme_label(parent, text, &lv_font_montserrat_20,
                                     COLOR_RED, 32, 32);
    lv_obj_set_width(label, 960);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
}

esp_err_t tuner_mount(lv_obj_t *parent)
{
    if (parent == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(&s_ui, 0, sizeof(s_ui));
    s_ui.tone_hz = 440;
    s_ui.tone_amp = 30;
    s_ui.tone_waveform = TONE_WAVE_SINE;
    s_ui.bpm = 120;
    s_ui.bpm_step = 1;
    s_ui.meter = METRONOME_METER_4_4;
    s_ui.accent = 1;
    s_ui.click_wave = METRONOME_CLICK_SINE;
    s_ui.visual_beat = -1;
    s_ui.tab = TAB_TUNER;

    const esp_err_t err = tuner_service_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "service start failed: %s", esp_err_to_name(err));
        show_mount_error(parent, err);
        return err;
    }

    s_ui.root = lv_obj_create(parent);
    lv_obj_set_pos(s_ui.root, 0, 0);
    lv_obj_set_size(s_ui.root, BSP_LCD_H_RES, BSP_LCD_V_RES);
    lv_obj_remove_flag(s_ui.root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(s_ui.root, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_ui.root, 0, 0);
    lv_obj_set_style_radius(s_ui.root, 0, 0);
    lv_obj_set_style_pad_all(s_ui.root, 0, 0);

    ui_theme_label(s_ui.root, "TUNER LAB", &lv_font_montserrat_28,
                   COLOR_TEXT, 32, 20);
    ui_theme_label(s_ui.root, "Pitch / tone / metronome  -  ES7210 in, ES8311 out",
                   &lv_font_montserrat_14, COLOR_MUTED, 34, 58);

    static const char *const tab_names[TAB_COUNT] = {"TUNER", "TONE",
                                                     "METRONOME"};
    for (int i = 0; i < TAB_COUNT; i++) {
        make_button(s_ui.root, tab_names[i], 32 + i * 158, 84, 150, 40,
                    &lv_font_montserrat_14, tab_clicked_cb,
                    (void *)(intptr_t)i, &s_ui.tabs[i]);
    }

    ui_theme_label(s_ui.root, "OUTPUT VOLUME", &lv_font_montserrat_14,
                   COLOR_MUTED, 620, 82);
    s_ui.volume_value = ui_theme_label(s_ui.root, "60%",
                                       &lv_font_montserrat_14, COLOR_TEXT,
                                       930, 82);
    lv_obj_set_width(s_ui.volume_value, 60);
    lv_obj_set_style_text_align(s_ui.volume_value, LV_TEXT_ALIGN_RIGHT, 0);
    const int volume = tuner_service_get_output_volume();
    s_ui.volume_slider = make_slider(s_ui.root, 620, 108, 300, 16, 0, 100,
                                     volume, volume_cb, NULL);
    lv_label_set_text_fmt(s_ui.volume_value, "%d%%", volume);

    lv_obj_t *tuner_page = make_page(s_ui.root, TAB_TUNER);
    lv_obj_t *tone_page = make_page(s_ui.root, TAB_TONE);
    lv_obj_t *metro_page = make_page(s_ui.root, TAB_METRONOME);

    build_tuner_page(tuner_page);
    build_tone_page(tone_page);
    build_metronome_page(metro_page);

    for (int i = 0; i < TAB_COUNT; i++) {
        button_style(&s_ui.tabs[i], (tuner_tab_t)i == TAB_TUNER, COLOR_CYAN);
        if ((tuner_tab_t)i != TAB_TUNER) {
            lv_obj_add_flag(s_ui.pages[i], LV_OBJ_FLAG_HIDDEN);
        }
    }

    tone_refresh();
    metro_refresh();
    update_tuner_view();
    update_waveform();
    update_metronome_visual();

    s_timer = lv_timer_create(tuner_timer_cb, TUNER_TIMER_MS, NULL);
    if (s_timer == NULL) {
        tuner_service_stop();
        memset(&s_ui, 0, sizeof(s_ui));
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "mounted");
    return ESP_OK;
}

void tuner_unmount(void)
{
    if (s_timer != NULL) {
        lv_timer_delete(s_timer);
        s_timer = NULL;
    }
    tuner_service_stop();
    memset(&s_ui, 0, sizeof(s_ui));
    ESP_LOGI(TAG, "unmounted");
}

const app_desc_t tuner_app = {
    .id = "tuner",
    .title = "Tuner",
    .description = "Pitch, tone generator and metronome.",
    .mount = tuner_mount,
    .unmount = tuner_unmount,
};
