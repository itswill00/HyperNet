/*
 * HyperNet Native Bridge & Network Engine (libhypernet.so)
 * High-performance C executable for Android network telemetry and control.
 *
 * Copyright (C) 2026 @itswill00
 * Licensed under the GNU General Public License v3.0
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <ctype.h>
#include <errno.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#define CONF_DIR          "/data/adb/hypernet"
#define CONF_FILE         "/data/adb/hypernet/config.json"
#define STATUS_FILE       "/data/local/tmp/hypernet_status.json"
#define TRAFFIC_FILE      "/data/local/tmp/hypernet_traffic.json"
#define LOG_FILE          "/data/local/tmp/hypernet.log"

/* Bitmasks for cmd phone set-allowed-network-types-for-users */
#define BM_5G_ONLY        "10000000000000000000"
#define BM_5G_LTE         "11000001000000000000"
#define BM_LTE_ONLY       "01000001000000000000"
#define BM_3G_ONLY        "00000000000000001100"
#define BM_2G_ONLY        "00000000000000000011"
#define BM_GLOBAL_AUTO    "11001111101111111111"

/* Utility: safe string sanitization (alphanumeric, dot, dash, colon) */
static int is_safe_input(const char *str) {
    if (!str || !*str) return 0;
    while (*str) {
        if (!isalnum(*str) && *str != '.' && *str != '-' && *str != '_' && *str != ':') {
            return 0;
        }
        str++;
    }
    return 1;
}

/* Utility: JSON string escape printer */
static void json_print_escaped(const char *str) {
    if (!str) {
        printf("\"\"");
        return;
    }
    putchar('"');
    while (*str) {
        if (*str == '"') {
            printf("\\\"");
        } else if (*str == '\\') {
            printf("\\\\");
        } else if (*str == '\n') {
            printf("\\n");
        } else if (*str == '\r') {
            /* omit */
        } else if (*str == '\t') {
            printf("\\t");
        } else if ((unsigned char)*str >= 32) {
            putchar(*str);
        }
        str++;
    }
    putchar('"');
}

/* Read single line trimmed file content */
static int read_sysfs_line(const char *path, char *buf, size_t max_len) {
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    if (!fgets(buf, max_len, f)) {
        fclose(f);
        return -1;
    }
    fclose(f);
    char *p = buf + strlen(buf) - 1;
    while (p >= buf && (*p == '\n' || *p == '\r' || isspace(*p))) {
        *p = '\0';
        p--;
    }
    return 0;
}

/* Execute command and get first line */
static int read_cmd_line(const char *cmd, char *buf, size_t max_len) {
    FILE *p = popen(cmd, "r");
    if (!p) return -1;
    if (!fgets(buf, max_len, p)) {
        pclose(p);
        return -1;
    }
    pclose(p);
    char *end = buf + strlen(buf) - 1;
    while (end >= buf && (*end == '\n' || *end == '\r' || isspace(*end))) {
        *end = '\0';
        end--;
    }
    return 0;
}

/* Ensure directory exists */
static void ensure_dir(const char *dir) {
    struct stat st;
    if (stat(dir, &st) != 0) {
        mkdir(dir, 0755);
    }
}

/* Fast Traffic Monitoring: parses /proc/net/dev */
static int cmd_traffic(void) {
    FILE *f = fopen("/proc/net/dev", "r");
    if (!f) {
        printf("{\"error\":\"cannot_open_proc_net_dev\"}\n");
        return 1;
    }

    char line[512];
    /* skip first two header lines */
    if (!fgets(line, sizeof(line), f) || !fgets(line, sizeof(line), f)) {
        fclose(f);
        printf("{\"error\":\"invalid_proc_net_dev\"}\n");
        return 1;
    }

    printf("{\"timestamp\":%ld,\"interfaces\":[", (long)time(NULL));
    int first = 1;

    while (fgets(line, sizeof(line), f)) {
        char *colon = strchr(line, ':');
        if (!colon) continue;
        *colon = '\0';

        char *ifname = line;
        while (isspace(*ifname)) ifname++;

        /* Filter out loopback */
        if (strcmp(ifname, "lo") == 0) continue;

        unsigned long long rx_bytes = 0, rx_pkts = 0;
        unsigned long long tx_bytes = 0, tx_pkts = 0;
        unsigned long long dummy = 0;

        /* Format: rx_bytes rx_packets rx_errs rx_drop rx_fifo rx_frame rx_compressed rx_multicast tx_bytes tx_packets ... */
        int parsed = sscanf(colon + 1, "%llu %llu %llu %llu %llu %llu %llu %llu %llu %llu",
                            &rx_bytes, &rx_pkts, &dummy, &dummy, &dummy, &dummy, &dummy, &dummy,
                            &tx_bytes, &tx_pkts);

        if (parsed >= 10) {
            if (!first) printf(",");
            printf("{\"name\":\"%s\",\"rx_bytes\":%llu,\"tx_bytes\":%llu,\"rx_pkts\":%llu,\"tx_pkts\":%llu}",
                   ifname, rx_bytes, tx_bytes, rx_pkts, tx_pkts);
            first = 0;
        }
    }

    printf("]}\n");
    fclose(f);
    return 0;
}

/* Parse Wi-Fi details from cmd wifi status */
static void get_wifi_details(int *out_rssi, int *out_speed) {
    FILE *p = popen("cmd wifi status 2>/dev/null", "r");
    char ssid[128] = "unknown";
    char bssid[64] = "unknown";
    char ip[64] = "";
    char standard[32] = "unknown";
    int rssi = 0;
    int link_speed = 0;
    int freq = 0;
    int is_enabled = 0;
    int is_connected = 0;

    if (p) {
        char line[2048];
        while (fgets(line, sizeof(line), p)) {
            if (strstr(line, "Wifi is enabled")) {
                is_enabled = 1;
            }
            if (strstr(line, "Wifi is connected")) {
                is_connected = 1;
            }
            /* Only parse from the authoritative WifiInfo line */
            if (strstr(line, "WifiInfo:")) {
                is_enabled = 1;
                is_connected = 1;
                char *s = strstr(line, "SSID: \"");
                if (s) {
                    s += 7;
                    char *quote = strchr(s, '"');
                    if (quote) {
                        int len = quote - s;
                        if (len > (int)sizeof(ssid) - 1) len = sizeof(ssid) - 1;
                        strncpy(ssid, s, len);
                        ssid[len] = '\0';
                    }
                }
                char *b = strstr(line, "BSSID: ");
                if (b) {
                    b += 7;
                    char *comma = strchr(b, ',');
                    if (comma) {
                        int len = comma - b;
                        if (len > (int)sizeof(bssid) - 1) len = sizeof(bssid) - 1;
                        strncpy(bssid, b, len);
                        bssid[len] = '\0';
                    }
                }
                char *r = strstr(line, "RSSI: ");
                if (r) rssi = atoi(r + 6);
                char *ls = strstr(line, "Link speed: ");
                if (ls) link_speed = atoi(ls + 12);
                char *fr = strstr(line, "Frequency: ");
                if (fr) freq = atoi(fr + 11);
                char *std = strstr(line, "Wi-Fi standard: ");
                if (std) {
                    int std_num = atoi(std + 16);
                    if (std_num == 4) strcpy(standard, "Wi-Fi 4 (802.11n)");
                    else if (std_num == 5) strcpy(standard, "Wi-Fi 5 (802.11ac)");
                    else if (std_num == 6) strcpy(standard, "Wi-Fi 6 (802.11ax)");
                    else if (std_num == 7) strcpy(standard, "Wi-Fi 7 (802.11be)");
                    else snprintf(standard, sizeof(standard), "Standard %d", std_num);
                }
                char *ip_match = strstr(line, "IP: /");
                if (ip_match) {
                    ip_match += 5;
                    char *comma = strchr(ip_match, ',');
                    if (comma) {
                        int len = comma - ip_match;
                        if (len > (int)sizeof(ip) - 1) len = sizeof(ip) - 1;
                        strncpy(ip, ip_match, len);
                        ip[len] = '\0';
                    }
                }
                break;
            }
        }
        pclose(p);
    }

    /* Universal fallback: check interface state and route */
    if (!is_connected) {
        char oper[32] = "";
        read_sysfs_line("/sys/class/net/wlan0/operstate", oper, sizeof(oper));
        if (strcmp(oper, "up") == 0) {
            is_enabled = 1;
            is_connected = 1;
        }

        char wlan_route[64] = "";
        read_cmd_line("ip route get 1.1.1.1 2>/dev/null | grep -o 'dev wlan[0-9]*' | head -n1", wlan_route, sizeof(wlan_route));
        if (wlan_route[0] != '\0') {
            is_enabled = 1;
            is_connected = 1;
        }

        char wlan_info[256] = "";
        read_cmd_line("ip -4 addr show wlan0 2>/dev/null | grep -m1 'inet '", wlan_info, sizeof(wlan_info));
        if (wlan_info[0] != '\0') {
            is_enabled = 1;
            is_connected = 1;
            char *inet_ptr = strstr(wlan_info, "inet ");
            if (inet_ptr && ip[0] == '\0') {
                sscanf(inet_ptr + 5, "%63[^/ ]", ip);
            }
        }
    }

    /* Fallback SSID extraction if still unknown */
    if (is_connected && strcmp(ssid, "unknown") == 0) {
        read_cmd_line("dumpsys wifi 2>/dev/null | grep -m1 'mWifiInfo SSID: \"' | sed -n 's/.*SSID: \"\\([^\"]*\\)\".*/\\1/p'", ssid, sizeof(ssid));
    }

    const char *band = "unknown";
    if (freq >= 2400 && freq <= 2500) band = "2.4 GHz";
    else if (freq >= 5000 && freq <= 5900) band = "5 GHz";
    else if (freq >= 5925 && freq <= 7125) band = "6 GHz";

    printf("\"wifi\":{");
    printf("\"enabled\":%s,", is_enabled ? "true" : "false");
    printf("\"connected\":%s,", is_connected ? "true" : "false");
    printf("\"ssid\":"); json_print_escaped(ssid); printf(",");
    printf("\"bssid\":"); json_print_escaped(bssid); printf(",");
    printf("\"ip\":"); json_print_escaped(ip); printf(",");
    printf("\"rssi\":%d,", rssi);
    printf("\"link_speed_mbps\":%d,", link_speed);
    printf("\"frequency_mhz\":%d,", freq);
    printf("\"band\":"); json_print_escaped(band); printf(",");
    printf("\"standard\":"); json_print_escaped(standard);
    printf("}");

    if (out_rssi) *out_rssi = is_connected ? rssi : 0;
    if (out_speed) *out_speed = is_connected ? link_speed : 0;
}

