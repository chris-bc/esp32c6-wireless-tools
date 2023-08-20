#include "bluetooth.h"
#include "common.h"
#include "esp_bt_defs.h"
#include "esp_err.h"
#include "esp_gap_bt_api.h"

#if defined(CONFIG_IDF_TARGET_ESP32)

const char *BT_TAG = "bt@GRAVITY";

app_gap_cb_t *gravity_bt_devices = NULL;
uint8_t gravity_bt_dev_count = 0;
app_gap_state_t state;

/* ESP32-Gravity :  Bluetooth
   Initially-prototyped functionality uses traditional Bluetooth discovery
   This *works*, but obtains VERY limited results.
   TODO: Research bluetooth protocol. Hopefully it'll be straightforward to sniff
         packets in the same way as 802.11, otherwise it might be possible to add
         a nRF24 to the ESP32.
   Plan A is to start with this discovery and then dive into sniffing, so BT commands:
   scan bt-d - Scan BT using Discovery
   scan bt-s - Scan BT using Sniffing
   select bt n - Select BT result n. Both discovery and sniffing results are included in the 'BT' reference

   Step 1. Build a list of discovered devices
   Step 2. Only display info for newly-discovered devices
   Step 3. Investigate (non-)display of services - All the "TODO"s
   Step 4. Try to connect and stuff
   Step 5. Sniff
   Step 6. Stalk

   What it did:
   - start discovery
   - find device
   - cancel discovery
   - receive discovery stopped event and start service discovery
   - display services

   What it should do:
   - start discovery
   - find device
   - pause discovery
   - receive discovery stopped event and start service discovery
   - display services
   - resume discovery
*/

/* cod2deviceStr
   Converts a uint32_t representing a Bluetooth Class Of Device (COD)'s major
   device code into a useful string descriptor of the specified COD major device.
   Returns an error indicator. Input 'cod' is the COD to be converted.
   char *string is a pointer to a character array of sufficient size to hold
   the results. The maximum possible number of characters required by this
   function, including the trailing NULL, is 59.
   uint8_t *stringLen is a pointer to a uint8_t variable in the calling code.
   This variable will be overwritten with the length of string.
*/
esp_err_t cod2deviceStr(uint32_t cod, char *string, uint8_t *stringLen) {
    esp_err_t err = ESP_OK;
    char temp[59] = "";
    esp_bt_cod_major_dev_t devType = esp_bt_gap_get_cod_major_dev(cod);
    switch (devType) {
        case ESP_BT_COD_MAJOR_DEV_MISC:
            strcpy(temp, "Miscellaneous");
            break;
        case ESP_BT_COD_MAJOR_DEV_COMPUTER:
            strcpy(temp, "Computer");
            break;
        case ESP_BT_COD_MAJOR_DEV_PHONE:
            strcpy(temp, "Phone (cellular, cordless, pay phone, modem)");
            break;
        case ESP_BT_COD_MAJOR_DEV_LAN_NAP:
            strcpy(temp, "LAN, Network Access Point");
            break;
        case ESP_BT_COD_MAJOR_DEV_AV:
            strcpy(temp, "Audio/Video (headset, speaker, stereo, video display, VCR)");
            break;
        case ESP_BT_COD_MAJOR_DEV_PERIPHERAL:
            strcpy(temp, "Peripheral (mouse, joystick, keyboard)");
            break;
        case ESP_BT_COD_MAJOR_DEV_IMAGING:
            strcpy(temp, "Imaging (printer, scanner, camera, display)");
            break;
        case ESP_BT_COD_MAJOR_DEV_WEARABLE:
            strcpy(temp, "Wearable");
            break;
        case ESP_BT_COD_MAJOR_DEV_TOY:
            strcpy(temp, "Toy");
            break;
        case ESP_BT_COD_MAJOR_DEV_HEALTH:
            strcpy(temp, "Health");
            break;
        case ESP_BT_COD_MAJOR_DEV_UNCATEGORIZED:
            strcpy(temp, "Uncategorised: Device not specified");
            break;
        default:
            strcpy(temp, "ERROR: Invalid Major Device Type");
            break;
    }
    strcpy(string, temp);
    *stringLen = strlen(string);

    return err;
}

