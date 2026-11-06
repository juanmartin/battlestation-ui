#include "apps.h"

#include "ui_probe.h"

static esp_err_t dashboard_mount(lv_obj_t *parent)
{
    return ui_probe_mount(parent);
}

static void dashboard_unmount(void)
{
    ui_probe_unmount();
}

const app_desc_t dashboard_app = {
    .id = "dashboard",
    .title = "Dashboard",
    .description = "Live audio, Wi-Fi, touch and display diagnostics.",
    .mount = dashboard_mount,
    .unmount = dashboard_unmount,
};
