/* Basic console example (esp_console_repl API)

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <stdio.h>
#include <string.h>
#include "esp_err.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_console.h"
#include "esp_vfs_dev.h"
#include "esp_vfs_fat.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "cmd_system.h"
#include "cmd_wifi.h"
#include "cmd_nvs.h"

#include "main.h"

static const char* TAG = "example";
#define PROMPT_STR CONFIG_IDF_TARGET

/* Console command history can be stored to and loaded from a file.
 * The easiest way to do this is to use FATFS filesystem on top of
 * wear_levelling library.
 */
#if CONFIG_CONSOLE_STORE_HISTORY

#define MOUNT_PATH "/data"
#define HISTORY_PATH MOUNT_PATH "/history.txt"

/* Command specifications */
esp_console_cmd_t commands[13] = {
    {
        .command = "beacon",
        .hint = "Toggle beacon spam attack. Usage: beacon ( RICKROLL | RANDOM | USER ) [ TARGET MAC ]. User-defined attack requires target-ssids to be set.",
        .help = "A beacon spam attack continously transmits forged beacon frames. RICKROLL will simulate eight APs named after popular song lyrics. RANDOM will generate random SSIDs between SSID_LEN_MIN and SSID_LEN_MAX in length. USER will generate SSIDs as specified in target-ssids.",
        .func = cmd_beacon
    }
};

int cmd_beacon(int argc, char **argv) {


    return 0;
}

static int register_console_commands() {
    esp_console_cmd_t *config;
    config = malloc(sizeof(esp_console_cmd_t));
    if (config == NULL) {
        ESP_LOGE(TAG, "Unable to allocate memory for BEACON command. PANIC!");
        return ESP_ERR_NO_MEM;
    }
    config->command = "beacon";
    config->hint = "Toggle beacon spam attack. Usage: beacon ( RICKROLL | RANDOM | USER ) [ TARGET MAC ]. User-defined attack requires target-ssids to be set.";
    config->help = "A beacon spam attack continously transmits forged beacon frames. RICKROLL will simulate eight APs named after popular song lyrics. RANDOM will generate random SSIDs between SSID_LEN_MIN and SSID_LEN_MAX in length. USER will generate SSIDs as specified in target-ssids.";
    config->func = cmd_beacon;
    config->argtable = NULL;
    esp_err_t err = esp_console_cmd_register(config);
    switch (err) {
    case ESP_OK:
        ESP_LOGI(TAG, "Registered command BEACON...");
        break;
    case ESP_ERR_NO_MEM:
        ESP_LOGE(TAG, "Out of memory registering command BEACON!");
        return ESP_ERR_NO_MEM;
    case ESP_ERR_INVALID_ARG:
        ESP_LOGW(TAG, "Invalid arguments provided during registration of BEACON. Skipping...");
        return ESP_ERR_INVALID_ARG;
    }

    config = malloc(sizeof(esp_console_cmd_t));
    if (config == NULL ) {
        //
    }


    return ESP_OK;
}

static void initialize_filesystem(void)
{
    static wl_handle_t wl_handle;
    const esp_vfs_fat_mount_config_t mount_config = {
            .max_files = 4,
            .format_if_mount_failed = true
    };
    esp_err_t err = esp_vfs_fat_spiflash_mount_rw_wl(MOUNT_PATH, "storage", &mount_config, &wl_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mount FATFS (%s)", esp_err_to_name(err));
        return;
    }
}
#endif // CONFIG_STORE_HISTORY

static void initialize_nvs(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK( nvs_flash_erase() );
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
}

void app_main(void)
{
    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    /* Prompt to be printed before each line.
     * This can be customized, made dynamic, etc.
     */
    repl_config.prompt = PROMPT_STR ">";
    repl_config.max_cmdline_length = CONFIG_CONSOLE_MAX_COMMAND_LINE_LENGTH;

    initialize_nvs();

#if CONFIG_CONSOLE_STORE_HISTORY
    initialize_filesystem();
    repl_config.history_save_path = HISTORY_PATH;
    ESP_LOGI(TAG, "Command history enabled");
#else
    ESP_LOGI(TAG, "Command history disabled");
#endif

    /* Initialise console */
    esp_console_config_t *config;
    config = malloc(sizeof(esp_console_config_t));
    if (config == NULL) {
        ESP_LOGE(TAG, "Unable to allocate memory for console configuration. PANIC");
        return;
    }
    config->hint_bold = 1;
    config->hint_color = atoi(LOG_COLOR_GREEN);
    config->max_cmdline_args = 3;
    config->max_cmdline_length = 64;
    esp_console_init(config);

    /* Register console commands */
    register_console_commands();
    esp_console_register_help_command();
    register_system_common();
#ifndef CONFIG_IDF_TARGET_ESP32H2  // needs deep sleep support, IDF-6268
    register_system_sleep();
#endif
#if SOC_WIFI_SUPPORTED
    register_wifi();
#endif
    register_nvs();

#if defined(CONFIG_ESP_CONSOLE_UART_DEFAULT) || defined(CONFIG_ESP_CONSOLE_UART_CUSTOM)
    esp_console_dev_uart_config_t hw_config = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_uart(&hw_config, &repl_config, &repl));

#elif defined(CONFIG_ESP_CONSOLE_USB_CDC)
    esp_console_dev_usb_cdc_config_t hw_config = ESP_CONSOLE_DEV_CDC_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_usb_cdc(&hw_config, &repl_config, &repl));

#elif defined(CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG)
    esp_console_dev_usb_serial_jtag_config_t hw_config = ESP_CONSOLE_DEV_USB_SERIAL_JTAG_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_usb_serial_jtag(&hw_config, &repl_config, &repl));

#else
#error Unsupported console type
#endif

    ESP_ERROR_CHECK(esp_console_start_repl(repl));
}