static char *bda2str(esp_bd_addr_t bda, char *str, size_t size) {
    if (bda == NULL || str == NULL || size < 18) {
        return NULL;
    }

    uint8_t *p = bda;
    sprintf(str, "%02x:%02x:%02x:%02x:%02x:%02x", p[0], p[1], p[2], p[3], p[4], p[5]);
    return str;
}

static char *uuid2str(esp_bt_uuid_t *uuid, char *str, size_t size) {
    if (uuid == NULL || str == NULL) {
        return NULL;
    }

    if (uuid->len == 2 && size >= 5) {
        sprintf(str, "%04x", uuid->uuid.uuid16);
    } else if (uuid->len == 4 && size >= 9) {
        sprintf(str, "%08"PRIx32, uuid->uuid.uuid32);
    } else if (uuid->len == 16 && size >= 37) {
        uint8_t *p = uuid->uuid.uuid128;
        sprintf(str, "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
                p[15], p[14], p[13], p[12], p[11], p[10], p[9], p[8], p[7], p[6], p[5], p[4],
                p[3], p[2], p[1], p[0]);
    } else {
        return NULL;
    }
    return str;
}

static bool get_name_from_eir(uint8_t *eir, uint8_t *bdname, uint8_t *bdname_len)
{
    uint8_t *rmt_bdname = NULL;
    uint8_t rmt_bdname_len = 0;

    if (!eir) {
        return false;
    }

    rmt_bdname = esp_bt_gap_resolve_eir_data(eir, ESP_BT_EIR_TYPE_CMPL_LOCAL_NAME, &rmt_bdname_len);
    if (!rmt_bdname) {
        rmt_bdname = esp_bt_gap_resolve_eir_data(eir, ESP_BT_EIR_TYPE_SHORT_LOCAL_NAME, &rmt_bdname_len);
    }

    if (rmt_bdname) {
        if (rmt_bdname_len > ESP_BT_GAP_MAX_BDNAME_LEN) {
            rmt_bdname_len = ESP_BT_GAP_MAX_BDNAME_LEN;
        }

        if (bdname) {
            memcpy(bdname, rmt_bdname, rmt_bdname_len);
            bdname[rmt_bdname_len] = '\0';
        }
        if (bdname_len) {
            *bdname_len = rmt_bdname_len;
        }
        return true;
    }

    return false;
}

