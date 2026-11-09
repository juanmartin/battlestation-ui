#include "tuner_engine.h"

#include <math.h>
#include <string.h>

#define TUNER_MIN_SAMPLE_RATE 4000u
#define TUNER_MAX_SAMPLE_RATE 192000u

struct tuner_engine {
    tuner_free_fn free_fn;
    uint32_t sample_rate;
    uint32_t window_size;
    uint32_t hop_size;
    uint32_t tau_min;
    uint32_t tau_max;
    uint32_t work_len;
    float min_frequency_hz;
    float max_frequency_hz;
    float a4_hz;
    float threshold;
    float min_rms;
    float min_confidence;

    int16_t *ring;
    float *window;
    float *cmnd; /* difference function, then cumulative-mean normalized */

    uint32_t ring_pos;
    uint32_t ring_fill;
    uint32_t since_analysis;
    uint32_t updates;

    audio_seqlock_t lock;
    tuner_result_t result;
};

static const char *const s_note_names[12] = {
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B",
};

static bool finite_positive(float value)
{
    return isfinite(value) && value > 0.0f;
}

static void publish_result(tuner_engine_t *engine, const tuner_result_t *result)
{
    audio_seqlock_write_begin(&engine->lock);
    engine->result = *result;
    audio_seqlock_write_end(&engine->lock);
}

static void publish_invalid(tuner_engine_t *engine)
{
    tuner_result_t result = {0};
    result.midi_note = -1;
    result.note_name[0] = '-';
    result.note_name[1] = '-';
    result.updates = engine->updates;
    publish_result(engine, &result);
}

static float interpolated_cost(const tuner_engine_t *engine, float tau)
{
    uint32_t index = (uint32_t)tau;
    float frac = tau - (float)index;
    const float *x = engine->window;
    const float *delayed = x + index;
    float sum = 0.0f;

    for (uint32_t j = 0; j < engine->work_len; j++) {
        float value = delayed[j] + (delayed[j + 1u] - delayed[j]) * frac;
        float diff = x[j] - value;
        sum += diff * diff;
    }
    return sum;
}

/*
 * Refine the integer-lag YIN minimum with a golden-section search over
 * fractional lags. This removes the waveform-dependent bias that a three-point
 * parabolic fit shows on square/saw inputs.
 */
static float refine_fractional_lag(const tuner_engine_t *engine, float center)
{
    float lo = center - 0.75f;
    float hi = center + 0.75f;
    float min_tau = (float)engine->tau_min - 1.0f;
    float max_tau = (float)engine->tau_max;

    if (lo < min_tau) {
        lo = min_tau;
    }
    if (hi > max_tau) {
        hi = max_tau;
    }
    if (!(hi > lo)) {
        return center;
    }

    const float golden = 0.6180339887498949f;
    float c = hi - golden * (hi - lo);
    float d = lo + golden * (hi - lo);
    float fc = interpolated_cost(engine, c);
    float fd = interpolated_cost(engine, d);

    for (int i = 0; i < 24; i++) {
        if (fc < fd) {
            hi = d;
            d = c;
            fd = fc;
            c = hi - golden * (hi - lo);
            fc = interpolated_cost(engine, c);
        } else {
            lo = c;
            c = d;
            fc = fd;
            d = lo + golden * (hi - lo);
            fd = interpolated_cost(engine, d);
        }
    }
    return 0.5f * (lo + hi);
}

static void fill_note_fields(tuner_result_t *result, float frequency_hz,
                             float a4_hz)
{
    double midi_exact =
        69.0 + 12.0 * log2((double)frequency_hz / (double)a4_hz);
    int midi = (int)floor(midi_exact + 0.5);

    if (midi < 0) {
        midi = 0;
    } else if (midi > 127) {
        midi = 127;
    }

    int pitch_class = midi % 12;
    double note_hz =
        (double)a4_hz * exp2(((double)midi - 69.0) / 12.0);
    double cents = 1200.0 * log2((double)frequency_hz / note_hz);
    if (!isfinite(cents)) {
        cents = 0.0;
    }

    result->midi_note = (int16_t)midi;
    result->octave = (int8_t)(midi / 12 - 1);
    result->cents = (float)cents;

    const char *name = s_note_names[pitch_class];
    size_t length = strlen(name);
    if (length > TUNER_ENGINE_NOTE_NAME_MAX - 1u) {
        length = TUNER_ENGINE_NOTE_NAME_MAX - 1u;
    }
    memset(result->note_name, 0, sizeof(result->note_name));
    memcpy(result->note_name, name, length);
}