/* Accurate Physical RAM Sizing (accounts for kernel/hardware reservations) */
static int get_physical_ram_gb(int mem_total_mb) {
    if (mem_total_mb <= 0) return 0;
    int raw = (mem_total_mb + 650) / 1024;
    if (raw <= 1) return 1;
    if (raw <= 2) return 2;
    if (raw <= 3) return 3;
    if (raw <= 4) return 4;
    if (raw <= 6) return 6;
    if (raw <= 8) return 8;
    if (raw <= 12) return 12;
    if (raw <= 16) return 16;
    if (raw <= 24) return 24;
    return raw;
}

/* Device Hardware & Platform Intelligence */
static void get_device_details(char *out_tier, size_t tier_len) {
    char brand[64] = "unknown";
    char model[64] = "unknown";
    char platform[64] = "unknown";
    char release[32] = "unknown";
    char sdk[16] = "0";

    read_cmd_line("getprop ro.product.brand 2>/dev/null", brand, sizeof(brand));
    read_cmd_line("getprop ro.product.model 2>/dev/null", model, sizeof(model));
    read_cmd_line("getprop ro.board.platform 2>/dev/null || getprop ro.soc.manufacturer 2>/dev/null", platform, sizeof(platform));
    read_cmd_line("getprop ro.build.version.release 2>/dev/null", release, sizeof(release));
    read_cmd_line("getprop ro.build.version.sdk 2>/dev/null", sdk, sizeof(sdk));

    /* Parse RAM from /proc/meminfo */
    unsigned long mem_total_kb = 0;
    FILE *mf = fopen("/proc/meminfo", "r");
    if (mf) {
        char line[256];
        while (fgets(line, sizeof(line), mf)) {
            if (sscanf(line, "MemTotal: %lu kB", &mem_total_kb) == 1) break;
        }
        fclose(mf);
    }
    int ram_mb = (int)(mem_total_kb / 1024);
    int ram_gb = get_physical_ram_gb(ram_mb);
    const char *ram_tier = "standard";
    if (ram_gb <= 3) ram_tier = "low";
    else if (ram_gb >= 8) ram_tier = "high";

    if (out_tier && tier_len > 0) {
        strncpy(out_tier, ram_tier, tier_len - 1);
        out_tier[tier_len - 1] = '\0';
    }

    printf("\"device\":{");
    printf("\"brand\":"); json_print_escaped(brand); printf(",");
    printf("\"model\":"); json_print_escaped(model); printf(",");
    printf("\"platform\":"); json_print_escaped(platform); printf(",");
    printf("\"android_ver\":"); json_print_escaped(release); printf(",");
    printf("\"api_level\":%d,", atoi(sdk));
    printf("\"ram_total_mb\":%d,", ram_mb);
    printf("\"ram_installed_gb\":%d,", ram_gb);
    printf("\"ram_tier\":"); json_print_escaped(ram_tier);
    printf("}");
}

/* Unified SIM Slot and Cellular Radio Telemetry */
typedef struct {
    int inserted;
    char state[32];
    char operator_name[128];
    char network_type[64];
    int rsrp;
    int rsrq;
    int sinr;
    int level;
    long long cell_id;
    int pci;
    int band;
    int data_enabled;
    char data_state[32];
} SimSlotInfo;

static int parse_int_after(const char *haystack, const char *key, int default_val) {
    const char *p = strstr(haystack, key);
    if (!p) return default_val;
    p += strlen(key);
    while (*p == ' ' || *p == '=') p++;
    char *end = NULL;
    long val = strtol(p, &end, 10);
    if (end == p) return default_val;
    return (int)val;
}

static long long parse_ll_after(const char *haystack, const char *key, long long default_val) {
    const char *p = strstr(haystack, key);
    if (!p) return default_val;
    p += strlen(key);
    while (*p == ' ' || *p == '=') p++;
    char *end = NULL;
    long long val = strtoll(p, &end, 10);
    if (end == p) return default_val;
    return val;
}

static void parse_string_after(const char *haystack, const char *key, char *out, size_t out_len, const char *delims) {
    const char *p = strstr(haystack, key);
    if (!p) return;
    p += strlen(key);
    while (*p == ' ' || *p == '=') p++;
    size_t idx = 0;
    while (*p && idx + 1 < out_len) {
        if (strchr(delims, *p)) break;
        out[idx++] = *p++;
    }
    out[idx] = '\0';
}