/* update_device_info
   This function is called by the Bluetooth callback when a device discovered event
   is received. It will maintain gravity_bt_devices and gravity_bt_dev_count with a
   singe element per physical device.
*/
void update_device_info(esp_bt_gap_cb_param_t *param) {
    app_gap_cb_t *currentDevice = &m_dev_info;
    char bda_str[18];
    char *bdNameStr = NULL;
    esp_bt_gap_dev_prop_t *p;
    char codDevType[59]; /* Placeholder to store COD major device type */
    uint8_t codDevTypeLen = 0;
    uint8_t thisNameLen;

    /* Is it a new BDA (i.e. device we haven't seen before)? */
    int deviceIdx = 0;
    char temp[18];
    bda2str(param->disc_res.bda, bda_str, 18);
    for (; deviceIdx < gravity_bt_dev_count && memcmp(param->disc_res.bda, 
                    gravity_bt_devices[deviceIdx].bda, ESP_BD_ADDR_LEN); ++deviceIdx) {
        #ifdef CONFIG_DEBUG_VERBOSE
            printf("Looking for existing device with BDA %s\tIteration %d of %d, BDA %s\n", bda_str, deviceIdx, gravity_bt_dev_count, bda2str(gravity_bt_devices[deviceIdx].bda, temp, 18));
        #endif
    }

    if (deviceIdx == gravity_bt_dev_count) {
        ESP_LOGI(BT_TAG, "Found New Device: %s", bda2str(param->disc_res.bda, bda_str, 18));
    }
    for (int i = 0; i < param->disc_res.num_prop; i++) {
        p = param->disc_res.prop + i;
        switch (p->type) {
        case ESP_BT_GAP_DEV_PROP_COD:
            currentDevice->cod = *(uint32_t *)(p->val);
            cod2deviceStr(currentDevice->cod, codDevType, &codDevTypeLen);
            if (deviceIdx == gravity_bt_dev_count) {
                ESP_LOGI(BT_TAG, "--Device Type: %s  Class: 0x%"PRIx32, codDevType, currentDevice->cod);
            }
            break;
        case ESP_BT_GAP_DEV_PROP_RSSI:
            currentDevice->rssi = *(int8_t *)(p->val);
            if (deviceIdx == gravity_bt_dev_count) {
                ESP_LOGI(BT_TAG, "--RSSI: %"PRId32, currentDevice->rssi);
            }
            break;
        case ESP_BT_GAP_DEV_PROP_BDNAME:
            /* extract bdnameLen */
            thisNameLen = (p->len > ESP_BT_GAP_MAX_BDNAME_LEN) ? ESP_BT_GAP_MAX_BDNAME_LEN :
                    (uint8_t)p->len;

            if (bdNameStr != NULL) {
                free(bdNameStr);
            }
            bdNameStr = malloc(sizeof(char) * (thisNameLen + 1));
            if (bdNameStr == NULL) {
                #ifdef CONFIG_FLIPPER
                    printf("Unable to malloc space for BT name\n");
                    continue;
                #else
                    ESP_LOGW(BT_TAG, "Unable to malloc space for Bluetooth device name");
                    continue;
                #endif
            }
            memcpy(currentDevice->bdname, p->val, thisNameLen);
            currentDevice->bdname_len = thisNameLen;
            memcpy(bdNameStr, p->val, thisNameLen);
            bdNameStr[thisNameLen] = '\0';

            #ifdef CONFIG_FLIPPER
                printf("--NAME: %s\n", bdNameStr);
            #else
                ESP_LOGI(BT_TAG, "--Device Name: %s", bdNameStr);
            #endif
            break;
        case ESP_BT_GAP_DEV_PROP_EIR: {
            memcpy(currentDevice->eir, p->val, p->len);
            currentDevice->eir_len = p->len;
            break;
        }
        default:
            break;
        }
    }

    /* search for device with Major device type "PHONE" or "Audio/Video" in COD */
    // if (!esp_bt_gap_is_valid_cod(cod) ||
	//     (!(esp_bt_gap_get_cod_major_dev(cod) == ESP_BT_COD_MAJOR_DEV_PHONE) &&
    //          !(esp_bt_gap_get_cod_major_dev(cod) == ESP_BT_COD_MAJOR_DEV_AV))) {
    //     return;
    // }

    memcpy(currentDevice->bda, param->disc_res.bda, ESP_BD_ADDR_LEN);
    
    if (currentDevice->bdname_len == 0) {
        get_name_from_eir(currentDevice->eir, currentDevice->bdname, &currentDevice->bdname_len);
    }

    // I THINK I NEED TO DO BITS HERE TO REFRESH BDNAMESTR AFTER THE ABOVE LINE
    // If that fails, there seem to be quite a few more than one of several related variables. Clean up.

    if (currentDevice->bdname_len > 0) {
        if (bdNameStr != NULL) {
            free(bdNameStr);
        }
        bdNameStr = malloc(sizeof(char) * (currentDevice->bdname_len + 1)); // TODO: Free this later
        if (bdNameStr == NULL) {
            #ifdef CONFIG_FLIPPER
                printf("Unable to malloc memory for BT name\n");
            #else
                ESP_LOGE(BT_TAG, "Unable to allocate %u bytes for BN device name", currentDevice->bdname_len + 1);
            #endif
            return;
        }
        memcpy(bdNameStr, currentDevice->bdname, sizeof(uint8_t) * currentDevice->bdname_len);
        bdNameStr[currentDevice->bdname_len] = '\0';
        if (deviceIdx == gravity_bt_dev_count) {
            #ifdef CONFIG_FLIPPER
                printf("Device Name: %s\n", bdNameStr);
            #else
                ESP_LOGI(BT_TAG, "--Device Name: %s", bdNameStr);
            #endif
        }
    }

    /* Is BDA already in gravity_bt_devices[]? */
    if (deviceIdx < gravity_bt_dev_count) {
        /* Found an existing device with the same BDA - Update its RSSI */
        /* YAGNI - Update bdname if it's changed */
        // TODO: Include a timestamp so we can age devices
        printf("Updating RSSI for %s from %ld to %ld\n", bdNameStr==NULL?bda_str:bdNameStr, gravity_bt_devices[deviceIdx].rssi, currentDevice->rssi);
        gravity_bt_devices[deviceIdx].rssi = currentDevice->rssi;
    } else {
        /* It's a new device - Add to gravity_bt_devices[] */
        // TODO: Can/do/will I ever use the state variable (hardcoded 0 here)?
        //bt_dev_add(currentDevice->bda, currentDevice->bdname, currentDevice->bdname_len, currentDevice->eir, currentDevice->eir_len, currentDevice->cod, currentDevice->rssi);
        bt_dev_add(currentDevice);

        /* Reset m_dev_info now that the new device has been added */
        m_dev_info.bdname_len = 0;
        m_dev_info.cod = 0;
        m_dev_info.eir_len = 0;
        m_dev_info.rssi = -127; /* Invalid value */
        memset(m_dev_info.eir, '\0', ESP_BT_GAP_EIR_DATA_LEN);
        memset(m_dev_info.bda, '\0', ESP_BD_ADDR_LEN);
        memset(m_dev_info.bdname, '\0', ESP_BT_GAP_MAX_BDNAME_LEN + 1);
    }

    state = APP_GAP_STATE_DEVICE_DISCOVER_COMPLETE;
    //ESP_LOGI(BT_TAG, "Cancel device discovery ...");
    //esp_bt_gap_cancel_discovery();
}

