#include "slot_switch.h"

#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_system.h"

static const char *TAG = "slot_switch";

bool slot_switch_boot_into(int ota_slot)
{
    const esp_partition_subtype_t subtype =
        (ota_slot < 0) ? ESP_PARTITION_SUBTYPE_APP_FACTORY
                       : (esp_partition_subtype_t)(ESP_PARTITION_SUBTYPE_APP_OTA_MIN + ota_slot);

    const esp_partition_t *target =
        esp_partition_find_first(ESP_PARTITION_TYPE_APP, subtype, NULL);
    if (target == NULL) {
        ESP_LOGE(TAG, "no partition for slot %d", ota_slot);
        return false;
    }

    const esp_err_t ret = esp_ota_set_boot_partition(target);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition failed: %s", esp_err_to_name(ret));
        return false;
    }

    ESP_LOGI(TAG, "rebooting into %s", target->label);
    esp_restart();
    return true;  // unreachable - esp_restart() does not return
}