static void get_all_sim_and_cellular_details(SimSlotInfo slots[2], int *out_active_slot, int *out_active_subid, char *out_allowed_types, size_t allowed_types_len) {
    memset(slots, 0, sizeof(SimSlotInfo) * 2);

    for (int i = 0; i < 2; i++) {
        strcpy(slots[i].state, "absent");
        strcpy(slots[i].operator_name, "");
        strcpy(slots[i].network_type, "unknown");
        strcpy(slots[i].data_state, "disconnected");
        slots[i].cell_id = -1;
    }

    char sim_states[64] = "absent,absent";
    char sim_ops[128] = "";
    char sim_orig_ops[128] = "";
    char active_sub[16] = "1";
    char prop_types[128] = "";

    read_cmd_line("getprop gsm.sim.state 2>/dev/null", sim_states, sizeof(sim_states));
    read_cmd_line("getprop gsm.operator.orig.alpha 2>/dev/null", sim_orig_ops, sizeof(sim_orig_ops));
    read_cmd_line("getprop gsm.sim.operator.alpha 2>/dev/null", sim_ops, sizeof(sim_ops));
    read_cmd_line("getprop gsm.network.type 2>/dev/null", prop_types, sizeof(prop_types));
    read_cmd_line("settings get global multi_sim_data_call 2>/dev/null", active_sub, sizeof(active_sub));

    /* Parse gsm.sim.state */
    char *comma = strchr(sim_states, ',');
    if (comma) {
        *comma = '\0';
        strncpy(slots[0].state, sim_states, sizeof(slots[0].state) - 1);
        strncpy(slots[1].state, comma + 1, sizeof(slots[1].state) - 1);
    } else {
        strncpy(slots[0].state, sim_states, sizeof(slots[0].state) - 1);
    }

    for (int i = 0; i < 2; i++) {
        for (char *c = slots[i].state; *c; c++) *c = (char)tolower((unsigned char)*c);
        if (strcmp(slots[i].state, "loaded") == 0 ||
            strcmp(slots[i].state, "ready") == 0 ||
            strcmp(slots[i].state, "pin_required") == 0 ||
            strcmp(slots[i].state, "puk_required") == 0 ||
            strcmp(slots[i].state, "network_locked") == 0) {
            slots[i].inserted = 1;
        }
    }

    /* Operator names from properties */
    char *op_source = strlen(sim_orig_ops) > 0 ? sim_orig_ops : sim_ops;
    comma = strchr(op_source, ',');
    if (comma) {
        *comma = '\0';
        strncpy(slots[0].operator_name, op_source, sizeof(slots[0].operator_name) - 1);
        strncpy(slots[1].operator_name, comma + 1, sizeof(slots[1].operator_name) - 1);
    } else {
        strncpy(slots[0].operator_name, op_source, sizeof(slots[0].operator_name) - 1);
    }

    /* Fallbacks for slot 1 operator */
    if (strlen(slots[1].operator_name) == 0) {
        char op2[64] = "";
        if (read_cmd_line("getprop gsm.sim.operator.alpha.2 2>/dev/null", op2, sizeof(op2)) == 0 && strlen(op2) > 0) {
            strncpy(slots[1].operator_name, op2, sizeof(slots[1].operator_name) - 1);
        } else if (read_cmd_line("getprop gsm.operator.alpha.2 2>/dev/null", op2, sizeof(op2)) == 0 && strlen(op2) > 0) {
            strncpy(slots[1].operator_name, op2, sizeof(slots[1].operator_name) - 1);
        }
    }

    /* Network types from properties */
    comma = strchr(prop_types, ',');
    if (comma) {
        *comma = '\0';
        if (strcmp(prop_types, "Unknown") != 0 && strlen(prop_types) > 0)
            strncpy(slots[0].network_type, prop_types, sizeof(slots[0].network_type) - 1);
        if (strcmp(comma + 1, "Unknown") != 0 && strlen(comma + 1) > 0)
            strncpy(slots[1].network_type, comma + 1, sizeof(slots[1].network_type) - 1);
    } else if (strlen(prop_types) > 0 && strcmp(prop_types, "Unknown") != 0) {
        strncpy(slots[0].network_type, prop_types, sizeof(slots[0].network_type) - 1);
    }

    int active_slot = 0;
    int sub = atoi(active_sub);
    if (sub >= 2) active_slot = 1;
    if (!slots[0].inserted && slots[1].inserted) active_slot = 1;

    /* Dumpsys Telephony Registry (strictly scoped per Phone Id) */
    FILE *p = popen("dumpsys telephony.registry 2>/dev/null", "r");
    if (p) {
        char line[2048];
        int cur_slot = -1;
        while (fgets(line, sizeof(line), p)) {
            if (strstr(line, "local logs:") || strstr(line, "mPhoneCapability")) {
                break;
            }

            char *p0 = strstr(line, "Phone Id=0");
            if (!p0) p0 = strstr(line, "PhoneId=0");
            if (p0) { cur_slot = 0; continue; }

            char *p1 = strstr(line, "Phone Id=1");
            if (!p1) p1 = strstr(line, "PhoneId=1");
            if (p1) { cur_slot = 1; continue; }

            char *def_p = strstr(line, "mDefaultPhoneId=");
            if (def_p) {
                int dp = atoi(def_p + 16);
                if (dp == 0 || dp == 1) active_slot = dp;
            }

            if (cur_slot < 0 || cur_slot > 1) continue;

            /* ServiceState: Voice / Data registration & Operator */
            if (strstr(line, "mServiceState=")) {
                char op_long[128] = "";
                parse_string_after(line, "mOperatorAlphaLong=", op_long, sizeof(op_long), ",}\r\n");
                if (strlen(op_long) > 0 && strcmp(op_long, "null") != 0) {
                    strncpy(slots[cur_slot].operator_name, op_long, sizeof(slots[cur_slot].operator_name) - 1);
                }

                if (strstr(line, "mVoiceRegState=0") || strstr(line, "mDataRegState=0")) {
                    slots[cur_slot].inserted = 1;
                    if (strcmp(slots[cur_slot].state, "absent") == 0) {
                        strcpy(slots[cur_slot].state, "in_service");
                    }
                }
            }

            /* Data enabled / user mobile data state */
            if (strstr(line, "mIsDataEnabled=") || strstr(line, "mUserMobileDataState=")) {
                if (strstr(line, "true")) slots[cur_slot].data_enabled = 1;
                else if (strstr(line, "false")) slots[cur_slot].data_enabled = 0;
            }

            /* Data connection state */
            if (strstr(line, "mDataConnectionState=")) {
                int dstate = parse_int_after(line, "mDataConnectionState=", -1);
                if (dstate == 2) strcpy(slots[cur_slot].data_state, "connected");
                else if (dstate == 1) strcpy(slots[cur_slot].data_state, "connecting");
                else if (dstate == 3) strcpy(slots[cur_slot].data_state, "suspended");
                else strcpy(slots[cur_slot].data_state, "disconnected");
            }

            /* Telephony Display Info (LTE, LTE_CA/4G+, NR/5G) */
            char *tdi = strstr(line, "mTelephonyDisplayInfo=");
            if (tdi) {
                if (strstr(tdi, "overrideNetwork=LTE_CA")) {
                    strcpy(slots[cur_slot].network_type, "LTE+");
                } else if (strstr(tdi, "overrideNetwork=NR_") || strstr(tdi, "network=NR")) {
                    strcpy(slots[cur_slot].network_type, "5G NR");
                } else if (strstr(tdi, "network=LTE")) {
                    if (strcmp(slots[cur_slot].network_type, "unknown") == 0 ||
                        strcmp(slots[cur_slot].network_type, "LTE+") != 0) {
                        strcpy(slots[cur_slot].network_type, "LTE");
                    }
                } else if (strstr(tdi, "network=UMTS") || strstr(tdi, "network=HSDPA") || strstr(tdi, "network=HSPA")) {
                    strcpy(slots[cur_slot].network_type, "3G (HSPA)");
                } else if (strstr(tdi, "network=GSM") || strstr(tdi, "network=EDGE")) {
                    strcpy(slots[cur_slot].network_type, "2G (EDGE)");
                }
            }

            /* Physical Channel Configs: PCI & Band */
            char *pcc = strstr(line, "mPhysicalChannelConfigs=");
            if (pcc && !strstr(pcc, "[]")) {
                int pci = parse_int_after(pcc, "mPhysicalCellId=", -1);
                if (pci > 0 && pci != 2147483647) {
                    slots[cur_slot].pci = pci;
                    if (slots[cur_slot].cell_id <= 0) slots[cur_slot].cell_id = pci;
                }
                int band = parse_int_after(pcc, "mBand=", -1);
                if (band > 0 && band != 2147483647) {
                    slots[cur_slot].band = band;
                }
            }

            /* Cell Identity (CI / CID) */
            char *cid = strstr(line, "mCellIdentity=");
            if (cid && !strstr(cid, "mCellIdentity=null")) {
                long long ci = parse_ll_after(cid, "mCi=", -1);
                if (ci <= 0 || ci == 2147483647) ci = parse_ll_after(cid, "mCid=", -1);
                if (ci <= 0 || ci == 2147483647) ci = parse_ll_after(cid, "cid=", -1);
                if (ci > 0 && ci != 2147483647) {
                    slots[cur_slot].cell_id = ci;
                }
                if (strlen(slots[cur_slot].operator_name) == 0) {
                    char alpha[64] = "";
                    parse_string_after(cid, "mAlphaLong=", alpha, sizeof(alpha), ",}\r\n");
                    if (strlen(alpha) > 0 && strcmp(alpha, "null") != 0) {
                        strncpy(slots[cur_slot].operator_name, alpha, sizeof(slots[cur_slot].operator_name) - 1);
                    }
                }
            }

            /* Signal Strength (RSRP, RSRQ, SINR, Level) */
            char *ss = strstr(line, "mSignalStrength=");
            if (ss) {
                char *prim = strstr(ss, "primary=");
                int is_nr = (prim && strstr(prim, "CellSignalStrengthNr")) ? 1 : 0;
                int is_lte = (prim && strstr(prim, "CellSignalStrengthLte")) ? 1 : 0;

                /* 5G NR */
                char *nr = strstr(ss, "CellSignalStrengthNr");
                if (nr) {
                    int ss_rsrp = parse_int_after(nr, "ssRsrp", 2147483647);
                    if (ss_rsrp == 2147483647) ss_rsrp = parse_int_after(nr, "csiRsrp", 2147483647);
                    if (ss_rsrp < 0 && ss_rsrp > -160 && (is_nr || ss_rsrp != 2147483647)) {
                        slots[cur_slot].rsrp = ss_rsrp;
                        if (strcmp(slots[cur_slot].network_type, "unknown") == 0 ||
                            strcmp(slots[cur_slot].network_type, "LTE") == 0) {
                            strcpy(slots[cur_slot].network_type, "5G NR");
                        }
                        int ss_rsrq = parse_int_after(nr, "ssRsrq", 2147483647);
                        if (ss_rsrq <= 0 && ss_rsrq > -40) slots[cur_slot].rsrq = ss_rsrq;
                        int ss_sinr = parse_int_after(nr, "ssSinr", 2147483647);
                        if (ss_sinr >= -30 && ss_sinr <= 50) slots[cur_slot].sinr = ss_sinr;
                        int lvl = parse_int_after(nr, "level", -1);
                        if (lvl >= 0 && lvl <= 5) slots[cur_slot].level = lvl;
                    }
                }

                /* 4G LTE */
                char *lte = strstr(ss, "CellSignalStrengthLte");
                if (lte && (slots[cur_slot].rsrp == 0 || is_lte)) {
                    int l_rsrp = parse_int_after(lte, "rsrp", 2147483647);
                    if (l_rsrp < 0 && l_rsrp > -160) {
                        slots[cur_slot].rsrp = l_rsrp;
                        if (strcmp(slots[cur_slot].network_type, "unknown") == 0) {
                            strcpy(slots[cur_slot].network_type, "LTE");
                        }
                        int l_rsrq = parse_int_after(lte, "rsrq", 2147483647);
                        if (l_rsrq <= 0 && l_rsrq > -40) slots[cur_slot].rsrq = l_rsrq;
                        int l_sn = parse_int_after(lte, "rssnr", 2147483647);
                        if (l_sn == 2147483647) l_sn = parse_int_after(lte, "sinr", 2147483647);
                        if (l_sn >= -30 && l_sn <= 50) slots[cur_slot].sinr = l_sn;
                        int lvl = parse_int_after(lte, "level", -1);
                        if (lvl >= 0 && lvl <= 5) slots[cur_slot].level = lvl;
                    }
                }

                /* Fallback to 3G WCDMA / 2G GSM if no LTE/NR */
                if (slots[cur_slot].rsrp == 0) {
                    char *wcdma = strstr(ss, "CellSignalStrengthWcdma");
                    if (wcdma) {
                        int rscp = parse_int_after(wcdma, "rscp", 2147483647);
                        if (rscp < 0 && rscp > -160) {
                            slots[cur_slot].rsrp = rscp;
                            if (strcmp(slots[cur_slot].network_type, "unknown") == 0) strcpy(slots[cur_slot].network_type, "3G");
                            int lvl = parse_int_after(wcdma, "level", -1);
                            if (lvl >= 0 && lvl <= 5) slots[cur_slot].level = lvl;
                        }
                    }
                }
            }

            /* Fallback mCellInfo for registered cell if rsrp is still 0 */
            if (slots[cur_slot].rsrp == 0) {
                char *ci_reg = strstr(line, "mRegistered=YES");
                if (ci_reg) {
                    char *ci_lte = strstr(ci_reg, "CellSignalStrengthLte:");
                    if (ci_lte) {
                        int rp = parse_int_after(ci_lte, "rsrp", 2147483647);
                        if (rp < 0 && rp > -160) {
                            slots[cur_slot].rsrp = rp;
                            if (strcmp(slots[cur_slot].network_type, "unknown") == 0) strcpy(slots[cur_slot].network_type, "LTE");
                            int rq = parse_int_after(ci_lte, "rsrq", 2147483647);
                            if (rq <= 0 && rq > -40) slots[cur_slot].rsrq = rq;
                            int sn = parse_int_after(ci_lte, "rssnr", 2147483647);
                            if (sn >= -30 && sn <= 50) slots[cur_slot].sinr = sn;
                        }
                    }
                }
            }
        }
        pclose(p);
    }

    /* Final validation for active slot */
    if (!slots[0].inserted && slots[1].inserted) active_slot = 1;
    else if (slots[0].inserted && !slots[1].inserted) active_slot = 0;

    if (out_active_slot) *out_active_slot = active_slot;
    if (out_active_subid) *out_active_subid = (sub > 0 ? sub : (active_slot + 1));

    /* Allowed network types */
    if (out_allowed_types && allowed_types_len > 0) {
        out_allowed_types[0] = '\0';
        char cmd[128];
        snprintf(cmd, sizeof(cmd), "cmd phone get-allowed-network-types-for-users -s %d 2>/dev/null", active_slot);
        if (read_cmd_line(cmd, out_allowed_types, allowed_types_len) != 0 || strlen(out_allowed_types) == 0) {
            if (read_cmd_line("cmd phone get-allowed-network-types-for-users 2>/dev/null", out_allowed_types, allowed_types_len) != 0 || strlen(out_allowed_types) == 0) {
                snprintf(cmd, sizeof(cmd), "settings get global preferred_network_mode%d 2>/dev/null", active_slot);
                if (read_cmd_line(cmd, out_allowed_types, allowed_types_len) != 0 || strlen(out_allowed_types) == 0) {
                    read_cmd_line("settings get global preferred_network_mode 2>/dev/null", out_allowed_types, allowed_types_len);
                }
            }
        }
    }
}

static void emit_sim_json(const SimSlotInfo slots[2], int active_slot, int active_subid) {
    printf("\"sim\":{");
    printf("\"active_slot\":%d,", active_slot);
    printf("\"active_subid\":%d,", active_subid);
    for (int i = 0; i < 2; i++) {
        printf("\"slot%d\":{", i);
        printf("\"inserted\":%s,", slots[i].inserted ? "true" : "false");
        printf("\"state\":"); json_print_escaped(slots[i].state); printf(",");
        printf("\"operator\":"); json_print_escaped(slots[i].operator_name); printf(",");
        printf("\"network_type\":"); json_print_escaped(slots[i].network_type); printf(",");
        printf("\"rsrp\":%d,", slots[i].rsrp);
        printf("\"rsrq\":%d,", slots[i].rsrq);
        printf("\"sinr\":%d,", slots[i].sinr);
        printf("\"level\":%d,", slots[i].level);
        printf("\"cell_id\":%lld,", slots[i].cell_id);
        printf("\"pci\":%d,", slots[i].pci);
        printf("\"band\":%d,", slots[i].band);
        printf("\"data_enabled\":%s,", slots[i].data_enabled ? "true" : "false");
        printf("\"data_state\":"); json_print_escaped(slots[i].data_state);
        printf("}%s", i == 0 ? "," : "");
    }
    printf("}");
}

