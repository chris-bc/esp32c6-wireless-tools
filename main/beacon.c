#include <esp_wifi.h>
#include "esp_wifi_default.h"
#include "esp_wifi_types.h"
#include "nvs.h"
#include <esp_err.h>
#include <stdbool.h>
#include <nvs_flash.h>
#include <string.h>

/*
 * This is the (currently unofficial) 802.11 raw frame TX API,
 * defined in esp32-wifi-lib's libnet80211.a/ieee80211_output.o
 *
 * This declaration is all you need for using esp_wifi_80211_tx in your own application.
 */
esp_err_t esp_wifi_80211_tx(wifi_interface_t ifx, const void *buffer, int len, bool en_sys_seq);

uint8_t beacon_raw[] = {
	0x80, 0x00,							    // 0-1: Frame Control
	0x00, 0x00,						    	// 2-3: Duration
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff,				// 4-9: Destination address (broadcast)
	0xba, 0xde, 0xaf, 0xfe, 0x00, 0x06,		// 10-15: Source address
	0xba, 0xde, 0xaf, 0xfe, 0x00, 0x06,		// 16-21: BSSID
	0x00, 0x00,				    		// 22-23: Sequence / fragment number
	0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,     // 24-31: Timestamp (GETS OVERWRITTEN TO 0 BY HARDWARE)
	0x64, 0x00,			    			// 32-33: Beacon interval
	0x31, 0x04,		    				// 34-35: Capability info
	0x00, 0x00, /* FILL CONTENT HERE */	// 36-38: SSID parameter set, 0x00:length:content
	0x01, 0x08, 0x82, 0x84,	0x8b, 0x96, 0x0c, 0x12, 0x18, 0x24,	// 39-48: Supported rates
	0x03, 0x01, 0x01,				// 49-51: DS Parameter set, current channel 1 (= 0x01),
	0x05, 0x04, 0x01, 0x02, 0x00, 0x00,		// 52-57: Traffic Indication Map
	
};

char *rick_ssids[] = {
	"01 Never gonna give you up",
	"02 Never gonna let you down",
	"03 Never gonna run around",
	"04 and desert you",
	"05 Never gonna make you cry",
	"06 Never gonna say goodbye",
	"07 Never gonna tell a lie",
	"08 and hurt you"
};

#define BEACON_SSID_OFFSET 38
#define SRCADDR_OFFSET 10
#define BSSID_OFFSET 16
#define SEQNUM_OFFSET 22
#define TOTAL_LINES (sizeof(rick_ssids) / sizeof(char *))

typedef enum {
    ATTACK_BEACON_NONE,
    ATTACK_BEACON_RICKROLL,
    ATTACK_BEACON_RANDOM,
    ATTACK_BEACON_USER
} beacon_attack_t;

static beacon_attack_t attackType;
static int SSID_LEN_MIN = 8;
static int SSID_LEN_MAX = 32;
static bool BEACON_INITIALISED = false;

static TaskHandle_t task;

void beaconSpam(void *pvParameter) {
	uint8_t line = 0;

	// Keep track of beacon sequence numbers on a per-songline-basis
	uint16_t seqnum[TOTAL_LINES] = { 0 };

	for (;;) {
		vTaskDelay(100 / TOTAL_LINES / portTICK_PERIOD_MS);

		// Insert line of Rick Astley's "Never Gonna Give You Up" into beacon packet
		uint8_t beacon_rick[200];
		memcpy(beacon_rick, beacon_raw, BEACON_SSID_OFFSET - 1);
		beacon_rick[BEACON_SSID_OFFSET - 1] = strlen(rick_ssids[line]);
		memcpy(&beacon_rick[BEACON_SSID_OFFSET], rick_ssids[line], strlen(rick_ssids[line]));
		memcpy(&beacon_rick[BEACON_SSID_OFFSET + strlen(rick_ssids[line])], &beacon_raw[BEACON_SSID_OFFSET], sizeof(beacon_raw) - BEACON_SSID_OFFSET);

		// Last byte of source address / BSSID will be line number - emulate multiple APs broadcasting one song line each
		beacon_rick[SRCADDR_OFFSET + 5] = line;
		beacon_rick[BSSID_OFFSET + 5] = line;

		// Update sequence number
		beacon_rick[SEQNUM_OFFSET] = (seqnum[line] & 0x0f) << 4;
		beacon_rick[SEQNUM_OFFSET + 1] = (seqnum[line] & 0xff0) >> 4;
		seqnum[line]++;
		if (seqnum[line] > 0xfff)
			seqnum[line] = 0;

		esp_wifi_80211_tx(WIFI_IF_AP, beacon_rick, sizeof(beacon_raw) + strlen(rick_ssids[line]), false);

		if (++line >= TOTAL_LINES)
			line = 0;
	}
}

int beacon_stop() {
	vTaskDelete(task);
    return ESP_OK;
}

int beacon_start(beacon_attack_t type) {
    /* Stop an existing beacon attack if one exists */
    if (attackType != ATTACK_BEACON_NONE) {
        beacon_stop();
    }
    attackType = type;

	// TODO: Prepare the appropriate beacon array
    
    xTaskCreate(&beaconSpam, "beaconSpam", 2048, NULL, 5, &task);
    

    return ESP_OK;
}