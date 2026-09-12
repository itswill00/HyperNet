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
static void get_wifi_details(void) {
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
    const char *ram_tier = "standard";
    if (ram_mb < 3800) ram_tier = "low";
    else if (ram_mb > 8192) ram_tier = "high";

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
    printf("\"ram_tier\":"); json_print_escaped(ram_tier);
    printf("}");
}

/* Multi-SIM Slot Detection */
static void get_sim_details(int *out_active_slot) {
    char sim_states[64] = "absent,absent";
    char sim_ops[128] = "";
    char active_sub[16] = "1";

    read_cmd_line("getprop gsm.sim.state 2>/dev/null", sim_states, sizeof(sim_states));
    read_cmd_line("getprop gsm.sim.operator.alpha 2>/dev/null", sim_ops, sizeof(sim_ops));
    read_cmd_line("settings get global multi_sim_data_call 2>/dev/null", active_sub, sizeof(active_sub));

    char state0[32] = "absent", state1[32] = "absent";
    char op0[64] = "", op1[64] = "";

    char *comma = strchr(sim_states, ',');
    if (comma) {
        *comma = '\0';
        strncpy(state0, sim_states, sizeof(state0) - 1);
        strncpy(state1, comma + 1, sizeof(state1) - 1);
    } else {
        strncpy(state0, sim_states, sizeof(state0) - 1);
    }

    for (char *c = state0; *c; c++) *c = tolower((unsigned char)*c);
    for (char *c = state1; *c; c++) *c = tolower((unsigned char)*c);

    char *op_comma = strchr(sim_ops, ',');
    if (op_comma) {
        *op_comma = '\0';
        strncpy(op0, sim_ops, sizeof(op0) - 1);
        strncpy(op1, op_comma + 1, sizeof(op1) - 1);
    } else {
        strncpy(op0, sim_ops, sizeof(op0) - 1);
    }

    int active_slot = 0;
    if (strcmp(state0, "absent") == 0 && strcmp(state1, "absent") != 0) {
        active_slot = 1;
    } else if (strcmp(state0, "absent") != 0 && strcmp(state1, "absent") != 0) {
        int sub = atoi(active_sub);
        if (sub >= 2) active_slot = 1;
    }

    if (out_active_slot) *out_active_slot = active_slot;

    printf("\"sim\":{");
    printf("\"active_slot\":%d,", active_slot);
    printf("\"active_subid\":%d,", atoi(active_sub) > 0 ? atoi(active_sub) : (active_slot + 1));
    printf("\"slot0\":{\"inserted\":%s,\"state\":", strcmp(state0, "absent") != 0 ? "true" : "false");
    json_print_escaped(state0); printf(",\"operator\":"); json_print_escaped(op0); printf("},");
    printf("\"slot1\":{\"inserted\":%s,\"state\":", strcmp(state1, "absent") != 0 ? "true" : "false");
    json_print_escaped(state1); printf(",\"operator\":"); json_print_escaped(op1); printf("}");
    printf("}");
}

