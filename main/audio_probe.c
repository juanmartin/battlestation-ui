#include "audio_probe.h"

esp_err_t audio_probe_start(void)
{
    return audio_service_start();
}

void audio_probe_get(audio_probe_snapshot_t *out)
{
    audio_service_get_legacy(out);
}
