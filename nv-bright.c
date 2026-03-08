/*
 * nv-bright: OLED brightness control via DPCD AUX on NVIDIA GPU
 *
 * Controls panel brightness by writing PANEL_TARGET_LUMINANCE (DPCD 0x734)
 * via the NVIDIA RM AUX channel API. Works on eDP OLED panels with
 * PANEL_LUMINANCE_CONTROL_CAP (DPCD 0x703 bit 4) in NVIDIA dGPU mode.
 *
 * Usage:
 *   nv-bright              Show current state
 *   nv-bright watch [min_nits] [max_nits]  Daemon: map nvidia_0 sysfs → DPCD luminance
 *   nv-bright lum <value>  Set TARGET_LUMINANCE (milli-cd/m²)
 *   nv-bright dump         Dump eDP DPCD registers
 *   nv-bright read <addr> [n]     Read DPCD register
 *   nv-bright raw <addr> <bytes>  Write raw DPCD bytes
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <math.h>
#include <poll.h>

#define NV_ESC_RM_ALLOC    0x2B
#define NV_ESC_RM_CONTROL  0x2A
#define NV_ESC_REGISTER_FD 201

#define NV01_ROOT_CLIENT       0x41
#define NV01_DEVICE_0          0x80
#define NV04_DISPLAY_COMMON    0x73

#define CMD_GET_SUPPORTED     0x730107
#define CMD_GET_CONNECT_STATE 0x730108
#define CMD_DP_AUXCH_CTRL     0x731341

#define CLIENT_HANDLE  0xBB000001
#define DEVICE_HANDLE  0xBB000002
#define DISPLAY_HANDLE 0xBB000003

/* RM API structs */
typedef struct { uint32_t hRoot, hObjectParent, hObjectNew; int32_t hClass;
                 uint64_t pAllocParms; uint32_t paramsSize; int32_t status; } NVOS21;
typedef struct { uint32_t hClient, hObject; int32_t cmd; uint32_t flags;
                 uint64_t params; uint32_t paramsSize; int32_t status; } NVOS54;
typedef struct { uint32_t deviceId, hClientShare, hTargetClient, hTargetDevice;
                 int32_t flags; uint32_t _p; uint64_t vaSpaceSize, vaStartInternal,
                 vaLimitInternal; int32_t vaMode; uint32_t _p2; } NV0080;
typedef struct { uint32_t subDeviceInstance, displayMask, displayMaskDDC; } GET_SUP;
typedef struct { uint32_t subDeviceInstance, flags, displayMask, retryTimeMs; } GET_CON;
typedef struct { uint32_t subDeviceInstance, displayId; uint8_t bAddrOnly, pad[3];
                 uint32_t cmd, addr; uint8_t data[16]; uint32_t size, replyType,
                 retryTimeMs; } AUXCH;
typedef struct { int ctl_fd; } REG_FD;

static int ctl_fd, dev_fd;
static int quiet;  /* suppress verbose output */

static int rm_alloc(int fd, uint32_t r, uint32_t p, uint32_t n, int32_t c, void *a, uint32_t s) {
    NVOS21 x = {r, p, n, c, (uint64_t)(uintptr_t)a, s, 0};
    if (ioctl(fd, _IOWR('F', 0x2B, NVOS21), &x) < 0 || x.status) return x.status ? x.status : -1;
    return 0;
}

static int rm_ctrl(uint32_t o, int32_t c, void *p, uint32_t s) {
    NVOS54 x = {CLIENT_HANDLE, o, c, 0, (uint64_t)(uintptr_t)p, s, 0};
    if (ioctl(ctl_fd, _IOWR('F', 0x2A, NVOS54), &x) < 0 || x.status) return x.status ? x.status : -1;
    return 0;
}

static int dpcd_read(uint32_t did, uint32_t addr, uint8_t *buf, uint32_t sz) {
    AUXCH a = {0}; a.displayId = did; a.cmd = 0x09; a.addr = addr; a.size = sz > 16 ? 16 : sz;
    int r = rm_ctrl(DISPLAY_HANDLE, CMD_DP_AUXCH_CTRL, &a, sizeof(a));
    if (r) {
        if (!quiet) fprintf(stderr, "  DPCD read 0x%04x failed: 0x%x\n", addr, r);
        return r;
    }
    if (buf) memcpy(buf, a.data, a.size > sz ? sz : a.size);
    if (!quiet) {
        printf("  DPCD 0x%04x:", addr);
        for (uint32_t i = 0; i < a.size; i++) printf(" %02x", a.data[i]);
        printf("\n");
    }
    return 0;
}

