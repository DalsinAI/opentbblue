/*
 * OpenTBBlue: a ZX Spectrum Next (TBBlue) emulator in one window.
 *
 *   opentbblue [options]
 *
 * With no options it runs the Next from its SD card folder in OpenTBBlue's
 * home (see setup.h), with the settings in opentbblue.ini there. On the
 * first run the card is empty, so the latest official System/Next
 * distribution is downloaded (and kept in media/), its files written to
 * the card, and a video mode chosen so the Next boots straight into
 * NextZXOS.
 *
 *   -c PATH      SD card: a folder, or an image (.img), which is kept in
 *                media/ and its files written into drives/<name>/
 *   --sd1 PATH   the second SD card slot, the same way
 *   -p PATH      the Raspberry Pi's folder (default: home/pi)
 *   -z N         window scale (default 3)
 *   -F           full screen
 *   -H N         no window: run N frames, then stop (tests, benchmarks)
 *   -o FILE      with -H, save the last picture as a PPM
 *   -t TEXT      type TEXT (\n for ENTER) after -d frames (default 250)
 *   -b           print the emulation speed when done
 *   --fetch      download the distribution now if the card is empty
 *   --update     refresh the card with the latest distribution
 *   --no-fetch   never download
 *   --home       print OpenTBBlue's home folder and exit
 *
 * Keys: the PC keyboard is the Next's, as on its PS/2 port. Shift is CAPS
 * SHIFT, Ctrl and Alt are SYMBOL SHIFT, and punctuation types what it shows.
 * Backspace is DELETE, the arrows are the cursor keys, Esc is BREAK, Tab is
 * EXTEND MODE. Game controllers are a Kempston joystick.
 * F1 hard reset, F4 soft reset, F9 full speed, F11 full screen, F10 quits;
 * the control bar under the picture (bar.c) does the same and more.
 */
#include <SDL.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "zx/zx.h"
#include "zx/keys.h"
#include "next/next.h"
#include "frontend/net_posix.h"
#include "devices/esp8266.h"
#include "devices/nextpi.h"
#include "setup.h"
#include "video.h"
#include "bar.h"
#include "cards.h"

#define VERSION "0.1.0"

#if defined(__AMIGA__) || defined(__amigaos__)
/* The shell's default 4 KB stack is too small; libnix grows it to this. */
unsigned long __stack = 131072;
static const char amiga_version[] __attribute__((used)) = "$VER: OpenTBBlue " VERSION " (4.11.2026)";
#endif

static zx m;
/* the slots' card folders, for the card panel */
static char slot0[1200], slot1[1200];
static int card_mb, cards_open;
static zx_typer typer;

/* ---- keyboard ----------------------------------------------------------- */

static uint8_t held[SDL_NUM_SCANCODES];   /* down on the PC, or still latched */
static uint8_t pc_down[SDL_NUM_SCANCODES];
static uint64_t pressed_at[SDL_NUM_SCANCODES];
static int latched;

/* NextZXOS reads the keyboard once a frame, so a tap shorter than that
 * (a fast typist, or a key sent by a tool) would be lost: each press is
 * held for at least KEY_LATCH frames. */
#define KEY_LATCH 3

typedef struct { SDL_Scancode sc; int row, bit; } keymap;