static void analyze(tuner_engine_t *engine)
{
    const uint32_t n = engine->window_size;
    const uint32_t work = engine->work_len;

    size_t index = engine->ring_pos;
    double mean_sum = 0.0;
    for (uint32_t i = 0; i < n; i++) {
        mean_sum += (double)engine->ring[index];
        index++;
        if (index >= n) {
            index = 0;
        }
    }
    float mean = (float)(mean_sum / ((double)n * 32768.0));

    index = engine->ring_pos;
    double sum_sq = 0.0;
    for (uint32_t i = 0; i < n; i++) {
        float value = (float)engine->ring[index] / 32768.0f - mean;
        engine->window[i] = value;
        sum_sq += (double)value * (double)value;
        index++;
        if (index >= n) {
            index = 0;
        }
    }

    engine->updates++;
    double rms = sqrt(sum_sq / (double)n);
    if (!isfinite(rms) || rms < (double)engine->min_rms) {
        publish_invalid(engine);
        return;
    }

    const float *x = engine->window;
    for (uint32_t tau = 1; tau <= engine->tau_max; tau++) {
        const float *delayed = x + tau;
        float sum = 0.0f;
        for (uint32_t j = 0; j < work; j++) {
            float diff = x[j] - delayed[j];
            sum += diff * diff;
        }
        engine->cmnd[tau] = sum;
    }

    float running = 0.0f;
    for (uint32_t tau = 1; tau <= engine->tau_max; tau++) {
        running += engine->cmnd[tau];
        if (running > 1e-20f) {
            engine->cmnd[tau] =
                engine->cmnd[tau] * (float)tau / running;
        } else {
            engine->cmnd[tau] = 1.0f;
        }
    }

    const uint32_t search_last =
        (engine->tau_max > engine->tau_min) ? engine->tau_max - 1u
                                            : engine->tau_max;
    int best = -1;
    bool found = false;
    for (uint32_t tau = engine->tau_min; tau <= search_last; tau++) {
        if (engine->cmnd[tau] < engine->threshold) {
            while (tau < search_last &&
                   engine->cmnd[tau + 1u] < engine->cmnd[tau]) {
                tau++;
            }
            best = (int)tau;
            found = true;
            break;
        }
    }
    if (best < 0) {
        uint32_t fallback = engine->tau_min;
        for (uint32_t tau = engine->tau_min + 1u; tau <= search_last; tau++) {
            if (engine->cmnd[tau] < engine->cmnd[fallback]) {
                fallback = tau;
            }
        }
        best = (int)fallback;
    }

    float a = engine->cmnd[best - 1];
    float b = engine->cmnd[best];
    float c = engine->cmnd[best + 1];
    float denom = a - 2.0f * b + c;
    float delta = 0.0f;
    if (fabsf(denom) > 1e-12f) {
        delta = 0.5f * (a - c) / denom;
        if (delta > 0.75f) {
            delta = 0.75f;
        } else if (delta < -0.75f) {
            delta = -0.75f;
        }
    }

    float tau = refine_fractional_lag(engine, (float)best + delta);
    float frequency = 0.0f;
    if (tau > 0.5f) {
        frequency = (float)engine->sample_rate / tau;
    }

    float confidence = 1.0f - b;
    if (!isfinite(confidence)) {
        confidence = 0.0f;
    } else if (confidence < 0.0f) {
        confidence = 0.0f;
    } else if (confidence > 1.0f) {
        confidence = 1.0f;
    }

    tuner_result_t result = {0};
    result.frequency_hz = frequency;
    result.confidence = confidence;
    result.midi_note = -1;
    result.note_name[0] = '-';
    result.note_name[1] = '-';
    result.updates = engine->updates;

    if (finite_positive(frequency) && isfinite(confidence)) {
        fill_note_fields(&result, frequency, engine->a4_hz);
        result.valid = found && confidence >= engine->min_confidence &&
                       frequency >= engine->min_frequency_hz &&
                       frequency <= engine->max_frequency_hz;
    }

    publish_result(engine, &result);
}