/* Parse Cellular details across Qualcomm, MediaTek, Exynos, and Tensor */
static void get_cellular_details(int *out_rsrp, int *out_sinr) {
    char operator_name[128] = "unknown";
    char network_type[64] = "unknown";
    int rsrp = 0;
    int rsrq = 0;
    int sinr = 0;
    int level = 0;
    long long cell_id = -1;
    char allowed_types[256] = "";

    /* Fast check via system properties */
    char prop_op[128] = "";
    char prop_type[128] = "";
    read_cmd_line("getprop gsm.sim.operator.alpha 2>/dev/null", prop_op, sizeof(prop_op));
    if (strlen(prop_op) > 0 && strcmp(prop_op, ",") != 0) {
        char *comma = strchr(prop_op, ',');
        if (comma && comma != prop_op) *comma = '\0';
        else if (comma && comma == prop_op && *(comma + 1) != '\0') {
            memmove(prop_op, comma + 1, strlen(comma + 1) + 1);
        }
        if (strlen(prop_op) > 0) strncpy(operator_name, prop_op, sizeof(operator_name) - 1);
    }

    read_cmd_line("getprop gsm.network.type 2>/dev/null", prop_type, sizeof(prop_type));
    if (strlen(prop_type) > 0 && strcmp(prop_type, "Unknown,Unknown") != 0) {
        char *comma = strchr(prop_type, ',');
        if (comma && comma != prop_type) *comma = '\0';
        else if (comma && comma == prop_type && *(comma + 1) != '\0') {
            memmove(prop_type, comma + 1, strlen(comma + 1) + 1);
        }
        if (strlen(prop_type) > 0 && strcmp(prop_type, "Unknown") != 0) {
            strncpy(network_type, prop_type, sizeof(network_type) - 1);
        }
    }

    /* Targeted dumpsys grep: fast and immune to line truncation */
    FILE *p = popen("dumpsys telephony.registry 2>/dev/null | grep -E 'mSignalStrength|CellSignalStrength|mCellIdentity|mOperatorAlpha' | head -n 40", "r");
    if (p) {
        char line[1024];
        while (fgets(line, sizeof(line), p)) {
            char *op = strstr(line, "mOperatorAlphaLong=");
            if (op) {
                op += 19;
                char *comma = strchr(op, ',');
                if (comma) *comma = '\0';
                if (strcmp(op, "null") != 0 && strlen(op) > 0) {
                    strncpy(operator_name, op, sizeof(operator_name) - 1);
                }
            }

            /* Match LTE */
            char *lte = strstr(line, "CellSignalStrengthLte");
            if (lte) {
                if (strcmp(network_type, "unknown") == 0) strcpy(network_type, "LTE");
                char *rp = strstr(lte, "rsrp");
                if (rp) {
                    char *eq = strchr(rp, '=');
                    if (eq) rsrp = atoi(eq + 1);
                }
                char *rq = strstr(lte, "rsrq");
                if (rq) {
                    char *eq = strchr(rq, '=');
                    if (eq) rsrq = atoi(eq + 1);
                }
                char *sn = strstr(lte, "rssnr");
                if (!sn) sn = strstr(lte, "sinr");
                if (sn) {
                    char *eq = strchr(sn, '=');
                    if (eq) sinr = atoi(eq + 1);
                }
                char *lv = strstr(lte, "level");
                if (lv) {
                    char *eq = strchr(lv, '=');
                    if (eq) level = atoi(eq + 1);
                }
            }

            /* Match 5G NR */
            char *nr = strstr(line, "CellSignalStrengthNr");
            if (nr) {
                strcpy(network_type, "5G NR");
                char *rp = strstr(nr, "ssRsrp");
                if (!rp) rp = strstr(nr, "csiRsrp");
                if (rp) {
                    char *eq = strchr(rp, '=');
                    if (eq) rsrp = atoi(eq + 1);
                }
                char *rq = strstr(nr, "ssRsrq");
                if (!rq) rq = strstr(nr, "csiRsrq");
                if (rq) {
                    char *eq = strchr(rq, '=');
                    if (eq) rsrq = atoi(eq + 1);
                }
                char *sn = strstr(nr, "ssSinr");
                if (!sn) sn = strstr(nr, "csiSinr");
                if (sn) {
                    char *eq = strchr(sn, '=');
                    if (eq) sinr = atoi(eq + 1);
                }
                char *lv = strstr(nr, "level");
                if (lv) {
                    char *eq = strchr(lv, '=');
                    if (eq) level = atoi(lv + 1);
                }
            }

            /* Cell ID */
            char *cid = strstr(line, "mCellIdentity");
            if (cid) {
                char *ci = strstr(cid, "mCi=");
                if (!ci) ci = strstr(cid, "mCid=");
                if (!ci) ci = strstr(cid, "cid=");
                if (ci) {
                    char *eq = strchr(ci, '=');
                    if (eq) cell_id = atoll(eq + 1);
                }
            }
        }
        pclose(p);
    }

    /* Try reading allowed network types */
    if (read_cmd_line("cmd phone get-allowed-network-types-for-users -s 0 2>/dev/null", allowed_types, sizeof(allowed_types)) != 0 || strlen(allowed_types) == 0) {
        if (read_cmd_line("cmd phone get-allowed-network-types-for-users 2>/dev/null", allowed_types, sizeof(allowed_types)) != 0 || strlen(allowed_types) == 0) {
            read_cmd_line("settings get global preferred_network_mode 2>/dev/null", allowed_types, sizeof(allowed_types));
        }
    }

    /* Treat sentinel values */
    if (rsrp == 2147483647 || rsrp > 0) rsrp = 0;
    if (rsrq == 2147483647 || rsrq > 0) rsrq = 0;
    if (sinr == 2147483647) sinr = 0;

    if (out_rsrp) *out_rsrp = rsrp;
    if (out_sinr) *out_sinr = sinr;

    printf("\"cellular\":{");
    printf("\"operator\":"); json_print_escaped(operator_name); printf(",");
    printf("\"network_type\":"); json_print_escaped(network_type); printf(",");
    printf("\"rsrp\":%d,", rsrp);
    printf("\"rsrq\":%d,", rsrq);
    printf("\"sinr\":%d,", sinr);
    printf("\"level\":%d,", level);
    printf("\"cell_id\":%lld,", cell_id);
    printf("\"allowed_types\":"); json_print_escaped(allowed_types);
    printf("}");
}