static void emit_cellular_json(const SimSlotInfo slots[2], int active_slot, const char *allowed_types, int *out_rsrp, int *out_sinr) {
    int s = (active_slot >= 0 && active_slot < 2) ? active_slot : 0;
    if (!slots[s].inserted && slots[1 - s].inserted) s = 1 - s;

    if (out_rsrp) *out_rsrp = slots[s].rsrp;
    if (out_sinr) *out_sinr = slots[s].sinr;

    printf("\"cellular\":{");
    printf("\"operator\":"); json_print_escaped(slots[s].operator_name[0] ? slots[s].operator_name : "unknown"); printf(",");
    printf("\"network_type\":"); json_print_escaped(slots[s].network_type); printf(",");
    printf("\"rsrp\":%d,", slots[s].rsrp);
    printf("\"rsrq\":%d,", slots[s].rsrq);
    printf("\"sinr\":%d,", slots[s].sinr);
    printf("\"level\":%d,", slots[s].level);
    printf("\"cell_id\":%lld,", slots[s].cell_id);
    printf("\"pci\":%d,", slots[s].pci);
    printf("\"band\":%d,", slots[s].band);
    printf("\"data_enabled\":%s,", slots[s].data_enabled ? "true" : "false");
    printf("\"data_state\":"); json_print_escaped(slots[s].data_state); printf(",");
    printf("\"allowed_types\":"); json_print_escaped(allowed_types);
    printf("}");
}

/* Intelligent Network Health & Quality Index */
static void get_network_health(int is_wifi, int is_cell, int wifi_rssi, int wifi_speed, int cell_rsrp, int cell_sinr) {
    int score = 0;
    const char *grade = "fair";
    char summary[128] = "No active network traffic detected";
    char recommendation[256] = "Default routing tables are configured";

    if (is_wifi) {
        score = 55;
        if (wifi_rssi != 0) {
            if (wifi_rssi >= -60) score += 25;
            else if (wifi_rssi >= -72) score += 15;
            else score += 5;
        } else score += 15;

        if (wifi_speed >= 300) score += 20;
        else if (wifi_speed >= 100) score += 15;
        else if (wifi_speed >= 40) score += 10;
        else score += 5;

        if (score >= 85) {
            grade = "optimal";
            snprintf(summary, sizeof(summary), "High-speed Wi-Fi connection (%d Mbps)", wifi_speed > 0 ? wifi_speed : 100);
            strcpy(recommendation, "Excellent signal clarity with minimal latency jitter.");
        } else if (score >= 70) {
            grade = "good";
            strcpy(summary, "Stable Wi-Fi connection");
            strcpy(recommendation, "Reliable signal coverage and good throughput.");
        } else {
            grade = "fair";
            strcpy(summary, "Low Wi-Fi signal strength");
            strcpy(recommendation, "Consider moving closer to the access point for improved stability.");
        }
    } else if (is_cell) {
        score = 50;
        if (cell_rsrp != 0) {
            if (cell_rsrp >= -85) score += 25;
            else if (cell_rsrp >= -100) score += 15;
            else score += 5;
        } else score += 10;

        if (cell_sinr >= 12) score += 25;
        else if (cell_sinr >= 5) score += 15;
        else score += 5;

        if (score >= 80) {
            grade = "optimal";
            strcpy(summary, "Strong cellular connection");
            strcpy(recommendation, "Optimal signal-to-noise ratio and stable tower reception.");
        } else if (score >= 65) {
            grade = "good";
            strcpy(summary, "Stable cellular connection");
            strcpy(recommendation, "Adequate signal strength and reliable data reception.");
        } else {
            grade = "fair";
            strcpy(summary, "Weak cellular signal");
            strcpy(recommendation, "Signal quality is degraded; reconnecting to the tower is recommended.");
        }
    } else {
        score = 20;
        grade = "poor";
        strcpy(summary, "No active internet route");
        strcpy(recommendation, "Please connect to a Wi-Fi access point or enable mobile data.");
    }

    if (score > 100) score = 100;
    if (score < 10) score = 10;

    printf("\"health\":{");
    printf("\"score\":%d,", score);
    printf("\"grade\":"); json_print_escaped(grade); printf(",");
    printf("\"summary\":"); json_print_escaped(summary); printf(",");
    printf("\"recommendation\":"); json_print_escaped(recommendation);
    printf("}");
}

/* Comprehensive System & Network Info */
static int cmd_info(void) {
    printf("{");

    /* 1. Device hardware intelligence */
    char ram_tier[32] = "standard";
    get_device_details(ram_tier, sizeof(ram_tier));
    printf(",");

    /* 2. SIM & Cellular details */
    SimSlotInfo sim_slots[2];
    int active_slot = 0;
    int active_subid = 1;
    char allowed_types[256] = "";
    int cell_rsrp = 0, cell_sinr = 0;
    get_all_sim_and_cellular_details(sim_slots, &active_slot, &active_subid, allowed_types, sizeof(allowed_types));

    emit_sim_json(sim_slots, active_slot, active_subid);
    printf(",");

    /* 3. Wi-Fi */
    int wifi_rssi = 0, wifi_speed = 0;
    get_wifi_details(&wifi_rssi, &wifi_speed);
    printf(",");

    /* 4. Cellular */
    emit_cellular_json(sim_slots, active_slot, allowed_types, &cell_rsrp, &cell_sinr);
    printf(",");

    /* 5. Default route via ip route get (highest precision on Android) */
    char def_gateway[64] = "";
    char def_iface[64] = "";
    char src_ip[64] = "";
    FILE *rf = popen("ip route get 1.1.1.1 2>/dev/null || ip route show table 0 2>/dev/null | grep -m1 'default via' || ip route show default 2>/dev/null", "r");
    if (rf) {
        char rline[512];
        if (fgets(rline, sizeof(rline), rf)) {
            char *via = strstr(rline, "via ");
            if (via) sscanf(via + 4, "%63s", def_gateway);
            char *dev = strstr(rline, "dev ");
            if (dev) sscanf(dev + 4, "%63s", def_iface);
            char *src = strstr(rline, "src ");
            if (src) sscanf(src + 4, "%63s", src_ip);
        }
        pclose(rf);
    }

    printf("\"network\":{");
    printf("\"gateway\":"); json_print_escaped(def_gateway); printf(",");
    printf("\"active_iface\":"); json_print_escaped(def_iface); printf(",");
    printf("\"local_ip\":"); json_print_escaped(src_ip);
    printf("},");

    /* 6. Network health diagnosis */
    int is_wifi = (strncmp(def_iface, "wlan", 4) == 0);
    int is_cell = (strncmp(def_iface, "rmnet", 5) == 0 || strncmp(def_iface, "ccmni", 5) == 0 || strncmp(def_iface, "pdp", 3) == 0);
    get_network_health(is_wifi, is_cell, wifi_rssi, wifi_speed, cell_rsrp, cell_sinr);
    printf(",");

    /* 7. TCP parameters */
    char tcp_cc[64] = "unknown";
    char tcp_avail[256] = "cubic reno";
    char fastopen[16] = "0";
    char mtu_probing[16] = "0";
    read_sysfs_line("/proc/sys/net/ipv4/tcp_congestion_control", tcp_cc, sizeof(tcp_cc));
    read_sysfs_line("/proc/sys/net/ipv4/tcp_available_congestion_control", tcp_avail, sizeof(tcp_avail));
    read_sysfs_line("/proc/sys/net/ipv4/tcp_fastopen", fastopen, sizeof(fastopen));
    read_sysfs_line("/proc/sys/net/ipv4/tcp_mtu_probing", mtu_probing, sizeof(mtu_probing));

    printf("\"tcp\":{");
    printf("\"current_cc\":"); json_print_escaped(tcp_cc); printf(",");
    printf("\"available_cc\":"); json_print_escaped(tcp_avail); printf(",");
    printf("\"fastopen\":%d,", atoi(fastopen));
    printf("\"mtu_probing\":%d", atoi(mtu_probing));
    printf("},");

    /* 8. Android system settings */
    char dns_mode[64] = "off";
    char dns_specifier[128] = "";
    char wifi_throttle[16] = "1";
    char mobile_data_always[16] = "0";

    read_cmd_line("settings get global private_dns_mode 2>/dev/null", dns_mode, sizeof(dns_mode));
    read_cmd_line("settings get global private_dns_specifier 2>/dev/null", dns_specifier, sizeof(dns_specifier));
    read_cmd_line("settings get global wifi_scan_throttle_enabled 2>/dev/null", wifi_throttle, sizeof(wifi_throttle));
    read_cmd_line("settings get global mobile_data_always_on 2>/dev/null", mobile_data_always, sizeof(mobile_data_always));
    int dpi_active = (system("iptables -t mangle -C POSTROUTING -p tcp --dport 443 --tcp-flags SYN,RST SYN -j TCPMSS --set-mss 536 >/dev/null 2>&1") == 0);

    printf("\"settings\":{");
    printf("\"private_dns_mode\":"); json_print_escaped(dns_mode); printf(",");
    printf("\"private_dns_specifier\":"); json_print_escaped(dns_specifier); printf(",");
    printf("\"wifi_scan_throttle\":%s,", strcmp(wifi_throttle, "1") == 0 ? "true" : "false");
    printf("\"mobile_data_always_on\":%s,", strcmp(mobile_data_always, "1") == 0 ? "true" : "false");
    printf("\"dpi_bypass\":%s", dpi_active ? "true" : "false");
    printf("}}\n");

    return 0;
}

/* Ping benchmark tool */
static int cmd_ping(const char *host, int count) {
    if (!host || !is_safe_input(host)) {
        printf("{\"error\":\"invalid_host\"}\n");
        return 1;
    }
    if (count <= 0 || count > 10) count = 3;

    char cmd[256];
    snprintf(cmd, sizeof(cmd), "/system/bin/ping -c %d -W 2 %s 2>&1", count, host);
    FILE *p = popen(cmd, "r");
    if (!p) {
        printf("{\"error\":\"ping_execution_failed\"}\n");
        return 1;
    }

    char line[256];
    int transmitted = count, received = 0;
    float min_ms = 0, avg_ms = 0, max_ms = 0, mdev_ms = 0;

    while (fgets(line, sizeof(line), p)) {
        if (strstr(line, "packets transmitted")) {
            sscanf(line, "%d packets transmitted, %d received", &transmitted, &received);
        }
        if (strstr(line, "min/avg/max")) {
            char *slash = strchr(line, '=');
            if (slash) {
                sscanf(slash + 1, " %f/%f/%f/%f", &min_ms, &avg_ms, &max_ms, &mdev_ms);
            }
        }
    }
    pclose(p);

    int loss = transmitted > 0 ? (int)(((transmitted - received) * 100) / transmitted) : 100;
    printf("{\"host\":\"%s\",\"transmitted\":%d,\"received\":%d,\"loss_pct\":%d,\"min_ms\":%.2f,\"avg_ms\":%.2f,\"max_ms\":%.2f,\"jitter_ms\":%.2f}\n",
           host, transmitted, received, loss, min_ms, avg_ms, max_ms, mdev_ms);
    return 0;
}

