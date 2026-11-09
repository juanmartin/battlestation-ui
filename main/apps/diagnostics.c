#include "apps.h"

#include "ui_probe.h"

static esp_err_t diagnostics_mount(lv_obj_t *parent)
{
    return ui_probe_mount(parent);
}

static void diagnostics_unmount(void)
{
    ui_probe_unmount();
}

const app_desc_t diagnostics_app = {
    .id = "diagnostics",
    .title = "Diagnostics",
    .description = "Live audio, Wi-Fi, touch and display diagnostics.",
    .mount = diagnostics_mount,
    .unmount = diagnostics_unmount,
};