tuner_engine_t *tuner_engine_create(const tuner_engine_config_t *cfg)
{
    if (cfg == NULL || cfg->alloc == NULL || cfg->free == NULL) {
        return NULL;
    }

    uint32_t sample_rate = cfg->sample_rate;
    if (sample_rate == 0) {
        sample_rate = TUNER_ENGINE_DEFAULT_SAMPLE_RATE;
    }
    if (sample_rate < TUNER_MIN_SAMPLE_RATE ||
        sample_rate > TUNER_MAX_SAMPLE_RATE) {
        return NULL;
    }

    uint32_t window_size = cfg->window_size;
    if (window_size == 0) {
        window_size = (sample_rate >= 32000u) ? 4096u
                                              : TUNER_ENGINE_DEFAULT_WINDOW;
    }
    if (window_size < 128u || window_size > TUNER_ENGINE_MAX_WINDOW) {
        return NULL;
    }

    float min_hz = cfg->min_frequency_hz;
    if (!finite_positive(min_hz)) {
        min_hz = TUNER_ENGINE_DEFAULT_MIN_HZ;
    }
    float max_hz = cfg->max_frequency_hz;
    if (!finite_positive(max_hz)) {
        max_hz = TUNER_ENGINE_DEFAULT_MAX_HZ;
    }
    if (min_hz < 1.0f || max_hz <= min_hz ||
        max_hz >= (float)sample_rate * 0.5f) {
        return NULL;
    }

    uint32_t tau_min = (uint32_t)floorf((float)sample_rate / max_hz);
    if (tau_min < 2u) {
        tau_min = 2u;
    }
    uint32_t tau_max = (uint32_t)ceilf((float)sample_rate / min_hz);
    if (tau_max < tau_min + 2u || tau_max + 65u >= window_size) {
        return NULL;
    }

    float a4_hz = cfg->a4_hz;
    if (!finite_positive(a4_hz)) {
        a4_hz = TUNER_ENGINE_DEFAULT_A4_HZ;
    }
    if (a4_hz < 100.0f || a4_hz > 1000.0f) {
        return NULL;
    }

    float threshold = cfg->threshold;
    if (!finite_positive(threshold)) {
        threshold = TUNER_ENGINE_DEFAULT_THRESHOLD;
    }
    if (threshold > 1.0f) {
        threshold = 1.0f;
    }

    float min_rms = cfg->min_rms;
    if (!finite_positive(min_rms)) {
        min_rms = TUNER_ENGINE_DEFAULT_MIN_RMS;
    }
    if (min_rms > 1.0f) {
        min_rms = 1.0f;
    }

    float min_confidence = cfg->min_confidence;
    if (!finite_positive(min_confidence)) {
        min_confidence = TUNER_ENGINE_DEFAULT_CONFIDENCE;
    }
    if (min_confidence > 1.0f) {
        min_confidence = 1.0f;
    }

    uint32_t hop_size = cfg->hop_size;
    if (hop_size == 0) {
        hop_size = window_size / 2u;
    }
    if (hop_size == 0) {
        hop_size = 1;
    }
    if (hop_size > window_size) {
        hop_size = window_size;
    }

    tuner_engine_t *engine = cfg->alloc(sizeof(*engine));
    if (engine == NULL) {
        return NULL;
    }
    memset(engine, 0, sizeof(*engine));
    engine->free_fn = cfg->free;

    engine->ring = cfg->alloc((size_t)window_size * sizeof(engine->ring[0]));
    engine->window =
        cfg->alloc((size_t)window_size * sizeof(engine->window[0]));
    engine->cmnd = cfg->alloc(((size_t)tau_max + 2u) *
                              sizeof(engine->cmnd[0]));
    if (engine->ring == NULL || engine->window == NULL ||
        engine->cmnd == NULL) {
        if (engine->ring != NULL) {
            cfg->free(engine->ring);
        }
        if (engine->window != NULL) {
            cfg->free(engine->window);
        }
        if (engine->cmnd != NULL) {
            cfg->free(engine->cmnd);
        }
        cfg->free(engine);
        return NULL;
    }

    engine->sample_rate = sample_rate;
    engine->window_size = window_size;
    engine->hop_size = hop_size;
    engine->tau_min = tau_min;
    engine->tau_max = tau_max;
    engine->work_len = window_size - tau_max - 1u;
    engine->min_frequency_hz = min_hz;
    engine->max_frequency_hz = max_hz;
    engine->a4_hz = a4_hz;
    engine->threshold = threshold;
    engine->min_rms = min_rms;
    engine->min_confidence = min_confidence;

    audio_seqlock_init(&engine->lock);
    tuner_engine_reset(engine);
    return engine;
}

