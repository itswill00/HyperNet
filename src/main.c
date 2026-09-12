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

/* Parse Cellular details from getprop and dumpsys */
static void get_cellular_details(void) {
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
        /* If comma present, pick first non-empty */
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

    FILE *p = popen("dumpsys telephony.registry 2>/dev/null | head -n 80", "r");
    if (p) {
        char line[512];
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

            char *lte = strstr(line, "CellSignalStrengthLte: ");
            if (lte) {
                if (strcmp(network_type, "unknown") == 0) strcpy(network_type, "LTE");
                char *rp = strstr(lte, "rsrp=");
                if (rp) rsrp = atoi(rp + 5);
                char *rq = strstr(lte, "rsrq=");
                if (rq) rsrq = atoi(rq + 5);
                char *sn = strstr(lte, "rssnr=");
                if (sn) sinr = atoi(sn + 6);
                char *lv = strstr(lte, "level=");
                if (lv) level = atoi(lv + 6);
            }

            char *nr = strstr(line, "CellSignalStrengthNr:{");
            if (nr) {
                strcpy(network_type, "5G NR");
                char *rp = strstr(nr, "ssRsrp = ");
                if (rp) rsrp = atoi(rp + 9);
                char *rq = strstr(nr, "ssRsrq = ");
                if (rq) rsrq = atoi(rq + 9);
                char *sn = strstr(nr, "ssSinr = ");
                if (sn) sinr = atoi(sn + 9);
                char *lv = strstr(nr, "level = ");
                if (lv) level = atoi(lv + 8);
            }

            char *cid = strstr(line, "mCellIdentity=");
            if (cid) {
                char *ci = strstr(cid, "mCi=");
                if (ci) cell_id = atoll(ci + 4);
            }
        }
        pclose(p);
    }

    read_cmd_line("cmd phone get-allowed-network-types-for-users -s 0 2>/dev/null", allowed_types, sizeof(allowed_types));

    /* Treat sentinel values */
    if (rsrp == 2147483647 || rsrp > 0) rsrp = 0;
    if (rsrq == 2147483647 || rsrq > 0) rsrq = 0;
    if (sinr == 2147483647) sinr = 0;

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