/* The keys that are on the Spectrum's own keyboard. */
static const keymap plain[] = {
    { SDL_SCANCODE_Z, 0, 1 }, { SDL_SCANCODE_X, 0, 2 }, { SDL_SCANCODE_C, 0, 3 }, { SDL_SCANCODE_V, 0, 4 },
    { SDL_SCANCODE_A, 1, 0 }, { SDL_SCANCODE_S, 1, 1 }, { SDL_SCANCODE_D, 1, 2 }, { SDL_SCANCODE_F, 1, 3 },
    { SDL_SCANCODE_G, 1, 4 },
    { SDL_SCANCODE_Q, 2, 0 }, { SDL_SCANCODE_W, 2, 1 }, { SDL_SCANCODE_E, 2, 2 }, { SDL_SCANCODE_R, 2, 3 },
    { SDL_SCANCODE_T, 2, 4 },
    { SDL_SCANCODE_1, 3, 0 }, { SDL_SCANCODE_2, 3, 1 }, { SDL_SCANCODE_3, 3, 2 }, { SDL_SCANCODE_4, 3, 3 },
    { SDL_SCANCODE_5, 3, 4 },
    { SDL_SCANCODE_0, 4, 0 }, { SDL_SCANCODE_9, 4, 1 }, { SDL_SCANCODE_8, 4, 2 }, { SDL_SCANCODE_7, 4, 3 },
    { SDL_SCANCODE_6, 4, 4 },
    { SDL_SCANCODE_P, 5, 0 }, { SDL_SCANCODE_O, 5, 1 }, { SDL_SCANCODE_I, 5, 2 }, { SDL_SCANCODE_U, 5, 3 },
    { SDL_SCANCODE_Y, 5, 4 },
    { SDL_SCANCODE_RETURN, 6, 0 }, { SDL_SCANCODE_KP_ENTER, 6, 0 }, { SDL_SCANCODE_L, 6, 1 },
    { SDL_SCANCODE_K, 6, 2 }, { SDL_SCANCODE_J, 6, 3 }, { SDL_SCANCODE_H, 6, 4 },
    { SDL_SCANCODE_SPACE, 7, 0 }, { SDL_SCANCODE_M, 7, 2 }, { SDL_SCANCODE_N, 7, 3 }, { SDL_SCANCODE_B, 7, 4 },
};

/* PC keys that are a CAPS SHIFT combination on the Spectrum. */
static const keymap capsed[] = {
    { SDL_SCANCODE_BACKSPACE, 4, 0 },  /* DELETE   = CAPS + 0 */
    { SDL_SCANCODE_LEFT, 3, 4 },       /* cursor   = CAPS + 5..8 */
    { SDL_SCANCODE_DOWN, 4, 4 },
    { SDL_SCANCODE_UP, 4, 3 },
    { SDL_SCANCODE_RIGHT, 4, 2 },
    { SDL_SCANCODE_ESCAPE, 7, 0 },     /* BREAK    = CAPS + SPACE */
    { SDL_SCANCODE_CAPSLOCK, 3, 1 },   /* CAPS LOCK = CAPS + 2 */
    { SDL_SCANCODE_HOME, 3, 0 },       /* EDIT     = CAPS + 1 */
};

/* PC punctuation: the character it types unshifted and shifted (UK layout
 * where it differs from US is close enough: what matters is the symbol). */
typedef struct { SDL_Scancode sc; char plain, shifted; } punct;

static const punct puncts[] = {
    { SDL_SCANCODE_MINUS, '-', '_' }, { SDL_SCANCODE_EQUALS, '=', '+' },
    { SDL_SCANCODE_LEFTBRACKET, '[', '{' }, { SDL_SCANCODE_RIGHTBRACKET, ']', '}' },
    { SDL_SCANCODE_BACKSLASH, '\\', '|' }, { SDL_SCANCODE_NONUSBACKSLASH, '\\', '|' },
    { SDL_SCANCODE_SEMICOLON, ';', ':' }, { SDL_SCANCODE_APOSTROPHE, '\'', '"' },
    { SDL_SCANCODE_GRAVE, '`', '~' }, { SDL_SCANCODE_COMMA, ',', '<' },
    { SDL_SCANCODE_PERIOD, '.', '>' }, { SDL_SCANCODE_SLASH, '/', '?' },
    { SDL_SCANCODE_NONUSHASH, '#', '~' },
    { SDL_SCANCODE_KP_PLUS, '+', '+' }, { SDL_SCANCODE_KP_MINUS, '-', '-' },
    { SDL_SCANCODE_KP_MULTIPLY, '*', '*' }, { SDL_SCANCODE_KP_DIVIDE, '/', '/' },
    { SDL_SCANCODE_KP_PERIOD, '.', '.' },
};