static int dpcd_write(uint32_t did, uint32_t addr, const uint8_t *buf, uint32_t sz) {
    AUXCH a = {0}; a.displayId = did; a.cmd = 0x08; a.addr = addr; a.size = sz > 16 ? 16 : sz;
    memcpy(a.data, buf, a.size);
    return rm_ctrl(DISPLAY_HANDLE, CMD_DP_AUXCH_CTRL, &a, sizeof(a));
}

static int init_rm(void) {
    ctl_fd = open("/dev/nvidiactl", O_RDWR);
    dev_fd = open("/dev/nvidia0", O_RDWR);
    if (ctl_fd < 0 || dev_fd < 0) { perror("open nvidia device"); return -1; }

    REG_FD reg = { ctl_fd };
    ioctl(dev_fd, _IOWR('F', NV_ESC_REGISTER_FD, REG_FD), &reg);

    if (rm_alloc(ctl_fd, CLIENT_HANDLE, CLIENT_HANDLE, CLIENT_HANDLE, NV01_ROOT_CLIENT, NULL, 0))
        { fprintf(stderr, "nv-bright: RM client alloc failed\n"); return -1; }

    NV0080 dp = {0};
    if (rm_alloc(ctl_fd, CLIENT_HANDLE, CLIENT_HANDLE, DEVICE_HANDLE, NV01_DEVICE_0, &dp, sizeof(dp)))
        { fprintf(stderr, "nv-bright: RM device alloc failed\n"); return -1; }

    if (rm_alloc(ctl_fd, CLIENT_HANDLE, DEVICE_HANDLE, DISPLAY_HANDLE, NV04_DISPLAY_COMMON, NULL, 0))
        { fprintf(stderr, "nv-bright: RM display alloc failed\n"); return -1; }

    return 0;
}

static uint32_t find_edp(void) {
    GET_SUP s = {0};
    if (rm_ctrl(DISPLAY_HANDLE, CMD_GET_SUPPORTED, &s, sizeof(s))) return 0;

    GET_CON c = {0}; c.displayMask = s.displayMask; c.flags = 1;
    if (rm_ctrl(DISPLAY_HANDLE, CMD_GET_CONNECT_STATE, &c, sizeof(c))) return 0;

    for (int bit = 0; bit < 32; bit++) {
        uint32_t did = 1u << bit;
        if (!(c.displayMask & did)) continue;
        uint8_t cap[4] = {0};
        if (dpcd_read(did, 0x700, cap, 4) == 0 && (cap[2] & 0x02))
            return did;
    }
    return 0;
}

static int set_luminance(uint32_t edp, uint32_t mcd) {
    uint8_t b[3] = { mcd & 0xFF, (mcd >> 8) & 0xFF, (mcd >> 16) & 0xFF };
    return dpcd_write(edp, 0x734, b, 3);
}

static uint32_t get_luminance(uint32_t edp) {
    uint8_t b[3] = {0};
    dpcd_read(edp, 0x734, b, 3);
    return b[0] | (b[1] << 8) | (b[2] << 16);
}