/* DNS Resolution Benchmark */
static int cmd_dns_bench(void) {
    const char *test_domain = "google.com";
    const char *providers[][2] = {
        {"Cloudflare", "1.1.1.1"},
        {"Google", "8.8.8.8"},
        {"Quad9", "9.9.9.9"},
        {"AdGuard", "94.140.14.14"},
        {"OpenDNS", "208.67.222.222"}
    };
    int count = sizeof(providers) / sizeof(providers[0]);

    printf("[");
    for (int i = 0; i < count; i++) {
        struct timespec start, end;
        clock_gettime(CLOCK_MONOTONIC, &start);

        int sock = socket(AF_INET, SOCK_DGRAM, 0);
        float latency = -1.0f;
        if (sock >= 0) {
            struct timeval tv = { .tv_sec = 2, .tv_usec = 0 };
            setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

            struct sockaddr_in serv_addr;
            memset(&serv_addr, 0, sizeof(serv_addr));
            serv_addr.sin_family = AF_INET;
            serv_addr.sin_port = htons(53);
            inet_pton(AF_INET, providers[i][1], &serv_addr.sin_addr);

            /* Standard DNS query for google.com (Type A) */
            unsigned char dns_query[] = {
                0x12, 0x34, /* ID */
                0x01, 0x00, /* Flags: Standard query, recursion desired */
                0x00, 0x01, /* QDCOUNT = 1 */
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                0x06, 'g', 'o', 'o', 'g', 'l', 'e',
                0x03, 'c', 'o', 'm',
                0x00,       /* Root label */
                0x00, 0x01, /* QTYPE = A */
                0x00, 0x01  /* QCLASS = IN */
            };

            if (sendto(sock, dns_query, sizeof(dns_query), 0, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) > 0) {
                unsigned char response[512];
                socklen_t addr_len = sizeof(serv_addr);
                if (recvfrom(sock, response, sizeof(response), 0, (struct sockaddr *)&serv_addr, &addr_len) > 0) {
                    clock_gettime(CLOCK_MONOTONIC, &end);
                    latency = (float)((end.tv_sec - start.tv_sec) * 1000.0 + (end.tv_nsec - start.tv_nsec) / 1000000.0);
                }
            }
            close(sock);
        }

        if (i > 0) printf(",");
        printf("{\"name\":\"%s\",\"ip\":\"%s\",\"domain\":\"%s\",\"latency_ms\":%.2f}",
               providers[i][0], providers[i][1], test_domain, latency);
    }
    printf("]\n");
    return 0;
}

/* Set Cellular Network Mode / Band Lock */
/* Set Cellular Network Mode / Band Lock across Android 10-16 */
static int cmd_set_mode(int slot, const char *mode) {
    if (!mode || !is_safe_input(mode)) {
        printf("{\"error\":\"invalid_mode\"}\n");
        return 1;
    }

    const char *bitmask = BM_GLOBAL_AUTO;
    int mode_id = 9;
    if (strcmp(mode, "5g_only") == 0) { bitmask = BM_5G_ONLY; mode_id = 24; }
    else if (strcmp(mode, "5g_lte") == 0) { bitmask = BM_5G_LTE; mode_id = 26; }
    else if (strcmp(mode, "lte_only") == 0) { bitmask = BM_LTE_ONLY; mode_id = 11; }
    else if (strcmp(mode, "3g_only") == 0) { bitmask = BM_3G_ONLY; mode_id = 12; }
    else if (strcmp(mode, "2g_only") == 0) { bitmask = BM_2G_ONLY; mode_id = 1; }
    else if (strcmp(mode, "auto") == 0) { bitmask = BM_GLOBAL_AUTO; mode_id = 9; }
    else {
        printf("{\"error\":\"unknown_mode\"}\n");
        return 1;
    }

    /* 1. Try modern cmd phone with slot option */
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "cmd phone set-allowed-network-types-for-users -s %d %s >/dev/null 2>&1", slot, bitmask);
    int ret = system(cmd);

    /* 2. Fallback: try without -s for single-SIM or older AOSP */
    if (ret != 0) {
        snprintf(cmd, sizeof(cmd), "cmd phone set-allowed-network-types-for-users %s >/dev/null 2>&1", bitmask);
        ret = system(cmd);
    }

    /* 3. Fallback: Android 10 global settings */
    if (ret != 0) {
        snprintf(cmd, sizeof(cmd), "settings put global preferred_network_mode%d %d >/dev/null 2>&1", slot, mode_id);
        system(cmd);
        snprintf(cmd, sizeof(cmd), "settings put global preferred_network_mode %d >/dev/null 2>&1", mode_id);
        ret = system(cmd);
    }

    ensure_dir(CONF_DIR);
    char conf_cmd[256];
    snprintf(conf_cmd, sizeof(conf_cmd), "echo '{\"preferred_mode\":\"%s\",\"slot\":%d}' > %s", mode, slot, CONF_FILE);
    system(conf_cmd);

    printf("{\"success\":%s,\"slot\":%d,\"mode\":\"%s\"}\n", ret == 0 ? "true" : "false", slot, mode);
    return 0;
}

/* Internal Silent Helpers */
static void internal_get_ram_tier(char *out_tier, size_t tier_len) {
    unsigned long mem_total_kb = 0;
    FILE *mf = fopen("/proc/meminfo", "r");
    if (mf) {
        char line[256];
        while (fgets(line, sizeof(line), mf)) {
            if (sscanf(line, "MemTotal: %lu kB", &mem_total_kb) == 1) break;
        }
        fclose(mf);
    }
    int ram_mb = (int)(mem_total_kb / 1024);
    int ram_gb = get_physical_ram_gb(ram_mb);
    const char *ram_tier = "standard";
    if (ram_gb <= 3) ram_tier = "low";
    else if (ram_gb >= 8) ram_tier = "high";

    if (out_tier && tier_len > 0) {
        strncpy(out_tier, ram_tier, tier_len - 1);
        out_tier[tier_len - 1] = '\0';
    }
}

static int internal_set_tcp_cc(const char *algo) {
    if (!algo || !is_safe_input(algo)) return -1;
    char avail[256];
    if (read_sysfs_line("/proc/sys/net/ipv4/tcp_available_congestion_control", avail, sizeof(avail)) != 0) return -1;
    if (!strstr(avail, algo)) return -1;
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "sysctl -w net.ipv4.tcp_congestion_control=%s >/dev/null 2>&1", algo);
    return system(cmd);
}

static int internal_set_tcp_profile(const char *profile) {
    if (!profile || !is_safe_input(profile)) return -1;
    int ret = 0;
    if (strcmp(profile, "gaming") == 0) {
        ret |= system("sysctl -w net.ipv4.tcp_rmem='4096 87380 4194304' >/dev/null 2>&1");
        ret |= system("sysctl -w net.ipv4.tcp_wmem='4096 16384 4194304' >/dev/null 2>&1");
        ret |= system("sysctl -w net.ipv4.tcp_notsent_lowat=16384 >/dev/null 2>&1");
        ret |= system("sysctl -w net.ipv4.tcp_low_latency=1 >/dev/null 2>&1");
    } else if (strcmp(profile, "throughput") == 0) {
        ret |= system("sysctl -w net.ipv4.tcp_rmem='8192 1048576 16777216' >/dev/null 2>&1");
        ret |= system("sysctl -w net.ipv4.tcp_wmem='8192 1048576 16777216' >/dev/null 2>&1");
        ret |= system("sysctl -w net.core.rmem_max=16777216 >/dev/null 2>&1");
        ret |= system("sysctl -w net.core.wmem_max=16777216 >/dev/null 2>&1");
        ret |= system("sysctl -w net.ipv4.tcp_window_scaling=1 >/dev/null 2>&1");
    } else if (strcmp(profile, "adaptive") == 0) {
        ret |= system("sysctl -w net.ipv4.tcp_rmem='4096 524288 8388608' >/dev/null 2>&1");
        ret |= system("sysctl -w net.ipv4.tcp_wmem='4096 524288 8388608' >/dev/null 2>&1");
        ret |= system("sysctl -w net.core.rmem_max=8388608 >/dev/null 2>&1");
        ret |= system("sysctl -w net.core.wmem_max=8388608 >/dev/null 2>&1");
        ret |= system("sysctl -w net.ipv4.tcp_window_scaling=1 >/dev/null 2>&1");
    } else if (strcmp(profile, "stock") == 0) {
        ret |= system("sysctl -w net.ipv4.tcp_rmem='4096 87380 6291456' >/dev/null 2>&1");
        ret |= system("sysctl -w net.ipv4.tcp_wmem='4096 16384 4194304' >/dev/null 2>&1");
        ret |= system("sysctl -w net.core.rmem_max=2097152 >/dev/null 2>&1");
        ret |= system("sysctl -w net.core.wmem_max=2097152 >/dev/null 2>&1");
    } else {
        return -1;
    }
    return ret;
}

static int internal_set_dns(const char *mode, const char *specifier) {
    if (!mode || !is_safe_input(mode)) return -1;
    char cmd[256];
    if (strcmp(mode, "hostname") == 0 && specifier && is_safe_input(specifier)) {
        snprintf(cmd, sizeof(cmd), "settings put global private_dns_specifier %s", specifier);
        system(cmd);
        return system("settings put global private_dns_mode hostname");
    } else if (strcmp(mode, "opportunistic") == 0) {
        return system("settings put global private_dns_mode opportunistic");
    } else {
        return system("settings put global private_dns_mode off");
    }
}

/* Set TCP Congestion Control */
static int cmd_set_tcp_cc(const char *algo) {
    int ret = internal_set_tcp_cc(algo);
    if (ret != 0) {
        printf("{\"error\":\"failed_setting_algorithm\"}\n");
        return 1;
    }
    printf("{\"success\":true,\"algorithm\":\"%s\"}\n", algo);
    return 0;
}

/* Set TCP Buffer Profiles */
static int cmd_set_tcp_profile(const char *profile) {
    int ret = internal_set_tcp_profile(profile);
    if (ret != 0) {
        printf("{\"error\":\"failed_setting_profile\"}\n");
        return 1;
    }
    printf("{\"success\":true,\"profile\":\"%s\"}\n", profile);
    return 0;
}

/* Set Android Private DNS */
static int cmd_set_dns(const char *mode, const char *specifier) {
    int ret = internal_set_dns(mode, specifier);
    if (ret != 0) {
        printf("{\"error\":\"failed_setting_dns\"}\n");
        return 1;
    }
    printf("{\"success\":true,\"mode\":\"%s\"}\n", mode);
    return 0;
}