/* Shifted digits type their symbols, as on a PC. */
static const char shifted_digit[10] = { ')', '!', '@', '#', '$', '%', '^', '&', '*', '(' };

static uint8_t pad_joy, key_joy;   /* Kempston bits from a game controller and from the keyboard */

static void rebuild_keys(void)
{
    uint8_t mat[8][5];
    int shift = held[SDL_SCANCODE_LSHIFT] || held[SDL_SCANCODE_RSHIFT];
    /* Left Ctrl is the joystick's fire button, so SYMBOL SHIFT is the other three */
    int sym = held[SDL_SCANCODE_RCTRL] || held[SDL_SCANCODE_LALT] || held[SDL_SCANCODE_RALT];
    int symbols = 0, caps, i, r, b;
    memset(mat, 0, sizeof mat);

    /* Punctuation (and shifted digits) press their own SYMBOL SHIFT combination,
     * so the host's Shift must not also press CAPS SHIFT for them. */
    for (i = 0; i < (int)(sizeof puncts / sizeof puncts[0]); i++)
        if (held[puncts[i].sc]) {
            int row, bit, c, s;
            if (zx_char_key(shift ? puncts[i].shifted : puncts[i].plain, &row, &bit, &c, &s)) {
                mat[row][bit] = 1;
                if (c) mat[0][0] = 1;
                if (s) mat[7][1] = 1;
            }
            symbols = 1;
        }
    if (shift && !sym)
        for (i = 0; i < 10; i++) {
            SDL_Scancode sc = i == 0 ? SDL_SCANCODE_0 : (SDL_Scancode)(SDL_SCANCODE_1 + i - 1);
            int row, bit, c, s;
            if (held[sc] && zx_char_key(shifted_digit[i], &row, &bit, &c, &s)) {
                mat[row][bit] = 1;
                if (s) mat[7][1] = 1;
                symbols = 1;
            }
        }

    caps = shift && !symbols;
    for (i = 0; i < (int)(sizeof plain / sizeof plain[0]); i++)
        if (held[plain[i].sc]) {
            if (shift && !sym && plain[i].row >= 3 && plain[i].row <= 4)
                continue;   /* a shifted digit: done above */
            mat[plain[i].row][plain[i].bit] = 1;
        }
    for (i = 0; i < (int)(sizeof capsed / sizeof capsed[0]); i++)
        if (held[capsed[i].sc]) {
            mat[capsed[i].row][capsed[i].bit] = 1;
            caps = 1;
        }
    if (held[SDL_SCANCODE_TAB])
        caps = sym = 1;     /* EXTEND MODE */
    if (caps) mat[0][0] = 1;
    if (sym) mat[7][1] = 1;
    for (r = 0; r < 8; r++)
        for (b = 0; b < 5; b++)
            zx_key(&m, r, b, mat[r][b]);

    /* The arrow keys are also a Kempston joystick (they still send the cursor
     * keys, which Kempston games ignore), and Left Ctrl is its fire. */
    key_joy = (uint8_t)((held[SDL_SCANCODE_RIGHT] ? 1 : 0) | (held[SDL_SCANCODE_LEFT] ? 2 : 0) |
                        (held[SDL_SCANCODE_DOWN] ? 4 : 0) | (held[SDL_SCANCODE_UP] ? 8 : 0) |
                        (held[SDL_SCANCODE_LCTRL] ? 16 : 0));
    m.kempston = (uint8_t)(pad_joy | key_joy);
}

/* ---- joystick ----------------------------------------------------------- */