/* Intelligent Network Health & Quality Index */
static void get_network_health(int is_wifi, int is_cell, int wifi_rssi, int wifi_speed, int cell_rsrp, int cell_sinr) {
    int score = 0;
    const char *grade = "fair";
    char summary[128] = "Idle network connection";
    char recommendation[256] = "Network parameters active";

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
            strcpy(recommendation, "Low RF noise, peak bufferbloat resistance");
        } else if (score >= 70) {
            grade = "good";
            strcpy(summary, "Stable Wi-Fi link");
            strcpy(recommendation, "Good signal coverage");
        } else {
            grade = "fair";
            strcpy(summary, "Weak Wi-Fi signal");
            strcpy(recommendation, "Move closer to access point or enable roaming");
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
            strcpy(summary, "Strong cellular carrier link");
            strcpy(recommendation, "Clean RF SNR, optimal tower attachment");
        } else if (score >= 65) {
            grade = "good";
            strcpy(summary, "Good cellular link");
            strcpy(recommendation, "Adequate coverage");
        } else {
            grade = "fair";
            strcpy(summary, "Low cellular signal quality");
            strcpy(recommendation, "Tap 'Refresh tower' or switch to Wi-Fi");
        }
    } else {
        score = 20;
        grade = "poor";
        strcpy(summary, "No active default gateway route");
        strcpy(recommendation, "Connect to Wi-Fi or mobile data");
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

    /* 2. SIM slots */
    int active_slot = 0;
    get_sim_details(&active_slot);
    printf(",");

    /* 3. Wi-Fi */
    get_wifi_details();
    printf(",");

    /* 4. Cellular */
    int cell_rsrp = 0, cell_sinr = 0;
    get_cellular_details(&cell_rsrp, &cell_sinr);
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
    get_network_health(is_wifi, is_cell, 0, 0, cell_rsrp, cell_sinr);
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
    const char *ram_tier = "standard";
    if (ram_mb < 3800) ram_tier = "low";
    else if (ram_mb > 8192) ram_tier = "high";

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

/* Anti-Censorship & DPI Bypass: TCP MSS packet fragmentation & DoT */
static int cmd_set_dpi_bypass(int enable) {
    if (enable) {
        system("iptables -t mangle -C POSTROUTING -p tcp --dport 443 --tcp-flags SYN,RST SYN -j TCPMSS --set-mss 536 >/dev/null 2>&1 || iptables -t mangle -I POSTROUTING -p tcp --dport 443 --tcp-flags SYN,RST SYN -j TCPMSS --set-mss 536 >/dev/null 2>&1");
        system("ip6tables -t mangle -C POSTROUTING -p tcp --dport 443 --tcp-flags SYN,RST SYN -j TCPMSS --set-mss 536 >/dev/null 2>&1 || ip6tables -t mangle -I POSTROUTING -p tcp --dport 443 --tcp-flags SYN,RST SYN -j TCPMSS --set-mss 536 >/dev/null 2>&1");

        /* If private DNS is off, automatically enable Cloudflare Anti-Censorship DNS */
        char curr_dns[64] = "";
        read_cmd_line("settings get global private_dns_mode 2>/dev/null", curr_dns, sizeof(curr_dns));
        if (strcmp(curr_dns, "off") == 0 || strlen(curr_dns) == 0) {
            internal_set_dns("hostname", "1dot1dot1dot1.cloudflare-dns.com");
        }

        system("mkdir -p /data/adb/modules/hypernet 2>/dev/null; echo '1' > /data/adb/modules/hypernet/dpi_bypass.conf; mkdir -p /data/adb/hypernet 2>/dev/null; echo '1' > /data/adb/hypernet/dpi_bypass.conf");
        printf("{\"success\":true,\"dpi_bypass\":true}\n");
    } else {
        system("iptables -t mangle -D POSTROUTING -p tcp --dport 443 --tcp-flags SYN,RST SYN -j TCPMSS --set-mss 536 >/dev/null 2>&1");
        system("ip6tables -t mangle -D POSTROUTING -p tcp --dport 443 --tcp-flags SYN,RST SYN -j TCPMSS --set-mss 536 >/dev/null 2>&1");
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
        system("iptables -t mangle -C POSTROUTING -p tcp --dport 443 --tcp-flags SYN,RST SYN -j TCPMSS --set-mss 536 >/dev/null 2>&1 || iptables -t mangle -I POSTROUTING -p tcp --dport 443 --tcp-flags SYN,RST SYN -j TCPMSS --set-mss 536 >/dev/null 2>&1");
        system("ip6tables -t mangle -C POSTROUTING -p tcp --dport 443 --tcp-flags SYN,RST SYN -j TCPMSS --set-mss 536 >/dev/null 2>&1 || ip6tables -t mangle -I POSTROUTING -p tcp --dport 443 --tcp-flags SYN,RST SYN -j TCPMSS --set-mss 536 >/dev/null 2>&1");
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