/* Comprehensive System & Network Info */
static int cmd_info(void) {
    printf("{");

    /* 1. Wi-Fi */
    get_wifi_details();
    printf(",");

    /* 2. Cellular */
    get_cellular_details();
    printf(",");

    /* 3. Default route and active interfaces */
    char def_gateway[64] = "";
    char def_iface[64] = "";
    FILE *rf = popen("ip route show table 0 2>/dev/null | grep -m1 'default via' || ip route show default 2>/dev/null", "r");
    if (rf) {
        char rline[256];
        if (fgets(rline, sizeof(rline), rf)) {
            if (sscanf(rline, "default via %63s dev %63s", def_gateway, def_iface) < 2) {
                /* Fallback if format is slightly different */
                char *via = strstr(rline, "via ");
                if (via) sscanf(via + 4, "%63s", def_gateway);
                char *dev = strstr(rline, "dev ");
                if (dev) sscanf(dev + 4, "%63s", def_iface);
            }
        }
        pclose(rf);
    }

    printf("\"network\":{");
    printf("\"gateway\":"); json_print_escaped(def_gateway); printf(",");
    printf("\"active_iface\":"); json_print_escaped(def_iface);
    printf("},");

    /* 4. TCP parameters */
    char tcp_cc[64] = "unknown";
    char tcp_avail[256] = "cubic reno";
    char fastopen[16] = "0";
    read_sysfs_line("/proc/sys/net/ipv4/tcp_congestion_control", tcp_cc, sizeof(tcp_cc));
    read_sysfs_line("/proc/sys/net/ipv4/tcp_available_congestion_control", tcp_avail, sizeof(tcp_avail));
    read_sysfs_line("/proc/sys/net/ipv4/tcp_fastopen", fastopen, sizeof(fastopen));

    printf("\"tcp\":{");
    printf("\"current_cc\":"); json_print_escaped(tcp_cc); printf(",");
    printf("\"available_cc\":"); json_print_escaped(tcp_avail); printf(",");
    printf("\"fastopen\":%d", atoi(fastopen));
    printf("},");

    /* 5. Android system settings */
    char dns_mode[64] = "off";
    char dns_specifier[128] = "";
    char wifi_throttle[16] = "1";
    char mobile_data_always[16] = "0";

    read_cmd_line("settings get global private_dns_mode 2>/dev/null", dns_mode, sizeof(dns_mode));
    read_cmd_line("settings get global private_dns_specifier 2>/dev/null", dns_specifier, sizeof(dns_specifier));
    read_cmd_line("settings get global wifi_scan_throttle_enabled 2>/dev/null", wifi_throttle, sizeof(wifi_throttle));
    read_cmd_line("settings get global mobile_data_always_on 2>/dev/null", mobile_data_always, sizeof(mobile_data_always));

    printf("\"settings\":{");
    printf("\"private_dns_mode\":"); json_print_escaped(dns_mode); printf(",");
    printf("\"private_dns_specifier\":"); json_print_escaped(dns_specifier); printf(",");
    printf("\"wifi_scan_throttle\":%s,", strcmp(wifi_throttle, "1") == 0 ? "true" : "false");
    printf("\"mobile_data_always_on\":%s", strcmp(mobile_data_always, "1") == 0 ? "true" : "false");
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
static int cmd_set_mode(int slot, const char *mode) {
    if (!mode || !is_safe_input(mode)) {
        printf("{\"error\":\"invalid_mode\"}\n");
        return 1;
    }

    const char *bitmask = BM_GLOBAL_AUTO;
    if (strcmp(mode, "5g_only") == 0) bitmask = BM_5G_ONLY;
    else if (strcmp(mode, "5g_lte") == 0) bitmask = BM_5G_LTE;
    else if (strcmp(mode, "lte_only") == 0) bitmask = BM_LTE_ONLY;
    else if (strcmp(mode, "3g_only") == 0) bitmask = BM_3G_ONLY;
    else if (strcmp(mode, "2g_only") == 0) bitmask = BM_2G_ONLY;
    else if (strcmp(mode, "auto") == 0) bitmask = BM_GLOBAL_AUTO;
    else {
        printf("{\"error\":\"unknown_mode\"}\n");
        return 1;
    }

    char cmd[256];
    snprintf(cmd, sizeof(cmd), "cmd phone set-allowed-network-types-for-users -s %d %s >/dev/null 2>&1", slot, bitmask);
    int ret = system(cmd);

    ensure_dir(CONF_DIR);
    char conf_cmd[256];
    snprintf(conf_cmd, sizeof(conf_cmd), "echo '{\"preferred_mode\":\"%s\",\"slot\":%d}' > %s", mode, slot, CONF_FILE);
    system(conf_cmd);

    printf("{\"success\":%s,\"slot\":%d,\"mode\":\"%s\"}\n", ret == 0 ? "true" : "false", slot, mode);
    return 0;
}

/* Set TCP Congestion Control */
static int cmd_set_tcp_cc(const char *algo) {
    if (!algo || !is_safe_input(algo)) {
        printf("{\"error\":\"invalid_algorithm\"}\n");
        return 1;
    }

    char avail[256];
    if (read_sysfs_line("/proc/sys/net/ipv4/tcp_available_congestion_control", avail, sizeof(avail)) != 0) {
        printf("{\"error\":\"failed_reading_available_cc\"}\n");
        return 1;
    }

    if (!strstr(avail, algo)) {
        printf("{\"error\":\"algorithm_not_supported_by_kernel\"}\n");
        return 1;
    }

    char cmd[256];
    snprintf(cmd, sizeof(cmd), "sysctl -w net.ipv4.tcp_congestion_control=%s >/dev/null 2>&1", algo);
    int ret = system(cmd);

    printf("{\"success\":%s,\"algorithm\":\"%s\"}\n", ret == 0 ? "true" : "false", algo);
    return 0;
}

/* Set TCP Buffer Profiles */
static int cmd_set_tcp_profile(const char *profile) {
    if (!profile || !is_safe_input(profile)) {
        printf("{\"error\":\"invalid_profile\"}\n");
        return 1;
    }

    int ret = 0;
    if (strcmp(profile, "gaming") == 0) {
        /* Low latency, minimal queuing */
        ret |= system("sysctl -w net.ipv4.tcp_rmem='4096 87380 4194304' >/dev/null 2>&1");
        ret |= system("sysctl -w net.ipv4.tcp_wmem='4096 16384 4194304' >/dev/null 2>&1");
        ret |= system("sysctl -w net.ipv4.tcp_notsent_lowat=16384 >/dev/null 2>&1");
        ret |= system("sysctl -w net.ipv4.tcp_low_latency=1 >/dev/null 2>&1");
    } else if (strcmp(profile, "throughput") == 0) {
        /* Expanded buffers for maximum streaming speed */
        ret |= system("sysctl -w net.ipv4.tcp_rmem='8192 1048576 16777216' >/dev/null 2>&1");
        ret |= system("sysctl -w net.ipv4.tcp_wmem='8192 1048576 16777216' >/dev/null 2>&1");
        ret |= system("sysctl -w net.core.rmem_max=16777216 >/dev/null 2>&1");
        ret |= system("sysctl -w net.core.wmem_max=16777216 >/dev/null 2>&1");
        ret |= system("sysctl -w net.ipv4.tcp_window_scaling=1 >/dev/null 2>&1");
    } else if (strcmp(profile, "stock") == 0) {
        /* Standard kernel defaults */
        ret |= system("sysctl -w net.ipv4.tcp_rmem='4096 87380 6291456' >/dev/null 2>&1");
        ret |= system("sysctl -w net.ipv4.tcp_wmem='4096 16384 4194304' >/dev/null 2>&1");
        ret |= system("sysctl -w net.core.rmem_max=2097152 >/dev/null 2>&1");
        ret |= system("sysctl -w net.core.wmem_max=2097152 >/dev/null 2>&1");
    } else {
        printf("{\"error\":\"unknown_profile\"}\n");
        return 1;
    }

    printf("{\"success\":%s,\"profile\":\"%s\"}\n", ret == 0 ? "true" : "false", profile);
    return 0;
}

/* Set Android Private DNS */
static int cmd_set_dns(const char *mode, const char *specifier) {
    if (!mode || !is_safe_input(mode)) {
        printf("{\"error\":\"invalid_mode\"}\n");
        return 1;
    }

    char cmd[256];
    if (strcmp(mode, "hostname") == 0 && specifier && is_safe_input(specifier)) {
        snprintf(cmd, sizeof(cmd), "settings put global private_dns_specifier %s", specifier);
        system(cmd);
        system("settings put global private_dns_mode hostname");
    } else if (strcmp(mode, "opportunistic") == 0) {
        system("settings put global private_dns_mode opportunistic");
    } else {
        system("settings put global private_dns_mode off");
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

/* Apply boot profile from config file */
static int cmd_apply_boot(void) {
    ensure_dir(CONF_DIR);
    /* Safe default tuning on boot if file doesn't specify otherwise */
    system("sysctl -w net.ipv4.tcp_fastopen=3 >/dev/null 2>&1");
    return 0;
}

static void print_usage(void) {
    printf("hypernet - standalone android network toolkit & bridge\n\n");
    printf("usage: libhypernet.so <command> [args...]\n\n");
    printf("commands:\n");
    printf("  info                    full network, wi-fi, cellular, and tcp diagnostics\n");
    printf("  traffic                 per-interface rx/tx bytes from /proc/net/dev\n");
    printf("  ping <host> [count]     safe icmp/socket latency and loss measurement\n");
    printf("  dns_bench               dns resolution latency comparison\n");
    printf("  set_mode <slot> <mode>  lock cellular band mode (5g_only|5g_lte|lte_only|3g_only|2g_only|auto)\n");
    printf("  set_tcp_cc <algo>       switch tcp congestion control (bbr, cubic, reno)\n");
    printf("  set_tcp_profile <prof>  apply buffer profile (gaming|throughput|stock)\n");
    printf("  set_dns <mode> [host]   configure android private dns\n");
    printf("  set_tweak <name> <val>  toggle tweaks (wifi_throttle|mobile_data_always|fast_open)\n");
    printf("  radio_refresh           toggle airplane mode to refresh cell tower attachment\n");
    printf("  apply_boot              reapply saved network configurations on boot\n");
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        print_usage();
        return 0;
    }

    const char *cmd = argv[1];

    if (strcmp(cmd, "info") == 0 || strcmp(cmd, "status") == 0) {
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
    } else if (strcmp(cmd, "radio_refresh") == 0) {
        return cmd_radio_refresh();
    } else if (strcmp(cmd, "apply_boot") == 0) {
        return cmd_apply_boot();
    } else {
        print_usage();
        return 1;
    }
}