void bt_gap_init() {
    memset(&m_dev_info, 0, sizeof(app_gap_cb_t));

    state = APP_GAP_STATE_IDLE;
    m_dev_info.cod = 0;
    m_dev_info.rssi = -127; /* Invalid value */
    m_dev_info.bdname_len = 0;
    m_dev_info.eir_len = 0;
}

static void bt_gap_cb(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param) {
    app_gap_cb_t *currentDevice = &m_dev_info;
    switch (event) {
        case ESP_BT_GAP_DISC_RES_EVT:
            update_device_info(param);
            break;
        case ESP_BT_GAP_DISC_STATE_CHANGED_EVT:
            if (param->disc_st_chg.state == ESP_BT_GAP_DISCOVERY_STOPPED) {
                ESP_LOGI(BT_TAG, "Device discovery stopped.");
                if ( (state == APP_GAP_STATE_DEVICE_DISCOVER_COMPLETE ||
                        state == APP_GAP_STATE_DEVICE_DISCOVERING) && gravity_bt_dev_count > 0) {
                    state = APP_GAP_STATE_SERVICE_DISCOVERING;
                    ESP_LOGI(BT_TAG, "Discover services...");
                    esp_bt_gap_get_remote_services(currentDevice->bda);
                }
            } else if (param->disc_st_chg.state == ESP_BT_GAP_DISCOVERY_STARTED) {
                ESP_LOGI(BT_TAG, "Discovery started");
            }
            break;
        case ESP_BT_GAP_RMT_SRVCS_EVT:
            char bda_str[18];
            if (memcmp(param->rmt_srvcs.bda, currentDevice->bda, ESP_BD_ADDR_LEN) == 0 &&
                    state == APP_GAP_STATE_SERVICE_DISCOVERING) {
                state = APP_GAP_STATE_SERVICE_DISCOVER_COMPLETE;
                if (param->rmt_srvcs.stat == ESP_BT_STATUS_SUCCESS) {
                    ESP_LOGI(BT_TAG, "Services for device %s found", bda2str(currentDevice->bda, bda_str, 18));
                    for (int i = 0; i < param->rmt_srvcs.num_uuids; ++i) {
                        esp_bt_uuid_t *u = param->rmt_srvcs.uuid_list + i;
                        // ESP_UUID_LEN_128 is uint8_t[128]
                        ESP_LOGI(BT_TAG, "-- UUID type %s, UUID: %lu", (u->len == ESP_UUID_LEN_16)?"ESP_UUID_LEN_16":(u->len == ESP_UUID_LEN_32)?"ESP_UUID_LEN_32":"ESP_UUID_LEN_128", (u->len == ESP_UUID_LEN_16)?u->uuid.uuid16:(u->len == ESP_UUID_LEN_32)?u->uuid.uuid32:0);
                        if (u->len == ESP_UUID_LEN_128) {
                            char *uuidStr = malloc(sizeof(char) * (3 * 128));
                            if (uuidStr == NULL) {
                                printf("Unable to allocate space for string representation of UUID128\n");
                                return;
                            }
                            if (bytes_to_string(u->uuid.uuid128, uuidStr, 128) != ESP_OK) {
                                printf ("bytes_to_string returned an error\n");
                                free(uuidStr);
                                return;
                            }
                            ESP_LOGI(BT_TAG, "UUID128: %s\n", uuidStr);
                            free(uuidStr);
                        }
                    }
                } else {
                    ESP_LOGI(BT_TAG, "Services for device %s not found", bda2str(currentDevice->bda, bda_str, 18));
                }
            }
            break;
        case ESP_BT_GAP_RMT_SRVC_REC_EVT:
        default:
            ESP_LOGI(BT_TAG, "event: %d", event);
            break;
    }
    return;
}