/* Set System Network Tweaks */
static int cmd_set_tweak(const char *tweak, int val) {
    if (!tweak || !is_safe_input(tweak)) {
        printf("{\"error\":\"invalid_tweak\"}\n");
        return 1;
    }

    char cmd[256];
    if (strcmp(tweak, "wifi_throttle") == 0) {
        snprintf(cmd, sizeof(cmd), "settings put global wifi_scan_throttle_enabled %d", val ? 1 : 0);
        system(cmd);
    } else if (strcmp(tweak, "mobile_data_always") == 0) {
        snprintf(cmd, sizeof(cmd), "settings put global mobile_data_always_on %d", val ? 1 : 0);
        system(cmd);
    } else if (strcmp(tweak, "fast_open") == 0) {
        snprintf(cmd, sizeof(cmd), "sysctl -w net.ipv4.tcp_fastopen=%d >/dev/null 2>&1", val ? 3 : 0);
        system(cmd);
    } else {
        printf("{\"error\":\"unknown_tweak\"}\n");
        return 1;
    }

    printf("{\"success\":true,\"tweak\":\"%s\",\"value\":%d}\n", tweak, val);
    return 0;
}

/* Refresh Cellular Radio (Clean Tower Re-association) */
static int cmd_radio_refresh(void) {
    int ret = system("cmd connectivity airplane-mode enable && sleep 1 && cmd connectivity airplane-mode disable");
    printf("{\"success\":%s}\n", ret == 0 ? "true" : "false");
    return 0;
}

/* Subnets of BGP-blackholed adult/censored networks that cannot be bypassed with MSS clamping alone */
static const char *BLACKHOLED_SUBNETS[] = {
    "66.254.96.0/19",   /* Aylo / MindGeek (Pornhub, Redtube, YouPorn, Brazzers) */
    "64.210.128.0/19",  /* Reflected Networks */
    "216.18.160.0/19",  /* Reflected Networks */
    "208.99.64.0/19",   /* Reflected Networks */
    "209.239.160.0/20", /* Reflected Networks */
    "104.232.220.0/22", /* Aylo AS44144 */
    "31.223.188.0/23",  /* Aylo AS44144 */
    "104.194.213.0/24",
    "104.143.95.0/24",
    "104.232.218.0/24",
    "84.247.60.0/24",
    "45.82.199.0/24",
    "45.12.179.0/24",
    "89.33.245.0/24",
    NULL
};

static void setup_warp_tunnel(int enable) {
    if (enable) {
        /* Ensure persistent warp.conf exists */
        if (access("/data/adb/hypernet/warp.conf", F_OK) != 0) {
            system("python3 -c '\n"
                   "import subprocess, urllib.request, json, os\n"
                   "for wg in [\"/data/adb/modules/hypernet/system/bin/wg\", \"/data/data/com.termux/files/home/HyperNet_Module/system/bin/wg\", \"/data/data/com.termux/files/usr/bin/wg\"]:\n"
                   "    if os.path.isfile(wg) and os.access(wg, os.X_OK):\n"
                   "        break\n"
                   "else:\n"
                   "    wg = \"wg\"\n"
                   "try:\n"
                   "    priv = subprocess.check_output([wg, \"genkey\"]).decode().strip()\n"
                   "    p = subprocess.Popen([wg, \"pubkey\"], stdin=subprocess.PIPE, stdout=subprocess.PIPE)\n"
                   "    pub, _ = p.communicate(priv.encode())\n"
                   "    pub = pub.decode().strip()\n"
                   "    req = urllib.request.Request(\"https://api.cloudflareclient.com/v0a2158/reg\",\n"
                   "        data=json.dumps({\"install_id\":\"\",\"tos\":\"2020-04-20T00:00:00.000Z\",\"key\":pub,\"fcm_token\":\"\",\"type\":\"Android\",\"locale\":\"en_US\"}).encode(),\n"
                   "        headers={\"Content-Type\":\"application/json; charset=UTF-8\",\"User-Agent\":\"okhttp/3.12.1\"})\n"
                   "    with urllib.request.urlopen(req, timeout=10) as resp:\n"
                   "        res = json.loads(resp.read().decode())\n"
                   "    peer_pub = res[\"config\"][\"peers\"][0][\"public_key\"]\n"
                   "    os.makedirs(\"/data/adb/hypernet\", exist_ok=True)\n"
                   "    with open(\"/data/adb/hypernet/warp.conf\", \"w\") as f:\n"
                   "        f.write(f\"[Interface]\\nPrivateKey = {priv}\\n\\n[Peer]\\nPublicKey = {peer_pub}\\nEndpoint = 162.159.192.1:2408\\nAllowedIPs = 0.0.0.0/0\\n\")\n"
                   "    os.chmod(\"/data/adb/hypernet/warp.conf\", 0o600)\n"
                   "except Exception:\n"
                   "    pass\n"
                   "' >/dev/null 2>&1");
        }

        if (access("/data/adb/hypernet/warp.conf", F_OK) != 0) return;

        const char *wg = "/data/adb/modules/hypernet/system/bin/wg";
        if (access(wg, X_OK) != 0) wg = "/data/data/com.termux/files/home/HyperNet_Module/system/bin/wg";
        if (access(wg, X_OK) != 0) wg = "/data/data/com.termux/files/usr/bin/wg";

        char cmd[512];
        system("ip link del dev hypernet-warp >/dev/null 2>&1");
        system("ip link add dev hypernet-warp type wireguard >/dev/null 2>&1");
        snprintf(cmd, sizeof(cmd), "%s setconf hypernet-warp /data/adb/hypernet/warp.conf >/dev/null 2>&1", wg);
        system(cmd);
        system("ip addr add 172.16.0.2/32 dev hypernet-warp >/dev/null 2>&1");
        system("ip link set hypernet-warp up >/dev/null 2>&1");

        for (int i = 0; BLACKHOLED_SUBNETS[i] != NULL; i++) {
            snprintf(cmd, sizeof(cmd), "ip route add %s dev hypernet-warp table 1337 >/dev/null 2>&1", BLACKHOLED_SUBNETS[i]);
            system(cmd);
            snprintf(cmd, sizeof(cmd), "ip rule add to %s table 1337 priority 9000 >/dev/null 2>&1", BLACKHOLED_SUBNETS[i]);
            system(cmd);
        }

        system("iptables -t nat -C POSTROUTING -o hypernet-warp -j MASQUERADE >/dev/null 2>&1 || iptables -t nat -A POSTROUTING -o hypernet-warp -j MASQUERADE >/dev/null 2>&1");
    } else {
        for (int i = 0; BLACKHOLED_SUBNETS[i] != NULL; i++) {
            char cmd[256];
            snprintf(cmd, sizeof(cmd), "ip rule del to %s table 1337 priority 9000 >/dev/null 2>&1", BLACKHOLED_SUBNETS[i]);
            system(cmd);
        }
        system("ip route flush table 1337 >/dev/null 2>&1");
        system("iptables -t nat -D POSTROUTING -o hypernet-warp -j MASQUERADE >/dev/null 2>&1");
        system("ip link del dev hypernet-warp >/dev/null 2>&1");
    }
}

/* Anti-Censorship & DPI Bypass: TCP MSS packet fragmentation & DoT */
static int cmd_set_dpi_bypass(int enable) {
    if (enable) {
        /* Port 443 HTTPS — clamp MSS to split TLS ClientHello across segments */
        system("iptables -t mangle -C POSTROUTING -p tcp --dport 443 --tcp-flags SYN,RST SYN -j TCPMSS --set-mss 536 >/dev/null 2>&1 || iptables -t mangle -I POSTROUTING -p tcp --dport 443 --tcp-flags SYN,RST SYN -j TCPMSS --set-mss 536 >/dev/null 2>&1");
        system("ip6tables -t mangle -C POSTROUTING -p tcp --dport 443 --tcp-flags SYN,RST SYN -j TCPMSS --set-mss 536 >/dev/null 2>&1 || ip6tables -t mangle -I POSTROUTING -p tcp --dport 443 --tcp-flags SYN,RST SYN -j TCPMSS --set-mss 536 >/dev/null 2>&1");

        /* Port 80 HTTP — many sites redirect from HTTP first; ISP DPI can inspect plain Host header */
        system("iptables -t mangle -C POSTROUTING -p tcp --dport 80 --tcp-flags SYN,RST SYN -j TCPMSS --set-mss 536 >/dev/null 2>&1 || iptables -t mangle -I POSTROUTING -p tcp --dport 80 --tcp-flags SYN,RST SYN -j TCPMSS --set-mss 536 >/dev/null 2>&1");
        system("ip6tables -t mangle -C POSTROUTING -p tcp --dport 80 --tcp-flags SYN,RST SYN -j TCPMSS --set-mss 536 >/dev/null 2>&1 || ip6tables -t mangle -I POSTROUTING -p tcp --dport 80 --tcp-flags SYN,RST SYN -j TCPMSS --set-mss 536 >/dev/null 2>&1");

        /* Block QUIC (HTTP/3 UDP 443) — forces browser fallback to TCP/TLS where MSS clamping works.
         * ponytail: DROP not REJECT so browser degrades to TCP quickly rather than waiting for RST */
        system("iptables -C OUTPUT -p udp --dport 443 -j DROP >/dev/null 2>&1 || iptables -I OUTPUT -p udp --dport 443 -j DROP >/dev/null 2>&1");
        system("ip6tables -C OUTPUT -p udp --dport 443 -j DROP >/dev/null 2>&1 || ip6tables -I OUTPUT -p udp --dport 443 -j DROP >/dev/null 2>&1");

        /* If private DNS is off, automatically enable Cloudflare Anti-Censorship DNS */
        char curr_dns[64] = "";
        read_cmd_line("settings get global private_dns_mode 2>/dev/null", curr_dns, sizeof(curr_dns));
        if (strcmp(curr_dns, "off") == 0 || strlen(curr_dns) == 0) {
            internal_set_dns("hostname", "1dot1dot1dot1.cloudflare-dns.com");
        }

        /* Native WireGuard tunnel for BGP-blackholed networks (Pornhub, Redtube, etc.) */
        setup_warp_tunnel(1);

        system("mkdir -p /data/adb/modules/hypernet 2>/dev/null; echo '1' > /data/adb/modules/hypernet/dpi_bypass.conf; mkdir -p /data/adb/hypernet 2>/dev/null; echo '1' > /data/adb/hypernet/dpi_bypass.conf");
        printf("{\"success\":true,\"dpi_bypass\":true}\n");
    } else {
        system("iptables -t mangle -D POSTROUTING -p tcp --dport 443 --tcp-flags SYN,RST SYN -j TCPMSS --set-mss 536 >/dev/null 2>&1");
        system("ip6tables -t mangle -D POSTROUTING -p tcp --dport 443 --tcp-flags SYN,RST SYN -j TCPMSS --set-mss 536 >/dev/null 2>&1");
        system("iptables -t mangle -D POSTROUTING -p tcp --dport 80 --tcp-flags SYN,RST SYN -j TCPMSS --set-mss 536 >/dev/null 2>&1");
        system("ip6tables -t mangle -D POSTROUTING -p tcp --dport 80 --tcp-flags SYN,RST SYN -j TCPMSS --set-mss 536 >/dev/null 2>&1");
        system("iptables -D OUTPUT -p udp --dport 443 -j DROP >/dev/null 2>&1");
        system("ip6tables -D OUTPUT -p udp --dport 443 -j DROP >/dev/null 2>&1");

        /* Tear down native WireGuard tunnel */
        setup_warp_tunnel(0);

        system("mkdir -p /data/adb/modules/hypernet 2>/dev/null; echo '0' > /data/adb/modules/hypernet/dpi_bypass.conf; mkdir -p /data/adb/hypernet 2>/dev/null; echo '0' > /data/adb/hypernet/dpi_bypass.conf");
        printf("{\"success\":true,\"dpi_bypass\":false}\n");
    }
    return 0;
}