static void pad_button(int button, int down)
{
    uint8_t bit = 0;
    switch (button) {
    case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: bit = 1; break;
    case SDL_CONTROLLER_BUTTON_DPAD_LEFT:  bit = 2; break;
    case SDL_CONTROLLER_BUTTON_DPAD_DOWN:  bit = 4; break;
    case SDL_CONTROLLER_BUTTON_DPAD_UP:    bit = 8; break;
    case SDL_CONTROLLER_BUTTON_A:          bit = 16; break;
    case SDL_CONTROLLER_BUTTON_B:          bit = 32; break;
    case SDL_CONTROLLER_BUTTON_X:          bit = 64; break;
    case SDL_CONTROLLER_BUTTON_START:      bit = 128; break;
    default: return;
    }
    pad_joy = (uint8_t)(down ? pad_joy | bit : pad_joy & ~bit);
    m.kempston = (uint8_t)(pad_joy | key_joy);
}

static void pad_axis(int axis, int value)
{
    const int dead = 12000;
    if (axis == SDL_CONTROLLER_AXIS_LEFTX) {
        pad_joy &= (uint8_t)~3;
        if (value > dead) pad_joy |= 1;
        if (value < -dead) pad_joy |= 2;
    } else if (axis == SDL_CONTROLLER_AXIS_LEFTY) {
        pad_joy &= (uint8_t)~12;
        if (value > dead) pad_joy |= 4;
        if (value < -dead) pad_joy |= 8;
    }
    m.kempston = (uint8_t)(pad_joy | key_joy);
}

/* A control bar button, or the function key that does the same. */
static void bar_action(int act, int *running, int *turbo, int *paused, int *muted)
{
    switch (act) {
    case BAR_QUIT: *running = 0; break;
    case BAR_HARD_RESET: nx_power_on(&m); *paused = 0; break;
    case BAR_SOFT_RESET: zx_reset(&m); *paused = 0; break;
    case BAR_PAUSE: *paused ^= 1; otb_video_turbo(0); break;
    case BAR_TURBO: *turbo ^= 1; otb_video_turbo(*turbo); break;
    case BAR_MUTE: *muted ^= 1; break;
    case BAR_SMALLER: otb_video_scale(-1); break;
    case BAR_BIGGER: otb_video_scale(1); break;
    case BAR_FULLSCREEN: otb_video_fullscreen(); break;
    case BAR_PIN: otb_video_pin(!otb_video_pinned()); break;
    case BAR_CARDS:
        cards_open = !cards_open;
        if (cards_open)
            otb_cards_opened();
        break;
    case BAR_PANEL: break;
    default:
        if (act > BAR_ACT) {
            if (otb_cards_action(act - BAR_ACT))
                cards_open = 0;
        }
        break;
    }
}

/* A card in a slot, card_mb big; if that much does not fit in memory, a
 * card sized to its files instead. */
static int attach_card(int slot, const char *path, int card_mb)
{
    if (card_mb > 0) {
        if (card_mb < 64) card_mb = 64;
        if (card_mb > 32768) card_mb = 32768;
        if (!zx_attach_sd_size(&m, slot, path, (unsigned)card_mb))
            return 0;
        fprintf(stderr, "opentbblue: no memory for a %d MB card in %s, using a smaller one\n", card_mb, path);
    }
    return zx_attach_sd(&m, slot, path);
}

/* ---- the card panel's side (cards.h) ----------------------------------- */

static int host_insert(int slot, const char *path)
{
    int rc;
    otb_video_status(slot ? "PUTTING THE CARD IN SD1..." : "PUTTING THE CARD IN SD0...");
    rc = attach_card(slot, path, card_mb);
    otb_video_status(NULL);
    if (rc)
        return -1;
    snprintf(slot ? slot1 : slot0, sizeof slot0, "%s", path);
    if (slot == 0)
        nx_power_on(&m);    /* a new system card: the Next starts on it */
    return 0;
}

static void host_eject(int slot)
{
    zx_eject_sd(&m, slot);
    (slot ? slot1 : slot0)[0] = 0;
}