void bt_gap_start() {
    esp_bt_gap_register_callback(bt_gap_cb);
    /* Set Device name */
    char *dev_name = "GRAVITY_INQUIRY";
    esp_bt_dev_set_device_name(dev_name);

    /* Set discoverable and connectable, wait to be connected */
    esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);

    bt_gap_init();

    /* Start to discover nearby devices */
    state = APP_GAP_STATE_DEVICE_DISCOVERING;
    esp_bt_gap_start_discovery(ESP_BT_INQ_MODE_GENERAL_INQUIRY, 0x30, 0);
}

void testBT() {
    esp_err_t err = ESP_OK;
    err = esp_bt_controller_mem_release(ESP_BT_MODE_BLE);
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    //bt_cfg.mode = ESP_BT_MODE_CLASSIC_BT;
    err = esp_bt_controller_init(&bt_cfg);
    /* Enable WiFi sleep mode in order for wireless coexistence to work */
    esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
    err = esp_bt_controller_enable(BTDM_CONTROLLER_MODE_EFF); /* Was ESP_BT_MODE_CLASSIC_BT */
    err = esp_bluedroid_init();
    err = esp_bluedroid_enable();
    bt_gap_start();
}

/* Add a new Bluetooth device to gravity_bt_devices[] 
   Creates a new BT device struct using the specified parameters and adds it to
   gravity_bt_devices[].
   A uniqueness check will be done using BDA prior to adding the specified device.
   The most barebones, valid way to call this (i.e. the minimum info that must be
   specified for a BT device) is bt_dev_add(myValidBDA, NULL, 0, NULL, 0, myValidCOD, 0, 0);
   bdNameLen should specify the raw length of the name because it is stored in a uint8_t[],
   excluding the trailing '\0'.
*/
esp_err_t bt_dev_add_components(esp_bd_addr_t bda, uint8_t *bdName, uint8_t bdNameLen, uint8_t *eir,
                        uint8_t eirLen, uint32_t cod, int32_t rssi) {
    esp_err_t err = ESP_OK, err2 = ESP_OK;

    /* Make sure the specified BDA doesn't already exist */
    if (isBDAInArray(bda, gravity_bt_devices, gravity_bt_dev_count)) {
        char bdaStr[18] = "";
        #ifdef CONFIG_FLIPPER
            printf("Unable to add existing BT Dev:\n%25s\n", bda2str(bda, bdaStr, 18));
        #else
            ESP_LOGE(BT_TAG, "Unable to add the requested Bluetooth device to Gravity's device array; BDA %s already exists.", bda2str(bda, bdaStr, 18));
        #endif
        return ESP_ERR_NOT_SUPPORTED;
    }

    /* Set up a replacement copy of gravity_bt_devices */
    app_gap_cb_t *newDevices = malloc(sizeof(app_gap_cb_t) * (gravity_bt_dev_count + 1));
    if (newDevices == NULL) {
        #ifdef CONFIG_FLIPPER
            printf("Unable to allocate memory to add BT device\n");
        #else
            ESP_LOGE(BT_TAG, "Insufficient memory to extend the array of Bluetooth devices in memory.");
        #endif
        return ESP_ERR_NO_MEM;
    }
    /* Copy contents from gravity_bt_devices to newDevices */
    /* This was previously a loop iterating through and calling an additional function to
       copy the struct. Luckily I stuffed up and corrupted nearby memory so I had to use my
       brain and come up with a simple solution - Copy the memory contents of the first array
       to the second.
    */
    memcpy(newDevices, gravity_bt_devices, (gravity_bt_dev_count * sizeof(app_gap_cb_t)));

    char nameStr[64] = "";
    if (bdNameLen > 0) {
        if (bdNameLen > 63) {
            printf("BDNameLen too long\n");
            nameStr[0] = '\0';
            bdNameLen = 0;
        } else {
            memcpy(nameStr, bdName, (int)bdNameLen);
            nameStr[bdNameLen] = '\0';
        }
    }
    /* Create new device at index gravity_bt_dev_count */
    newDevices[gravity_bt_dev_count].bdname_len = bdNameLen;
    newDevices[gravity_bt_dev_count].eir_len = eirLen;
    newDevices[gravity_bt_dev_count].rssi = rssi;
    newDevices[gravity_bt_dev_count].cod = cod;
    memcpy(newDevices[gravity_bt_dev_count].bda, bda, ESP_BD_ADDR_LEN);
    memcpy(newDevices[gravity_bt_dev_count].bdname, bdName, bdNameLen);
    newDevices[gravity_bt_dev_count].bdname[bdNameLen] = '\0';
    memcpy(newDevices[gravity_bt_dev_count].eir, eir, eirLen);

    /* Finally copy new device array into place */
    if (gravity_bt_devices != NULL) {
        free(gravity_bt_devices);
    }
    gravity_bt_devices = newDevices;
    ++gravity_bt_dev_count;

    return err;
}