/* Check Site Reachability (Pure POSIX socket probe for blocked domains like www.reddit.com) */
static int cmd_check_site(const char *domain) {
    if (!domain || !is_safe_input(domain)) {
        printf("{\"error\":\"invalid_domain\"}\n");
        return 1;
    }
    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(domain, "443", &hints, &res) != 0) {
        printf("{\"domain\":\"%s\",\"reachable\":false,\"reason\":\"dns_lookup_failed\"}\n", domain);
        return 0;
    }

    struct sockaddr_in *sin = (struct sockaddr_in *)res->ai_addr;
    char ip_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &(sin->sin_addr), ip_str, INET_ADDRSTRLEN);

    /* Check for known Indonesian ISP block landing IPs (TrustPositif / Internet Baik) */
    if (strncmp(ip_str, "127.", 4) == 0 || strcmp(ip_str, "0.0.0.0") == 0 ||
        strncmp(ip_str, "180.250.", 8) == 0 || strncmp(ip_str, "118.98.", 7) == 0) {
        freeaddrinfo(res);
        printf("{\"domain\":\"%s\",\"ip\":\"%s\",\"reachable\":false,\"reason\":\"dns_poisoned\"}\n", domain, ip_str);
        return 0;
    }

    int s = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (s < 0) {
        freeaddrinfo(res);
        printf("{\"domain\":\"%s\",\"reachable\":false,\"reason\":\"socket_creation_failed\"}\n", domain);
        return 0;
    }

    struct timeval tv = { .tv_sec = 3, .tv_usec = 0 };
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    int conn = connect(s, res->ai_addr, res->ai_addrlen);
    close(s);
    freeaddrinfo(res);

    if (conn == 0) {
        printf("{\"domain\":\"%s\",\"ip\":\"%s\",\"reachable\":true,\"status\":\"accessible\"}\n", domain, ip_str);
    } else {
        printf("{\"domain\":\"%s\",\"reachable\":false,\"reason\":\"connection_refused\"}\n", domain);
    }
    return 0;
}

/* Intelligent Auto-Tuner tailored to hardware specs and network health */
static int cmd_smart_optimize(void) {
    char ram_tier[32] = "standard";
    internal_get_ram_tier(ram_tier, sizeof(ram_tier));

    /* 1. Adaptive buffer sizing */
    const char *applied_profile = "adaptive";
    if (strcmp(ram_tier, "high") == 0) {
        internal_set_tcp_profile("throughput");
        applied_profile = "throughput (16MB)";
    } else if (strcmp(ram_tier, "low") == 0) {
        internal_set_tcp_profile("gaming");
        applied_profile = "compact (4MB)";
    } else {
        internal_set_tcp_profile("adaptive");
        applied_profile = "adaptive (8MB)";
    }

    /* 2. TCP congestion control auto-selection */
    char avail[256] = "";
    const char *chosen_cc = "cubic";
    if (read_sysfs_line("/proc/sys/net/ipv4/tcp_available_congestion_control", avail, sizeof(avail)) == 0) {
        if (strstr(avail, "bbr")) {
            chosen_cc = "bbr";
            internal_set_tcp_cc("bbr");
        } else if (strstr(avail, "cubic")) {
            chosen_cc = "cubic";
            internal_set_tcp_cc("cubic");
        }
    }

    /* 3. Safe TCP latency & MTU tuning */
    system("sysctl -w net.ipv4.tcp_slow_start_after_idle=0 >/dev/null 2>&1");
    system("sysctl -w net.ipv4.tcp_fastopen=3 >/dev/null 2>&1");
    system("sysctl -w net.ipv4.tcp_mtu_probing=1 >/dev/null 2>&1");
    system("sysctl -w net.ipv4.tcp_autocorking=1 >/dev/null 2>&1");
    system("sysctl -w net.ipv4.tcp_sack=1 >/dev/null 2>&1");
    system("sysctl -w net.ipv4.tcp_window_scaling=1 >/dev/null 2>&1");
    system("settings put global wifi_scan_throttle_enabled 0 >/dev/null 2>&1");

    /* 4. Smart DNS Resolver benchmark and auto-application */
    const char *dns_candidates[][3] = {
        {"Cloudflare", "1.1.1.1", "one.one.one.one"},
        {"Google", "8.8.8.8", "dns.google"},
        {"Quad9", "9.9.9.9", "dns.quad9.net"},
        {"AdGuard", "94.140.14.14", "dns.adguard-dns.com"}
    };
    int num_dns = sizeof(dns_candidates) / sizeof(dns_candidates[0]);
    float best_lat = 99999.0f;
    int best_idx = 0;

    for (int i = 0; i < num_dns; i++) {
        struct timespec s0, s1;
        clock_gettime(CLOCK_MONOTONIC, &s0);
        int sk = socket(AF_INET, SOCK_DGRAM, 0);
        if (sk >= 0) {
            struct timeval tv = { .tv_sec = 1, .tv_usec = 200000 };
            setsockopt(sk, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
            struct sockaddr_in sa;
            memset(&sa, 0, sizeof(sa));
            sa.sin_family = AF_INET;
            sa.sin_port = htons(53);
            inet_pton(AF_INET, dns_candidates[i][1], &sa.sin_addr);

            unsigned char q[] = {
                0x12, 0x34, 0x01, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                0x06, 'g', 'o', 'o', 'g', 'l', 'e', 0x03, 'c', 'o', 'm', 0x00, 0x00, 0x01, 0x00, 0x01
            };
            if (sendto(sk, q, sizeof(q), 0, (struct sockaddr *)&sa, sizeof(sa)) > 0) {
                unsigned char resp[512];
                socklen_t slen = sizeof(sa);
                if (recvfrom(sk, resp, sizeof(resp), 0, (struct sockaddr *)&sa, &slen) > 0) {
                    clock_gettime(CLOCK_MONOTONIC, &s1);
                    float lat = (float)((s1.tv_sec - s0.tv_sec) * 1000.0 + (s1.tv_nsec - s0.tv_nsec) / 1000000.0);
                    if (lat > 0 && lat < best_lat) {
                        best_lat = lat;
                        best_idx = i;
                    }
                }
            }
            close(sk);
        }
    }

    if (best_lat < 5000.0f) {
        internal_set_dns("hostname", dns_candidates[best_idx][2]);
    }

    /* Save persistent configuration */
    ensure_dir(CONF_DIR);
    char conf_cmd[512];
    snprintf(conf_cmd, sizeof(conf_cmd),
             "echo '{\"smart_optimized\":true,\"tcp_cc\":\"%s\",\"profile\":\"%s\",\"dns_host\":\"%s\"}' > %s",
             chosen_cc, applied_profile, best_lat < 5000.0f ? dns_candidates[best_idx][2] : "", CONF_FILE);
    system(conf_cmd);

    printf("{\"success\":true,\"ram_tier\":"); json_print_escaped(ram_tier); printf(",");
    printf("\"tcp_cc\":"); json_print_escaped(chosen_cc); printf(",");
    printf("\"buffer_profile\":"); json_print_escaped(applied_profile); printf(",");
    printf("\"best_dns\":"); json_print_escaped(dns_candidates[best_idx][0]); printf(",");
    printf("\"best_dns_latency_ms\":%.1f,", best_lat < 5000.0f ? best_lat : 0.0f);
    printf("\"tfo\":true,\"mtu_probing\":true}\n");

    return 0;
}

/* Apply boot profile from config file */
static int cmd_apply_boot(void) {
    ensure_dir(CONF_DIR);

    /* 1. Base kernel sysctls */
    system("sysctl -w net.ipv4.tcp_fastopen=3 >/dev/null 2>&1");
    system("sysctl -w net.ipv4.tcp_slow_start_after_idle=0 >/dev/null 2>&1");
    system("sysctl -w net.ipv4.tcp_mtu_probing=1 >/dev/null 2>&1");
    system("sysctl -w net.ipv4.tcp_autocorking=1 >/dev/null 2>&1");

    /* 2. Read saved config if exists */
    FILE *f = fopen(CONF_FILE, "r");
    if (f) {
        char buf[1024];
        if (fgets(buf, sizeof(buf), f)) {
            char *mode = strstr(buf, "\"preferred_mode\":\"");
            char *slot_str = strstr(buf, "\"slot\":");
            if (mode && slot_str) {
                mode += 18;
                char *end = strchr(mode, '"');
                if (end) {
                    *end = '\0';
                    int slot = atoi(slot_str + 7);
                    cmd_set_mode(slot, mode);
                }
            }
            char *cc = strstr(buf, "\"tcp_cc\":\"");
            if (cc) {
                cc += 10;
                char *end = strchr(cc, '"');
                if (end) {
                    *end = '\0';
                    cmd_set_tcp_cc(cc);
                }
            }
        }
        fclose(f);
    }

    /* 3. Restore DPI bypass if enabled */
    char dpi_cfg[16] = "";
    read_cmd_line("cat /data/adb/modules/hypernet/dpi_bypass.conf 2>/dev/null || cat /data/adb/hypernet/dpi_bypass.conf 2>/dev/null", dpi_cfg, sizeof(dpi_cfg));
    if (strcmp(dpi_cfg, "1") == 0) {
        cmd_set_dpi_bypass(1);
    }

    return 0;
}

/* Standalone CLI Speedtest Engine via Pure POSIX Sockets */
static int cmd_speedtest(int json_output, const char *server_id) {
    const char *target_host = "speed.cloudflare.com";
    const char *server_display = "Cloudflare Anycast";
    const char *down_path = "/__down?bytes=2500000";
    const char *up_path = "/__up";
    int can_upload = 1;
    int up_size = 500000;

    if (server_id && strcmp(server_id, "cf_stream") == 0) {
        server_display = "Cloudflare Streaming";
        down_path = "/__down?bytes=5000000";
        up_size = 800000;
    } else if (server_id && strcmp(server_id, "cf_latency") == 0) {
        server_display = "Cloudflare Low-Latency";
        down_path = "/__down?bytes=1000000";
        up_size = 250000;
    } else if (server_id && (strcmp(server_id, "tele2") == 0 || strcmp(server_id, "speedtest.tele2.net") == 0)) {
        target_host = "speedtest.tele2.net";
        server_display = "Tele2 Edge Global";
        down_path = "/1MB.zip";
        up_path = "/upload.php";
        up_size = 250000;
    }

    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(target_host, "80", &hints, &res) != 0) {
        if (json_output) printf("{\"error\":\"dns_lookup_failed\"}\n");
        else printf("error: cannot resolve %s\n", target_host);
        return 1;
    }

    /* 1. Latency measurement */
    float ping_ms = 0.0f;
    for (int i = 0; i < 3; i++) {
        int s = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
        if (s < 0) continue;
        struct timeval tv = { .tv_sec = 1, .tv_usec = 500000 };
        setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

        struct timespec t0, t1;
        clock_gettime(CLOCK_MONOTONIC, &t0);
        if (connect(s, res->ai_addr, res->ai_addrlen) == 0) {
            char req[256];
            snprintf(req, sizeof(req), "HEAD / HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n\r\n", target_host);
            send(s, req, strlen(req), 0);
            char buf[256];
            recv(s, buf, sizeof(buf), 0);
            clock_gettime(CLOCK_MONOTONIC, &t1);
            float rtt = (float)((t1.tv_sec - t0.tv_sec) * 1000.0 + (t1.tv_nsec - t0.tv_nsec) / 1000000.0);
            if (ping_ms == 0.0f || rtt < ping_ms) ping_ms = rtt;
        }
        close(s);
    }

    /* 2. Download benchmark with bounded duration */
    float down_mbps = 0.0f;
    int ds = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (ds >= 0) {
        struct timeval dtv = { .tv_sec = 2, .tv_usec = 500000 };
        setsockopt(ds, SOL_SOCKET, SO_RCVTIMEO, &dtv, sizeof(dtv));
        if (connect(ds, res->ai_addr, res->ai_addrlen) == 0) {
            char req[256];
            snprintf(req, sizeof(req), "GET %s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n\r\n", down_path, target_host);
            send(ds, req, strlen(req), 0);
            char recv_buf[16384];
            size_t total_bytes = 0;
            struct timespec st, ed;
            clock_gettime(CLOCK_MONOTONIC, &st);
            while (1) {
                ssize_t n = recv(ds, recv_buf, sizeof(recv_buf), 0);
                if (n <= 0) break;
                total_bytes += n;
                clock_gettime(CLOCK_MONOTONIC, &ed);
                float elapsed = (float)((ed.tv_sec - st.tv_sec) + (ed.tv_nsec - st.tv_nsec) / 1000000000.0);
                if (elapsed >= 2.5f && total_bytes >= 300000) break;
            }
            clock_gettime(CLOCK_MONOTONIC, &ed);
            float elapsed = (float)((ed.tv_sec - st.tv_sec) + (ed.tv_nsec - st.tv_nsec) / 1000000000.0);
            if (elapsed > 0.05f && total_bytes > 500) {
                down_mbps = (float)((total_bytes * 8.0) / (elapsed * 1000000.0));
            }
        }
        close(ds);
    }

    /* 3. Upload benchmark with bounded duration */
    float up_mbps = 0.0f;
    if (can_upload && up_path) {
        int us = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
        if (us >= 0) {
            struct timeval utv = { .tv_sec = 2, .tv_usec = 0 };
            setsockopt(us, SOL_SOCKET, SO_SNDTIMEO, &utv, sizeof(utv));
            if (connect(us, res->ai_addr, res->ai_addrlen) == 0) {
                char hdr[256];
                snprintf(hdr, sizeof(hdr), "POST %s HTTP/1.1\r\nHost: %s\r\nContent-Length: %d\r\nConnection: close\r\n\r\n", up_path, target_host, up_size);
                send(us, hdr, strlen(hdr), 0);
                char chunk[8192];
                memset(chunk, '0', sizeof(chunk));
                int remaining = up_size;
                struct timespec st, ed;
                clock_gettime(CLOCK_MONOTONIC, &st);
                while (remaining > 0) {
                    int to_send = remaining > (int)sizeof(chunk) ? (int)sizeof(chunk) : remaining;
                    ssize_t sent = send(us, chunk, to_send, 0);
                    if (sent <= 0) break;
                    remaining -= sent;
                    clock_gettime(CLOCK_MONOTONIC, &ed);
                    float elapsed = (float)((ed.tv_sec - st.tv_sec) + (ed.tv_nsec - st.tv_nsec) / 1000000000.0);
                    if (elapsed >= 1.8f && (up_size - remaining) >= 150000) break;
                }
                char resp[256];
                recv(us, resp, sizeof(resp), 0);
                clock_gettime(CLOCK_MONOTONIC, &ed);
                float elapsed = (float)((ed.tv_sec - st.tv_sec) + (ed.tv_nsec - st.tv_nsec) / 1000000000.0);
                if (elapsed > 0.05f) {
                    up_mbps = (float)(((up_size - remaining) * 8.0) / (elapsed * 1000000.0));
                }
            }
            close(us);
        }
    } else {
        up_mbps = down_mbps * 0.45f;
    }

    freeaddrinfo(res);

    if (json_output) {
        printf("{\"ping_ms\":%.1f,\"download_mbps\":%.1f,\"upload_mbps\":%.1f,\"server\":", ping_ms, down_mbps, up_mbps);
        json_print_escaped(server_display);
        printf("}\n");
    } else {
        printf("hypernet speedtest results:\n");
        printf("  latency:  %.1f ms\n", ping_ms);
        printf("  download: %.1f mbps\n", down_mbps);
        printf("  upload:   %.1f mbps\n", up_mbps);
        printf("  server:   %s\n", server_display);
    }
    return 0;
}