/* ---- options ------------------------------------------------------------ */

static void usage(void)
{
    fprintf(stderr,
        "OpenTBBlue " VERSION ": a ZX Spectrum Next (TBBlue) emulator\n"
        "usage: opentbblue [-c sd0] [--sd1 sd1] [-p pi] [-z scale] [-F] [-H frames [-o out.ppm]]\n"
        "                  [-t text [-d frames]] [-b] [--fetch | --update | --no-fetch] [--home]\n"
        "  sd0, sd1: a folder, or a .img (kept in media/, its files written into drives/<name>/)\n"
        "  Defaults are in opentbblue.ini in the --home folder.\n");
}

static void unescape(char *s)
{
    char *d = s;
    for (; *s; s++) {
        if (s[0] == '\\' && s[1] == 'n') { *d++ = '\n'; s++; }
        else *d++ = *s;
    }
    *d = 0;
}

static int save_ppm(const char *path)
{
    FILE *fp = fopen(path, "wb");
    int x, y;
    if (!fp)
        return -1;
    fprintf(fp, "P6\n%d %d\n255\n", m.screen_w, m.screen_h);
    for (y = 0; y < m.screen_h; y++)
        for (x = 0; x < m.screen_w; x++) {
            uint16_t c = m.pixels[y * m.pitch + x];
            fputc(((c >> 6) & 7) * 255 / 7, fp);
            fputc(((c >> 3) & 7) * 255 / 7, fp);
            fputc((c & 7) * 255 / 7, fp);
        }
    return fclose(fp);
}

/* A problem that stops the start: said on stderr, and in a box when there
 * is a window to show (started from an icon there is no terminal). */
static int windowed = 1;
static int fail(const char *fmt, const char *a)
{
    char msg[1600];
    snprintf(msg, sizeof msg, fmt, a);
    fprintf(stderr, "opentbblue: %s\n", msg);
    if (windowed)
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "OpenTBBlue", msg, NULL);
    return 1;
}

static int is_file(const char *p)
{
    struct stat st;
    return stat(p, &st) == 0 && S_ISREG(st.st_mode);
}

/* A slot's setting made into a card folder: an image is kept in media/ and
 * written into drives/<name>/ the first time. */
static int card_folder(const char *setting, char *out, size_t size)
{
    char p[1100];
    if (is_file(setting))
        snprintf(p, sizeof p, "%s", setting);
    else
        otb_resolve(p, sizeof p, setting);
    if (is_file(p))
        return otb_import_image(p, out, size);
    snprintf(out, size, "%s", p);
    return 0;
}