esp_err_t bt_dev_add(app_gap_cb_t *dev) {
    return bt_dev_add_components(dev->bda, dev->bdname, dev->bdname_len,
            dev->eir, dev->eir_len, dev->cod, dev->rssi);
}

/* Is the specified bluetooth device address in the specified array, which has the specified length? */
bool isBDAInArray(esp_bd_addr_t bda, app_gap_cb_t *array, uint8_t arrayLen) {
    int i = 0;
    for (; i < arrayLen && memcmp(bda, array[i].bda, ESP_BD_ADDR_LEN); ++i) { }
    
    /* If i < arrayLen then it was found */
    return (i < arrayLen);
}

/* Copy all elements of a app_gap_cb_t representing a Bluetooth device
   from one variable to another.
   If dest is a reused app_gap_cb_t, this function WILL NOT manage the
   clean release of memory from its elements. That's up to the caller.
*/
esp_err_t bt_dev_copy(app_gap_cb_t dest, app_gap_cb_t source) {
    esp_err_t err = ESP_OK;

    /* Start with the pass-by-value elements */
    dest.bdname_len = source.bdname_len;
    dest.eir_len = source.eir_len;
    dest.rssi = source.rssi;
    dest.cod = source.cod;

    /* And now the refs */
    memcpy(dest.bda, source.bda, ESP_BD_ADDR_LEN);
    memcpy(dest.eir, source.eir, source.eir_len);
    memcpy(dest.bdname, source.bdname, source.bdname_len);
    dest.bdname[dest.bdname_len] = '\0';

    return err;
}

#endif