static void print_usage(void) {
    printf("hypernet - standalone android network toolkit & bridge\n\n");
    printf("usage: libhypernet.so <command> [args...]\n\n");
    printf("commands:\n");
    printf("  speedtest [--json] [srv] run standalone network speed test via socket\n");
    printf("  info                    full network, wi-fi, cellular, and tcp diagnostics\n");
    printf("  traffic                 per-interface rx/tx bytes from /proc/net/dev\n");
    printf("  ping <host> [count]     safe icmp/socket latency and loss measurement\n");
    printf("  dns_bench               dns resolution latency comparison\n");
    printf("  set_mode <slot> <mode>  lock cellular band mode (5g_only|5g_lte|lte_only|3g_only|2g_only|auto)\n");
    printf("  set_tcp_cc <algo>       switch tcp congestion control (bbr, cubic, reno)\n");
    printf("  set_tcp_profile <prof>  apply buffer profile (gaming|throughput|stock)\n");
    printf("  set_dns <mode> [host]   configure android private dns\n");
    printf("  set_tweak <name> <val>  toggle tweaks (wifi_throttle|mobile_data_always|fast_open)\n");
    printf("  set_dpi_bypass <0|1>    toggle anti-censorship DPI bypass (TCP MSS packet fragmentation)\n");
    printf("  check_site [domain]     probe web access reachability (default: www.reddit.com)\n");
    printf("  radio_refresh           toggle airplane mode to refresh cell tower attachment\n");
    printf("  smart_optimize          intelligent hardware-tailored network & dns auto-tuning\n");
    printf("  apply_boot              reapply saved network configurations on boot\n");
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        print_usage();
        return 0;
    }

    const char *cmd = argv[1];

    if (strcmp(cmd, "speedtest") == 0) {
        int json_out = 0;
        const char *server_id = "cf";
        for (int i = 2; i < argc; i++) {
            if (strcmp(argv[i], "--json") == 0) json_out = 1;
            else server_id = argv[i];
        }
        return cmd_speedtest(json_out, server_id);
    } else if (strcmp(cmd, "info") == 0 || strcmp(cmd, "status") == 0) {
        return cmd_info();
    } else if (strcmp(cmd, "traffic") == 0) {
        return cmd_traffic();
    } else if (strcmp(cmd, "ping") == 0) {
        const char *host = argc > 2 ? argv[2] : "1.1.1.1";
        int count = argc > 3 ? atoi(argv[3]) : 3;
        return cmd_ping(host, count);
    } else if (strcmp(cmd, "dns_bench") == 0) {
        return cmd_dns_bench();
    } else if (strcmp(cmd, "set_mode") == 0) {
        if (argc < 4) {
            printf("{\"error\":\"missing_arguments\"}\n");
            return 1;
        }
        return cmd_set_mode(atoi(argv[2]), argv[3]);
    } else if (strcmp(cmd, "set_tcp_cc") == 0) {
        if (argc < 3) {
            printf("{\"error\":\"missing_algorithm\"}\n");
            return 1;
        }
        return cmd_set_tcp_cc(argv[2]);
    } else if (strcmp(cmd, "set_tcp_profile") == 0) {
        if (argc < 3) {
            printf("{\"error\":\"missing_profile\"}\n");
            return 1;
        }
        return cmd_set_tcp_profile(argv[2]);
    } else if (strcmp(cmd, "set_dns") == 0) {
        if (argc < 3) {
            printf("{\"error\":\"missing_mode\"}\n");
            return 1;
        }
        const char *spec = argc > 3 ? argv[3] : NULL;
        return cmd_set_dns(argv[2], spec);
    } else if (strcmp(cmd, "set_tweak") == 0) {
        if (argc < 4) {
            printf("{\"error\":\"missing_arguments\"}\n");
            return 1;
        }
        return cmd_set_tweak(argv[2], atoi(argv[3]));
    } else if (strcmp(cmd, "set_dpi_bypass") == 0) {
        int enable = argc > 2 ? atoi(argv[2]) : 1;
        return cmd_set_dpi_bypass(enable);
    } else if (strcmp(cmd, "check_site") == 0) {
        const char *dom = argc > 2 ? argv[2] : "www.reddit.com";
        return cmd_check_site(dom);
    } else if (strcmp(cmd, "radio_refresh") == 0) {
        return cmd_radio_refresh();
    } else if (strcmp(cmd, "smart_optimize") == 0) {
        return cmd_smart_optimize();
    } else if (strcmp(cmd, "apply_boot") == 0) {
        return cmd_apply_boot();
    } else {
        print_usage();
        return 1;
    }
}
