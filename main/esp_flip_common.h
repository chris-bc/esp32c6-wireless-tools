/*  Globals to track module status information */
enum AttackMode {
    ATTACK_BEACON,
    ATTACK_PROBE,
    ATTACK_SNIFF,
    ATTACK_DEAUTH,
    ATTACK_MANA,
    ATTACK_MANA_VERBOSE,
    ATTACK_MANA_LOUD,
    ATTACK_AP_DOS,
    ATTACK_AP_CLONE,
    ATTACK_SCAN,
    ATTACK_HANDSHAKE,
    ATTACK_RANDOMISE_MAC, // True
    ATTACKS_COUNT
};
typedef enum AttackMode AttackMode;

/* Command usage strings */
const char USAGE_BEACON[] = "Beacon spam attack. Usage: beacon [ RICKROLL | RANDOM [ COUNT ] | INFINITE | USER | OFF ]";
const char USAGE_TARGET_SSIDS[] = "Manage SSID targets. Usage: target-ssids [ ( ADD | REMOVE ) <ssid_name> ]";
const char USAGE_PROBE[] = "Probe flood attack. Usage: probe [ ANY | SSIDS | OFF ]";
const char USAGE_SNIFF[] = "Display interesting packets. Usage: sniff [ ON | OFF ]";
const char USAGE_DEAUTH[] = "Deauth attack. Usage: deauth [ <millis> ] [ FRAME | DEVICE | SPOOF ] [ STA | BROADCAST | OFF ]";
const char USAGE_MANA[] = "Mana attack. Usage: mana ( CLEAR | ( [ VERBOSE ] [ ON | OFF ] ) | ( AUTH [ NONE | WEP | WPA ] ) | ( LOUD [ ON | OFF ] ) )";
const char USAGE_STALK[] = "Toggle target tracking/homing. Usage: stalk";
const char USAGE_AP_DOS[] = "802.11 denial-of-service attack. Usage: ap-dos [ ON | OFF ]";
const char USAGE_AP_CLONE[] = "Clone and attempt takeover of the specified AP. Usage: ap-clone [ <AP MAC> | OFF ]";
const char USAGE_SCAN[] = "Scan for wireless devices. Usage: scan [ <ssid> | ON | OFF ]";
const char USAGE_HOP[] = "Configure channel hopping. Usage: hop [ <millis> ] [ ON | OFF | DEFAULT | KILL ]";
const char USAGE_SET[] = "Set a variable. Usage: set <variable> <value>";
const char USAGE_GET[] = "Get a variable. Usage: get <variable>";
const char USAGE_VIEW[] = "List available targets. Usage: view ( AP | STA )+";
const char USAGE_SELECT[] = "Select an element. Usage: select ( AP | STA ) <elementId>+";
const char USAGE_CLEAR[] = "Clear stored APs or STAs. Usage: clear ( AP | STA | ALL )";
const char USAGE_HANDSHAKE[] = "Toggle monitoring for encryption material. Usage handshake [ ON | OFF ]";
const char USAGE_COMMANDS[] = "Display a *brief* summary of Gravity commands";