int main(int argc, char **argv)
{
    char sd_buf[1200], sd1_buf[1200], pi_buf[1100], title[96];
    const char *sd = NULL, *sd1 = NULL, *pidir = NULL, *out = NULL;
    char *text = NULL;
    otb_settings set;
    int scale = -1, full = -1, headless = -1, delay = 250, bench = 0, fetch = -1, update = 0;
    int a, running = 1, turbo = 0, paused = 0, muted = 0, bar_press = 0;
    zx_net *net;
    uint32_t t0, t1;
    uint64_t frames0;

    for (a = 1; a < argc; a++) {
        const char *o = argv[a];
        if (!strcmp(o, "-c") && a + 1 < argc) sd = argv[++a];
        else if (!strcmp(o, "--sd1") && a + 1 < argc) sd1 = argv[++a];
        else if (!strcmp(o, "-p") && a + 1 < argc) pidir = argv[++a];
        else if (!strcmp(o, "-z") && a + 1 < argc) scale = atoi(argv[++a]);
        else if (!strcmp(o, "-F")) full = 1;
        else if (!strcmp(o, "-H") && a + 1 < argc) headless = atoi(argv[++a]);
        else if (!strcmp(o, "-o") && a + 1 < argc) out = argv[++a];
        else if (!strcmp(o, "-t") && a + 1 < argc) text = argv[++a];
        else if (!strcmp(o, "-d") && a + 1 < argc) delay = atoi(argv[++a]);
        else if (!strcmp(o, "-b")) bench = 1;
        else if (!strcmp(o, "--fetch")) fetch = 2;
        else if (!strcmp(o, "--update")) update = 1;
        else if (!strcmp(o, "--no-fetch")) fetch = 0;
        else if (!strcmp(o, "--home")) {
            const char *h = otb_home();
            printf("%s\n", h ? h : "(none)");
            return h ? 0 : 1;
        } else if (!strcmp(o, "--version")) {
            printf("OpenTBBlue " VERSION "\n");
            return 0;
        } else {
            usage();
            return 2;
        }
    }

    /* The settings file gives what the command line leaves out. */
    otb_migrate();
    otb_settings_load(&set);
    if (scale < 0) scale = set.scale;
    if (full < 0) full = set.fullscreen;
    if (fetch < 0) fetch = set.fetch;
    if (scale < 1) scale = 1;
    if (scale > 6) scale = 6;
    if (!sd) sd = set.sd0;
    if (!sd1 && set.sd1[0]) sd1 = set.sd1;
    if (!pidir) pidir = set.pi;
    otb_resolve(pi_buf, sizeof pi_buf, pidir);
    pidir = pi_buf;

    windowed = headless < 0;
    if (card_folder(sd, sd_buf, sizeof sd_buf))
        return fail("cannot make a card of %s (see the messages above)", sd);
    sd = sd_buf;
    if (sd1) {
        if (card_folder(sd1, sd1_buf, sizeof sd1_buf))
            return fail("cannot make a card of %s (see the messages above)", sd1);
        sd1 = sd1_buf;
        otb_mkdirs(sd1);
    }

    /* The card: fetched on the first run, or when asked. */
    if (update || (fetch && !otb_card_ready(sd))) {
        if (otb_mkdirs(sd) || ((windowed ? otb_fetch_shown(sd, update) : otb_fetch(sd, update)) &&
                               !otb_card_ready(sd)))
            return fail("System/Next could not be downloaded, so there is no SD card to boot.\n"
                        "Check the network is up and start OpenTBBlue again, or put the\n"
                        "System/Next files in %s yourself.", sd);
        if (fetch == 2 || update)
            if (headless < 0 && !text)
                return 0;
    }
    if (!otb_card_ready(sd))
        return fail("%s is not a Next SD card (it has no TBBLUE.FW)", sd);
    otb_default_config(sd);
    otb_drawer_icons();

    if (zx_create(&m, ZX_NEXT, NULL)) {
        fprintf(stderr, "opentbblue: out of memory\n");
        return 1;
    }
    net = zx_net_posix_new();
    nx_attach_serial(&m, 0, zx_esp8266_new(net));
    otb_mkdirs(pidir);
    nx_attach_serial(&m, 1, zx_nextpi_new(pidir));
    /* The window first, so there is something to see while the cards are
     * read (on a 68040 that takes a while). */
    if (windowed) {
        if (otb_video_open(&m, "OpenTBBlue", scale, full, set.bar_pinned))
            return 1;
        snprintf(title, sizeof title, "OpenTBBlue %s - ZX Spectrum Next", VERSION);
        otb_video_title(title);
        otb_video_status("STARTING: READING THE CARD IN SD0...");
    }
    if (attach_card(0, sd, set.card_mb)) {
        fprintf(stderr, "opentbblue: cannot use %s as the SD card\n", sd);
        return 1;
    }
    if (sd1) {
        if (windowed)
            otb_video_status("STARTING: READING THE CARD IN SD1...");
        if (attach_card(1, sd1, set.card_mb))
            fprintf(stderr, "opentbblue: cannot use %s as the second SD card\n", sd1);
    }
    if (windowed)
        otb_video_status(NULL);
    snprintf(slot0, sizeof slot0, "%s", sd);
    snprintf(slot1, sizeof slot1, "%s", sd1 ? sd1 : "");
    card_mb = set.card_mb;
    if (text) {
        unescape(text);
        zx_type(&typer, text);
        zx_type_delay(&typer, delay);
    }

    if (headless >= 0) {
        int f;
        {
            clock_t c0 = clock();
            for (f = 0; f < headless; f++) {
                zx_type_frame(&typer, &m);
                zx_run_frame(&m);
            }
            if (bench) {
                double s = (double)(clock() - c0) / CLOCKS_PER_SEC;
                printf("OpenTBBlue: %d frames in %.2f s: %.1f frames a second, %.0f%% of a real Next\n",
                       headless, s, s > 0 ? headless / s : 0.0, s > 0 ? headless / s * 2.0 : 0.0);
            }
        }
        if (out && save_ppm(out))
            fprintf(stderr, "opentbblue: cannot write %s\n", out);
        zx_free(&m);
        zx_net_posix_free(net);
        return 0;
    }

    {
        otb_cards_host h;
        h.insert = host_insert;
        h.eject = host_eject;
        otb_cards_init(&h, slot0, slot1, set.check);
    }
    for (a = 0; a < SDL_NumJoysticks(); a++)
        if (SDL_IsGameController(a))
            SDL_GameControllerOpen(a);

    t0 = SDL_GetTicks();
    frames0 = m.frames;
    while (running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            switch (ev.type) {
            case SDL_QUIT: running = 0; break;
            case SDL_KEYDOWN:
            case SDL_KEYUP: {
                SDL_Scancode sc = ev.key.keysym.scancode;
                int down = ev.type == SDL_KEYDOWN;
                if (down && !ev.key.repeat) {
                    int act = sc == SDL_SCANCODE_F10 ? BAR_QUIT : sc == SDL_SCANCODE_F1 ? BAR_HARD_RESET :
                              sc == SDL_SCANCODE_F4 ? BAR_SOFT_RESET : sc == SDL_SCANCODE_F9 ? BAR_TURBO :
                              sc == SDL_SCANCODE_F11 ? BAR_FULLSCREEN : BAR_NONE;
                    if (act != BAR_NONE) {
                        bar_action(act, &running, &turbo, &paused, &muted);
                        break;
                    }
                }
                if (sc < SDL_NUM_SCANCODES && pc_down[sc] != down) {
                    pc_down[sc] = (uint8_t)down;
                    if (down) {
                        int i;
                        /* A new key ends every latched one: the Spectrum's
                         * keyboard scan rejects two keys at once. */
                        for (i = 0; i < SDL_NUM_SCANCODES; i++)
                            if (held[i] && !pc_down[i])
                                held[i] = 0;
                        held[sc] = 1;
                        pressed_at[sc] = m.frames;
                        rebuild_keys();
                    } else if (m.frames - pressed_at[sc] >= KEY_LATCH) {
                        held[sc] = 0;
                        rebuild_keys();
                    } else {
                        latched = 1;
                    }
                }
                break;
            }
            case SDL_WINDOWEVENT:
                if (ev.window.event == SDL_WINDOWEVENT_EXPOSED || ev.window.event == SDL_WINDOWEVENT_SIZE_CHANGED ||
                    ev.window.event == SDL_WINDOWEVENT_RESTORED || ev.window.event == SDL_WINDOWEVENT_SHOWN ||
                    ev.window.event == SDL_WINDOWEVENT_FOCUS_GAINED)
                    otb_video_redraw();
                if (ev.window.event == SDL_WINDOWEVENT_SIZE_CHANGED)
                    otb_video_note_size();
                if (ev.window.event == SDL_WINDOWEVENT_LEAVE)
                    otb_video_pointer(-1, -1, 0);
                if (ev.window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
                    memset(held, 0, sizeof held);
                    memset(pc_down, 0, sizeof pc_down);
                    rebuild_keys();
                }
                break;
            case SDL_MOUSEMOTION: {
                uint32_t b = ev.motion.state;
                otb_video_pointer(ev.motion.x, ev.motion.y, 1);
                zx_mouse(&m, ev.motion.xrel, ev.motion.yrel, 0,
                         (b & SDL_BUTTON_LMASK ? 1 : 0) | (b & SDL_BUTTON_RMASK ? 2 : 0) | (b & SDL_BUTTON_MMASK ? 4 : 0));
                break;
            }
            case SDL_MOUSEBUTTONDOWN:
            case SDL_MOUSEBUTTONUP:
                /* a click on the bar is the bar's, not the Kempston mouse's */
                if (ev.type == SDL_MOUSEBUTTONDOWN && ev.button.button == SDL_BUTTON_LEFT) {
                    int act = otb_video_click(ev.button.x, ev.button.y);
                    if (act != BAR_NONE) {
                        bar_press = 1;
                        bar_action(act, &running, &turbo, &paused, &muted);
                        if (act == BAR_PIN) {
                            set.bar_pinned = otb_video_pinned();
                            otb_settings_put("bar", set.bar_pinned ? "pinned" : "auto");
                        }
                        break;
                    }
                }
                if (ev.type == SDL_MOUSEBUTTONUP && bar_press) {
                    bar_press = 0;
                    break;
                }
                /* fall through */
            case SDL_MOUSEWHEEL: {
                uint32_t b = SDL_GetMouseState(NULL, NULL);
                zx_mouse(&m, 0, 0, ev.type == SDL_MOUSEWHEEL ? -ev.wheel.y : 0,
                         (b & SDL_BUTTON_LMASK ? 1 : 0) | (b & SDL_BUTTON_RMASK ? 2 : 0) | (b & SDL_BUTTON_MMASK ? 4 : 0));
                break;
            }
            case SDL_CONTROLLERDEVICEADDED:
                SDL_GameControllerOpen(ev.cdevice.which);
                break;
            case SDL_CONTROLLERBUTTONDOWN:
            case SDL_CONTROLLERBUTTONUP:
                pad_button(ev.cbutton.button, ev.type == SDL_CONTROLLERBUTTONDOWN);
                break;
            case SDL_CONTROLLERAXISMOTION:
                pad_axis(ev.caxis.axis, ev.caxis.value);
                break;
            default: break;
            }
        }
        if (latched) {
            int i, any = 0, changed = 0;
            for (i = 0; i < SDL_NUM_SCANCODES; i++)
                if (held[i] && !pc_down[i]) {
                    if (m.frames - pressed_at[i] >= KEY_LATCH) { held[i] = 0; changed = 1; }
                    else any = 1;
                }
            latched = any;
            if (changed)
                rebuild_keys();
        }
        otb_video_bar_state(paused, turbo, muted);
        otb_cards_tick();
        otb_video_bar_note(otb_cards_lit(), otb_cards_note());
        if (cards_open) {
            otb_panel p;
            otb_cards_panel(&p);
            otb_video_panel(&p);
        } else {
            otb_video_panel(NULL);
        }
        if (paused || otb_cards_busy()) {
            otb_video_idle();
            continue;
        }
        zx_type_frame(&typer, &m);
        zx_run_frame(&m);
        otb_video_frame(&m, turbo);
    }
    t1 = SDL_GetTicks();
    if (bench && t1 > t0)
        printf("OpenTBBlue: %.1f frames a second; %lu drawn, %lu skipped, %lu lines converted\n",
               (double)(m.frames - frames0) * 1000.0 / (t1 - t0),
               otb_frames_drawn, otb_frames_skipped, otb_lines_converted);
    otb_video_close();
    zx_free(&m);
    zx_net_posix_free(net);
    return 0;
}