void tuner_engine_destroy(tuner_engine_t *engine)
{
    if (engine == NULL) {
        return;
    }
    tuner_free_fn free_fn = engine->free_fn;
    free_fn(engine->ring);
    free_fn(engine->window);
    free_fn(engine->cmnd);
    free_fn(engine);
}

void tuner_engine_reset(tuner_engine_t *engine)
{
    if (engine == NULL) {
        return;
    }
    memset(engine->ring, 0, (size_t)engine->window_size *
                               sizeof(engine->ring[0]));
    memset(engine->window, 0,
           (size_t)engine->window_size * sizeof(engine->window[0]));
    memset(engine->cmnd, 0,
           ((size_t)engine->tau_max + 2u) * sizeof(engine->cmnd[0]));
    engine->ring_pos = 0;
    engine->ring_fill = 0;
    engine->since_analysis = 0;
    engine->updates = 0;

    audio_seqlock_write_begin(&engine->lock);
    memset(&engine->result, 0, sizeof(engine->result));
    engine->result.midi_note = -1;
    engine->result.note_name[0] = '-';
    engine->result.note_name[1] = '-';
    audio_seqlock_write_end(&engine->lock);
}

bool tuner_engine_process_block(tuner_engine_t *engine,
                                const int16_t *samples,
                                size_t frames)
{
    if (engine == NULL || (samples == NULL && frames > 0)) {
        return false;
    }

    bool analyzed = false;
    for (size_t i = 0; i < frames; i++) {
        engine->ring[engine->ring_pos] = samples[i];
        engine->ring_pos++;
        if (engine->ring_pos >= engine->window_size) {
            engine->ring_pos = 0;
        }
        if (engine->ring_fill < engine->window_size) {
            engine->ring_fill++;
        }
        engine->since_analysis++;

        if (engine->ring_fill >= engine->window_size &&
            engine->since_analysis >= engine->hop_size) {
            analyze(engine);
            engine->since_analysis = 0;
            analyzed = true;
        }
    }
    return analyzed;
}

bool tuner_engine_get_result(const tuner_engine_t *engine,
                             tuner_result_t *out)
{
    if (engine == NULL || out == NULL) {
        return false;
    }

    tuner_result_t snapshot;
    if (!audio_seqlock_read(&engine->lock, &snapshot, &engine->result,
                            sizeof(snapshot), 4u)) {
        return false;
    }
    *out = snapshot;
    return true;
}

void tuner_engine_set_a4_reference(tuner_engine_t *engine, float a4_hz)
{
    if (engine == NULL || !finite_positive(a4_hz) || a4_hz < 100.0f ||
        a4_hz > 1000.0f) {
        return;
    }
    engine->a4_hz = a4_hz;
}

void tuner_engine_set_threshold(tuner_engine_t *engine, float threshold)
{
    if (engine == NULL || !finite_positive(threshold)) {
        return;
    }
    if (threshold > 1.0f) {
        threshold = 1.0f;
    }
    engine->threshold = threshold;
}