int main(int argc, char **argv)
{
    quiet = (argc >= 2 && strcmp(argv[1], "watch") == 0);

    if (init_rm()) return 1;

    uint32_t edp = find_edp();
    if (!edp) { fprintf(stderr, "nv-bright: no eDP display with DPCD backlight found\n"); return 1; }

    if (quiet) {
        /* watch mode — minimal output */
        const char *sysfs = "/sys/class/backlight/nvidia_0/brightness";
        /* Default to Samsung ATNA60HU02-0: SDR 5-500 cd/m², register in milli-cd/m² */
        double LUM_MIN = 5000.0, LUM_MAX = 500000.0;

        if (argc >= 4) {
            LUM_MIN = strtod(argv[2], NULL) * 1000.0;
            LUM_MAX = strtod(argv[3], NULL) * 1000.0;
        }

        int sysfs_fd = open(sysfs, O_RDONLY);
        if (sysfs_fd < 0) { perror(sysfs); return 1; }

        fprintf(stderr, "nv-bright: watching %s, display 0x%x, range %.0f-%.0f mcd/m²\n",
                sysfs, edp, LUM_MIN, LUM_MAX);

        int last_val = -1;
        for (;;) {
            char buf[32];
            lseek(sysfs_fd, 0, SEEK_SET);
            int n = read(sysfs_fd, buf, sizeof(buf) - 1);
            if (n <= 0) { usleep(500000); continue; }
            buf[n] = '\0';
            int val = atoi(buf);

            if (val != last_val) {
                uint32_t lum;
                if (val <= 0) lum = (uint32_t)LUM_MIN;
                else if (val >= 100) lum = (uint32_t)LUM_MAX;
                else lum = (uint32_t)(LUM_MIN * pow(LUM_MAX / LUM_MIN, val / 100.0));

                if (set_luminance(edp, lum) != 0) {
                    /* DPCD write failed — driver state is likely bad.
                     * Reinitialize RM client and try again. */
                    fprintf(stderr, "nv-bright: DPCD write failed, reinitializing RM...\n");
                    close(ctl_fd);
                    close(dev_fd);
                    if (init_rm() == 0 && find_edp() == edp) {
                        /* Reinitialization successful, retry the write */
                        set_luminance(edp, lum);
                        last_val = val;
                    } else {
                        /* Reinitialization failed, wait and let loop retry */
                        usleep(500000);
                    }
                } else {
                    last_val = val;
                }
            }

            struct pollfd pfd = { .fd = sysfs_fd, .events = POLLPRI | POLLERR };
            poll(&pfd, 1, 50);
        }
    }

    /* Interactive mode — verbose output */
    uint8_t cap[4], ctl[4];
    dpcd_read(edp, 0x700, cap, 4);
    dpcd_read(edp, 0x720, ctl, 4);

    uint16_t cur_bright = (ctl[2] << 8) | ctl[3];
    int bklght_en = ctl[0] & 1;
    int ctl_mode = ctl[1] & 3;
    int luminance = (ctl[1] >> 7) & 1;

    printf("Display: 0x%x  eDP rev: 1.%d\n", edp, cap[0]);
    printf("GENERAL_CAP1: 0x%02x  BKLGHT_CAP: 0x%02x\n", cap[1], cap[2]);
    printf("DISPLAY_CTL: 0x%02x  BKLGHT_MODE: 0x%02x\n", ctl[0], ctl[1]);
    printf("  BKLGHT_EN=%d  CTL_MODE=%d  LUMINANCE=%d\n", bklght_en, ctl_mode, luminance);
    printf("  Brightness: 0x%04x (%d)\n", cur_bright, cur_bright);

    uint32_t cur_lum = get_luminance(edp);
    printf("  TARGET_LUMINANCE: %d mcd/m² (%d.%03d cd/m²)\n",
           cur_lum, cur_lum / 1000, cur_lum % 1000);

    if (argc < 2) {
        printf("\nUsage: nv-bright <watch [min_nits] [max_nits]|lum N|dump|read ADDR [N]|raw ADDR B1..>\n");
        close(ctl_fd); close(dev_fd);
        return 0;
    }

    if (strcmp(argv[1], "lum") == 0 && argc >= 3) {
        uint32_t val = strtoul(argv[2], NULL, 0);
        printf("Setting TARGET_LUMINANCE to %d mcd/m² (%d.%03d cd/m²)...\n",
               val, val / 1000, val % 1000);
        set_luminance(edp, val);
        uint32_t rb = get_luminance(edp);
        printf("Readback: %d mcd/m²\n", rb);
    } else if (strcmp(argv[1], "read") == 0 && argc >= 3) {
        uint32_t addr = strtoul(argv[2], NULL, 16);
        uint32_t count = argc >= 4 ? (uint32_t)atoi(argv[3]) : 4;
        if (count > 16) count = 16;
        uint8_t tmp[16];
        dpcd_read(edp, addr, tmp, count);
    } else if (strcmp(argv[1], "raw") == 0 && argc >= 4) {
        uint32_t addr = strtoul(argv[2], NULL, 16);
        uint32_t count = argc - 3;
        uint8_t vals[16];
        for (uint32_t i = 0; i < count && i < 16; i++)
            vals[i] = strtoul(argv[3 + i], NULL, 16);
        printf("Writing %d bytes to DPCD 0x%04x:", count, addr);
        for (uint32_t i = 0; i < count; i++) printf(" %02x", vals[i]);
        printf("\n");
        dpcd_write(edp, addr, vals, count);
        dpcd_read(edp, addr, NULL, count);
    } else if (strcmp(argv[1], "dump") == 0) {
        printf("\neDP Caps (0x700-0x71F):\n");
        for (uint32_t a = 0x700; a < 0x720; a += 4)
            dpcd_read(edp, a, NULL, 4);
        printf("\neDP Control (0x720-0x73F):\n");
        for (uint32_t a = 0x720; a < 0x740; a += 4)
            dpcd_read(edp, a, NULL, 4);
    }

    close(ctl_fd); close(dev_fd);
    return 0;
}
