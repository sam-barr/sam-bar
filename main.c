#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>

#include <sys/inotify.h>
#include <sys/timerfd.h>
#include <sys/wait.h>

#include <xcb/xcb.h>
#include <xcb/xcb_aux.h>
#include <xcb/xcb_renderutil.h>

#include <fontconfig/fontconfig.h>
#include <ft2build.h>
#include FT_FREETYPE_H

#include "fonts-for-xcb/utf8_utils/utf8.h"
#include "fonts-for-xcb/xcbft/xcbft.h"

#define SB_NUM_CHARS 3
#define SCREEN_NUMBER 0
#define ERROR NULL
#define DATE_BUF_SIZE sizeof("#1Jun#1 05#1Fri#1 07#1 38")
#define STDIN_LINE_LENGTH 60
#define VOLUME_LENGTH 15
#define BATTERY_LENGTH 20
#define BATTERY_DIRECTORY "/sys/class/power_supply/BAT0"
#define LIGHT_LENGTH 15
#define LIGHT_DIRECTORY "/sys/class/backlight/intel_backlight"
#define FONT_TEMPLATE "Source Code Pro:dpi=%d:size=%d:antialias=true:style=bold"
#define CHARS "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ1234567890[] %●"
#define BACKGROUND_COLOR 0xFF161821
#define STRUTS_NUM_ARGS 12
#define MAC_ADDRESS "00:1B:66:AC:77:78"
/* jsyk: 0F1117B8 is pretty, but doesn't match your theme */

#define true 1
#define false 0

#define DEBUG_BOOL(B) printf("%s\n", (B) ? "true" : "false")
#define sb_pen_to_char(p) ((p) + '0')
#define sb_char_to_pen(c) ((c) - '0')
#define sb_is_numeric(c)  (((c) ^ '0') < 10)

enum {
        SB_POLL_STDIN = 0,
        SB_POLL_TIMER,
        SB_POLL_VOLUME,
        SB_POLL_BATTERY,
        SB_POLL_LIGHT,
        SB_POLL_MAX
};

/* these names correspond to my alacritty config */
enum SB_PEN {
        SB_FG = 0,
        SB_BLACK_B,
        SB_GREEN_N,
        SB_CYAN_B,
        SB_RED_N,
        SB_YELLOW_N,
        SB_PEN_MAX
};

#define SB_MAKE_COLOR(r, g, b) { 0x##r##r, 0x##g##g, 0x##b##b, 0xFFFF }
const xcb_render_color_t SB_PEN_COLOR[SB_PEN_MAX] = {
        SB_MAKE_COLOR(D2, D4, DE),
        SB_MAKE_COLOR(6B, 70, 89),
        SB_MAKE_COLOR(B4, BE, 82),
        SB_MAKE_COLOR(95, C4, CE),
        SB_MAKE_COLOR(E2, 78, 78),
        SB_MAKE_COLOR(E2, A4, 78),
};
#undef SB_MAKE_COLOR

enum {
        NET_WM_WINDOW_TYPE = 0,
        NET_WM_WINDOW_TYPE_DOCK,
        NET_WM_STRUT_PARTIAL,
        SB_ATOM_MAX
};

#define SB_MAKE_ATOM_STRING(str) { str, sizeof(str) - 1 }
const struct { char *name; int len; } SB_ATOM_STRING[SB_ATOM_MAX] = {
        SB_MAKE_ATOM_STRING("_NET_WM_WINDOW_TYPE"),
        SB_MAKE_ATOM_STRING("_NET_WM_WINDOW_TYPE_DOCK"),
        SB_MAKE_ATOM_STRING("_NET_WM_STRUT_PARTIAL"),
};
#undef SB_MAKE_ATOM_STRING

/*
 * Struct which owns all the critical stuff
 * Basically instead of having all of these as globals;
 * I stick them all in a struct and pass that around
 */
struct sam_bar {
        xcb_connection_t *connection;
        xcb_screen_t *screen;
        xcb_window_t window;
        xcb_gcontext_t gc;
        xcb_colormap_t colormap;
        xcb_visualid_t visual_id;
        xcb_atom_t atoms[SB_ATOM_MAX];

        xcb_render_picture_t picture;
        xcb_render_picture_t pens[SB_PEN_MAX];
        xcb_render_glyphset_t glyphset;

        struct xcbft_face_holder face_holder;

        int font_height, line_padding, x_off, width, height;
};

enum {
        LEFT = 0,
        RIGHT,
        TOP,
        BOTTOM,
        LEFT_START_Y,
        LEFT_END_Y,
        RIGHT_START_Y,
        RIGHT_END_Y,
        TOP_START_X,
        TOP_END_X,
        BOTTOM_START_X,
        BOTTOM_END_X
};

enum {
        READ_FD = 0,
        WRITE_FD,
        PIPE_SIZE
};

struct exec_info {
    int pipe[PIPE_SIZE];
    pid_t pid;
};

void sb_test_cookie(const struct sam_bar *sam_bar,
                xcb_void_cookie_t cookie, const char *message) {
        if (xcb_request_check(sam_bar->connection, cookie) != NULL) {
                fprintf(stderr, "%s\n", message);
                exit(EXIT_FAILURE);
        }
}

/*
 * Draw a bit of text on the status bar.
 * Assumptions:
 * - message matches ((#[0-9])?ccc)*, where c is one of the characters in CHARS
 */
void sb_draw_text(const struct sam_bar *sam_bar, int y, const char *message) {
        enum SB_PEN pen;
        FcChar32 text_32[SB_NUM_CHARS];
        int i, message_len, line_height;
        xcb_render_util_composite_text_stream_t *text_stream;

        message_len = strlen(message);
        line_height = sam_bar->font_height + sam_bar->line_padding;

        for (; message[0] != '\0' && message[0] != '\n'; y += line_height) {
                if (message[0] == '#') {
                        pen = sb_char_to_pen(message[1]);
                        message += 2;
                        message_len -= 2;
                } else {
                        pen = SB_FG;
                }

                /* load 3 unicode characters */
                for (i = 0; i < SB_NUM_CHARS; i++) {
                        int shift = FcUtf8ToUcs4(
                                (FcChar8 *)message,
                                text_32 + i,
                                message_len
                        );
                        message_len -= shift;
                        message += shift;
                }

                text_stream = xcb_render_util_composite_text_stream(
                        sam_bar->glyphset,
                        SB_NUM_CHARS,
                        0
                );
                xcb_render_util_glyphs_32(
                        text_stream,
                        sam_bar->x_off, y,
                        SB_NUM_CHARS,
                        text_32
                );
                xcb_render_util_composite_text(
                        sam_bar->connection,
                        XCB_RENDER_PICT_OP_OVER,
                        sam_bar->pens[pen],
                        sam_bar->picture,
                        0,
                        0, 0, /* x, y */
                        text_stream
                );
                xcb_render_util_composite_text_free(text_stream);
        }
}

/*
 * Forks and calls execv on args; setting up the pipe
 * Assumptions:
 * - the last element of args = NULL
 */
void sb_exec(struct exec_info *info, char **args) {
        if (pipe(info->pipe) == -1) {
                printf("pipe failed\n");
                exit(EXIT_FAILURE);
        }

        info->pid = fork();
        if (info->pid == -1) {
                /* fork failed */
                printf("fork failed\n");
                exit(EXIT_FAILURE);
        } else if (info->pid == 0) {
                /* we are child */
                dup2(info->pipe[WRITE_FD], STDOUT_FILENO);
                close(info->pipe[WRITE_FD]);
                close(info->pipe[READ_FD]);
                execv(args[0], args);
        }
}

/*
 * Waits for the associated process of info to terminate;
 * then closes the write end of the pipe
 * Assumptions:
 * - info->pid actually refers to an open process
 */
void sb_wait(struct exec_info *info) {
        waitpid(info->pid, NULL, 0);
        close(info->pipe[WRITE_FD]);
}

/*
 * Kills the associated process of info and closes both ends of the pipe
 * Assumptions:
 * - info->pid actually refers to an open process
 */
void sb_kill(struct exec_info *info) {
        kill(info->pid, SIGTERM);
        close(info->pipe[WRITE_FD]);
        close(info->pipe[READ_FD]);
}

/*
 * Read num_bytes from the pipe into the buffer, appends a null byte,
 * and closes the read end of the pipe
 * Assumptions:
 * - buffer is big enough
 */
void sb_read(struct exec_info *info, char *buffer, size_t num_bytes) {
        buffer[read(info->pipe[READ_FD], buffer, num_bytes)] = '\0';
        close(info->pipe[READ_FD]);
}

void sb_loop_read_recording(char *recording_string) {
        char *pgrep[] = {"/usr/bin/pgrep", "-c", "ffmpeg-dummy", NULL},
             buffer[] = {'0'};
        struct exec_info pgrep_info;

        sb_exec(&pgrep_info, pgrep);
        sb_wait(&pgrep_info);
        sb_read(&pgrep_info, buffer, sizeof buffer);

        if (buffer[0] == '0') {
                recording_string[0] = '\0';
        } else {
                recording_string[0] = '#';
        }
}

void sb_loop_read_volume(char *volume_string) {
        char *pamixer[] = {"/usr/bin/pamixer", "--get-volume-human", NULL},
             *bluetooth[] = {"/usr/bin/bluetoothctl", "info", MAC_ADDRESS, NULL},
             buffer[1024], volume_buffer[10];
        struct exec_info pamixer_info, bluetooth_info;
        int connected = false;

        sb_exec(&pamixer_info, pamixer);
        sb_exec(&bluetooth_info, bluetooth);
        sb_wait(&pamixer_info);
        sb_wait(&bluetooth_info);

        /* assumption: $(bluetoothctl info | wc -c) < 1024 */
        sb_read(&pamixer_info, volume_buffer, sizeof volume_buffer);
        sb_read(&bluetooth_info, buffer, sizeof buffer);
        connected = strstr(buffer, "Connected: yes") != NULL;

        /* decide color */
        volume_string[5] = '#';
        volume_string[6] = sb_pen_to_char(connected ? SB_CYAN_B : SB_BLACK_B);

        if (volume_buffer[0] == 'm') {
                /* muted */
                strcpy(volume_string + 7, "Mut");
        } else if (volume_buffer[3] == '%') {
                /* max volume */
                strcpy(volume_string + 7, "Max");
        } else if (volume_buffer[1] == '%') {
                /* 1 digit volume */
                volume_string[7] = ' ';
                volume_string[8] = volume_buffer[0];
                volume_string[9] = '%';
        } else {
                /* 2 digit volume */
                volume_string[7] = volume_buffer[0];
                volume_string[8] = volume_buffer[1];
                volume_string[9] = '%';
        }
}

void sb_loop_read_battery(char *battery_string) {
        char status, capacity[4];
        FILE *status_file, *capacity_file;

        capacity_file = fopen(BATTERY_DIRECTORY "/capacity", "r");
        status_file = fopen(BATTERY_DIRECTORY "/status", "r");

        status = fgetc(status_file);
        fgets(capacity, 4, capacity_file);

        if (capacity[2] == '0') {
                /* battery full */
                strcpy(battery_string + 5, "#2Ful");
        } else {
                battery_string[5] = '#';
                /* decide color for percentage */
                /* I think this is the first time I've used fall-through on purpose */
                switch (capacity[0]) {
                case  '9':
                case  '8': 
                        battery_string[6] = sb_pen_to_char(SB_GREEN_N);
                        break;
                case  '7':
                case  '6':
                case  '5':
                case  '4':
                case  '3':
                        battery_string[6] = sb_pen_to_char(SB_YELLOW_N);
                        break;
                case  '2':
                case  '1':
                        battery_string[6] = sb_pen_to_char(SB_RED_N);
                        break;
                }

                /* append the capacity */
                if (capacity[1] == '\n') {
                        /* single digit, add a space */
                        battery_string[6] = sb_pen_to_char(SB_RED_N);
                        battery_string[7] = ' ';
                        battery_string[8] = capacity[0];
                } else {
                        battery_string[7] = capacity[0];
                        battery_string[8] = capacity[1];
                }
                battery_string[9] = '%';
                /* Display if the battery is charging */
                if (status == 'C') {
                        strcpy(battery_string + 10, "#5Chg");
                } else {
                        battery_string[10] = '\0';
                }
        }

        fclose(status_file);
        fclose(capacity_file);
}

int sb_str_to_int(char *str) {
        int c, n = 0;
        while (sb_is_numeric(c = *(str++))) {
                n = n * 10 + c - '0';
        }
        return n;
}

void sb_loop_read_light(char *light_string) {
        FILE *brightness_file, *max_file;
        int brightness, max, light;
        char buffer[100];

        brightness_file = fopen(LIGHT_DIRECTORY "/brightness", "r");
        max_file = fopen(LIGHT_DIRECTORY "/max_brightness", "r");

        fgets(buffer, sizeof buffer, brightness_file);
        brightness = sb_str_to_int(buffer);
        fgets(buffer, sizeof buffer, max_file);
        max = sb_str_to_int(buffer);
        light = 100 * brightness / max;

        strcpy(light_string + 5, "#1");
        if (light == 100) {
                strcpy(light_string + 7, "Max");
        } else if (light < 10) {
                light_string[7] = ' ';
                light_string[8] = light + '0';
                light_string[9] = '%';
        } else {
                light_string[7] = light / 10 + '0';
                light_string[8] = light % 10 + '0';
                light_string[9] = '%';
        }

        fclose(brightness_file);
        fclose(max_file);
}

void sb_loop_main(struct sam_bar *sam_bar) {
        struct pollfd pollfds[SB_POLL_MAX];
        struct itimerspec ts;
        int redraw = false, just_pamixer = false, i, hide = true;
        unsigned long int elapsed = 0; 
        xcb_rectangle_t rectangle;
        char time_string[DATE_BUF_SIZE] = {0},
             stdin_string[STDIN_LINE_LENGTH] = {0},
             volume_string[VOLUME_LENGTH] = {0},
             battery_string[BATTERY_LENGTH] = {0},
             light_string[LIGHT_LENGTH] = {0},
             recording_string[] = "#4 ● ";
        struct exec_info pactl_info;

        {
                char *pactl[] = {"/usr/bin/pactl", "subscribe", NULL};
                int flags;
                sb_exec(&pactl_info, pactl);
                flags = fcntl(pactl_info.pipe[READ_FD], F_GETFL, 0);
                fcntl(pactl_info.pipe[READ_FD], F_SETFL, flags | O_NONBLOCK);
                strcpy(volume_string, "#1Vol");
        }

        pollfds[SB_POLL_STDIN].fd = STDIN_FILENO;
        pollfds[SB_POLL_TIMER].fd = timerfd_create(CLOCK_MONOTONIC, 0);
        pollfds[SB_POLL_VOLUME].fd = pactl_info.pipe[READ_FD];
        pollfds[SB_POLL_BATTERY].fd = inotify_init1(IN_NONBLOCK);
        pollfds[SB_POLL_LIGHT].fd = inotify_init1(IN_NONBLOCK);
        for(i = 0; i < SB_POLL_MAX; i++)
                pollfds[i].events = POLLIN;

        inotify_add_watch(
                pollfds[SB_POLL_BATTERY].fd,
                BATTERY_DIRECTORY "/uevent",
                IN_ACCESS
        );
        strcpy(battery_string, "#1Bat");

        inotify_add_watch(
                pollfds[SB_POLL_LIGHT].fd,
                LIGHT_DIRECTORY "/brightness",
                IN_MODIFY
        );
        strcpy(light_string, "#1Lit");

        ts.it_interval.tv_sec = 1; /* fire every second */
        ts.it_interval.tv_nsec = 0;
        ts.it_value.tv_sec = 0;
        ts.it_value.tv_nsec = 1; /* initial fire happens *basically* instantly */
        timerfd_settime(pollfds[1].fd, 0, &ts, NULL);

        rectangle.x = rectangle.y = 0;
        rectangle.width = sam_bar->width;
        rectangle.height = sam_bar->height;

        /* main loop */
        sb_loop_read_volume(volume_string);
        sb_loop_read_battery(battery_string);
        sb_loop_read_light(light_string);
        for (;;) {
                /* blocks until one of the fds becomes open */
                poll(pollfds, SB_POLL_MAX, -1);
                if (pollfds[SB_POLL_STDIN].revents & POLLHUP) {
                        /* stdin died, and so do we */
                        break;
                } else if (pollfds[SB_POLL_STDIN].revents & POLLIN) {
                        int do_hide;
                        if (fgets(stdin_string, STDIN_LINE_LENGTH, stdin) == NULL)
                                break;
                        do_hide = strstr(stdin_string, "XXX") != NULL;
                        /* state changed */
                        if (do_hide != hide) {
                                hide = do_hide;
                                if (do_hide) {
                                        xcb_unmap_window(
                                                sam_bar->connection,
                                                sam_bar->window
                                        );
                                } else {
                                        xcb_map_window(
                                                sam_bar->connection,
                                                sam_bar->window
                                        );
                                }
                        }
                        redraw = true;
                } else if (pollfds[SB_POLL_TIMER].revents & POLLIN) {
                        long num;
                        time_t rawtime;
                        struct tm *info;
                        char prev_minute;

                        read(pollfds[SB_POLL_TIMER].fd, &num, sizeof(long));
                        elapsed += num;

                        prev_minute = time_string[DATE_BUF_SIZE - 2];
                        time(&rawtime);
                        info = localtime(&rawtime);
                        strftime(
                                time_string,
                                DATE_BUF_SIZE,
                                "#1%b#1 %d#1%a#1 %I#1 %M",
                                info
                        );
                        redraw = prev_minute != time_string[DATE_BUF_SIZE - 2];
                } else if (pollfds[SB_POLL_VOLUME].revents & POLLIN) {
                        char buffer[1024];

                        while (read(pactl_info.pipe[READ_FD], buffer, sizeof buffer) > 0);
                        if (just_pamixer) {
                                just_pamixer = false;
                        } else {
                                sb_loop_read_volume(volume_string);
                                just_pamixer = true;
                                redraw = true;
                        }
                } else if (pollfds[SB_POLL_BATTERY].revents & POLLIN) {
                        /* read the battery when the status changes */
                        struct inotify_event event;
                        read(pollfds[SB_POLL_BATTERY].fd, &event, sizeof event);
                        sb_loop_read_battery(battery_string);
                        redraw = true;
                } else if (pollfds[SB_POLL_LIGHT].revents & POLLIN) {
                        struct inotify_event event;
                        read(pollfds[SB_POLL_LIGHT].fd, &event, sizeof event);
                        sb_loop_read_light(light_string);
                        redraw = true;
                }

                /* read battery every 30 seconds */
                if (elapsed % 30 == 0) {
                        sb_loop_read_battery(battery_string);
                        redraw = true;
                }

                if (elapsed % 5 == 0) {
                        sb_loop_read_recording(recording_string);
                        redraw = true;
                }

                if (redraw && hide) {
                        xcb_poly_fill_rectangle(
                                sam_bar->connection,
                                sam_bar->window,
                                sam_bar->gc,
                                1, /* 1 rectangle */
                                &rectangle
                        );
                        xcb_flush(sam_bar->connection);
                } else if (redraw && !hide) {
                        int y = sam_bar->height;
                        /* clear the screen */
                        xcb_poly_fill_rectangle(
                                sam_bar->connection,
                                sam_bar->window,
                                sam_bar->gc,
                                1, /* 1 rectangle */
                                &rectangle
                        );
                        /* write the text */
                        sb_draw_text(sam_bar, sam_bar->font_height, stdin_string);
                        y -= 4 * sam_bar->font_height + 5 * sam_bar->line_padding;
                        sb_draw_text(sam_bar, y, time_string);
                        y -= 3 * sam_bar->font_height + 2 * sam_bar->line_padding;
                        sb_draw_text(sam_bar, y, volume_string);
                        y -= 3 * sam_bar->font_height + 2 * sam_bar->line_padding;
                        if (battery_string[10] != '\0') {
                                y -= sam_bar->font_height + sam_bar->line_padding;
                        }
                        sb_draw_text(sam_bar, y, battery_string);
                        y -= 3 * sam_bar->font_height + 2 * sam_bar->line_padding;
                        sb_draw_text(sam_bar, y, light_string);
                        y -= 1 * sam_bar->font_height + 1 * sam_bar->line_padding;
                        sb_draw_text(sam_bar, y, recording_string);
                        xcb_flush(sam_bar->connection);
                }
                redraw = false;
        }

        /* relinquish loop resources */
        sb_kill(&pactl_info);
}

int main(void) {
        struct sam_bar sam_bar;
        int i;

        { /* initialize most of the xcb stuff sam_bar */
                int ptr[] = { SCREEN_NUMBER };
                sam_bar.connection = xcb_connect(NULL, ptr);
                if (xcb_connection_has_error(sam_bar.connection)) {
                        xcb_disconnect(sam_bar.connection);
                        fprintf(stderr, "Unable to connect to X\n");
                        return EXIT_FAILURE;
                }
                sam_bar.screen = xcb_setup_roots_iterator(
                        xcb_get_setup(sam_bar.connection)
                ).data;
                sam_bar.height = sam_bar.screen->height_in_pixels;
                sam_bar.window = xcb_generate_id(sam_bar.connection);
                sam_bar.gc = xcb_generate_id(sam_bar.connection);
                sam_bar.picture = xcb_generate_id(sam_bar.connection);
                sam_bar.colormap = xcb_generate_id(sam_bar.connection);
                sam_bar.visual_id = xcb_aux_find_visual_by_attrs(
                        sam_bar.screen, 
                        -1, 
                        32
                )->visual_id;
                for (i = 0; i < SB_PEN_MAX; i++) {
                        sam_bar.pens[i] = xcbft_create_pen(
                                sam_bar.connection,
                                SB_PEN_COLOR[i]
                        );
                }
        }

        /* initialize a 32 bit colormap */
        xcb_create_colormap(
                sam_bar.connection,
                XCB_COLORMAP_ALLOC_NONE,
                sam_bar.colormap, sam_bar.screen->root,
                sam_bar.visual_id
        );

        { /* load up fonts and glyphs */
                char searchlist[100] = {0}, *current_display;
                FcStrSet *fontsearch;
                struct xcbft_patterns_holder font_patterns;
                struct utf_holder chars;
                int dpi, size;

                current_display = getenv("CURRENT_DISPLAY");
                if (strcmp("low", current_display) == 0) {
                        sam_bar.font_height = 14;
                        sam_bar.line_padding = 10;
                        sam_bar.x_off = 2;
                        sam_bar.width = 32;
                        size = 11;
                        dpi = 96;
                } else if (strcmp("high", current_display) == 0) {
                        sam_bar.font_height = 32;
                        sam_bar.line_padding = 24;
                        sam_bar.x_off = 5;
                        sam_bar.width = 75;
                        size = 7;
                        dpi = 336;
                } else {
                        fprintf(
                                stderr,
                                "Unknown CURRENT_DISPLAY: %s\n",
                                current_display
                        );
                        return EXIT_FAILURE;
                }

                xcbft_init();
                sprintf(searchlist, FONT_TEMPLATE, dpi, size);
                fontsearch = xcbft_extract_fontsearch_list(searchlist);
                font_patterns = xcbft_query_fontsearch_all(fontsearch);
                FcStrSetDestroy(fontsearch);
                sam_bar.face_holder = xcbft_load_faces(font_patterns, dpi);
                xcbft_patterns_holder_destroy(font_patterns);
                chars = char_to_uint32(CHARS);
                sam_bar.glyphset = xcbft_load_glyphset(
                                sam_bar.connection,
                                sam_bar.face_holder,
                                chars,
                                dpi
                ).glyphset;
                utf_holder_destroy(chars);
        }

        { /* initialize window */
                xcb_void_cookie_t cookie;
                int mask = XCB_CW_BACK_PIXEL
                        | XCB_CW_BORDER_PIXEL
                        | XCB_CW_OVERRIDE_REDIRECT
                        | XCB_CW_COLORMAP;
                /* we have a 32 bit visual/colormap, su just use ARGB colors */
                int values[4];
                values[0] = BACKGROUND_COLOR;
                values[1] = 0xFFFFFFFF;
                values[2] = true;
                values[3] = sam_bar.colormap;

                cookie = xcb_create_window_checked(
                        sam_bar.connection,
                        32, /* 32 bits of depth */
                        sam_bar.window, sam_bar.screen->root,
                        0, 0, /* top corner of screen */
                        sam_bar.width, sam_bar.height,
                        0, /* border width */
                        XCB_WINDOW_CLASS_INPUT_OUTPUT,
                        sam_bar.visual_id,
                        mask,
                        values
                );
                sb_test_cookie(&sam_bar, cookie, "xcb_create_window_checked failed");
        }

        { /* initialize graphics context (used for clearing the screen) */
                xcb_void_cookie_t cookie;
                int mask = XCB_GC_FOREGROUND | XCB_GC_BACKGROUND;
                int values[2] = { BACKGROUND_COLOR, 0xFFFFFFFF };
                cookie = xcb_create_gc_checked(
                        sam_bar.connection,
                        sam_bar.gc,
                        sam_bar.window,
                        mask,
                        values
                );
                sb_test_cookie(&sam_bar, cookie, "xcb_create_gc_checked failed");
        }

        { /* initialize picture (used for drawing text) */
                const xcb_render_query_pict_formats_reply_t *fmt_rep =
                        xcb_render_util_query_formats(sam_bar.connection);
                xcb_render_pictforminfo_t *fmt = xcb_render_util_find_standard_format(
                        fmt_rep, 
                        XCB_PICT_STANDARD_ARGB_32
                );
                int mask = XCB_RENDER_CP_POLY_MODE | XCB_RENDER_CP_POLY_EDGE;
                int values[2] = { 
                        XCB_RENDER_POLY_MODE_IMPRECISE,
                        XCB_RENDER_POLY_EDGE_SMOOTH
                };
                xcb_void_cookie_t cookie = xcb_render_create_picture_checked(
                        sam_bar.connection,
                        sam_bar.picture,
                        sam_bar.window,
                        fmt->id,
                        mask,
                        values
                );
                sb_test_cookie(&sam_bar, cookie, "xcb_create_picture_checked failed");
        }

        { /* load atoms */
                xcb_intern_atom_cookie_t atom_cookies[SB_ATOM_MAX];
                for (i = 0; i < SB_ATOM_MAX; i++) {
                        atom_cookies[i] = xcb_intern_atom(
                                sam_bar.connection,
                                0, /* "atom created if it doesn't already exist" */
                                SB_ATOM_STRING[i].len,
                                SB_ATOM_STRING[i].name
                        );
                }
                for (i = 0; i < SB_ATOM_MAX; i++) {
                        xcb_intern_atom_reply_t *reply = xcb_intern_atom_reply(
                                sam_bar.connection,
                                atom_cookies[i],
                                ERROR
                        );
                        sam_bar.atoms[i] = reply->atom;
                        free(reply);
                }
        }

        /* change window properties to be a dock */
        xcb_change_property(
                sam_bar.connection,
                XCB_PROP_MODE_REPLACE,
                sam_bar.window,
                sam_bar.atoms[NET_WM_WINDOW_TYPE],
                XCB_ATOM_ATOM,
                32, /* MAGIC NUMBER?? */
                1, /* sending 1 argument */
                &sam_bar.atoms[NET_WM_WINDOW_TYPE_DOCK]
        );

        {
                /* setup struts so windows don't overlap the bar */
                int struts[STRUTS_NUM_ARGS] = {0};
                struts[LEFT] = sam_bar.width;
                struts[LEFT_START_Y] = struts[RIGHT_START_Y] = 0;
                struts[LEFT_END_Y] = struts[RIGHT_END_Y] = sam_bar.height;
                struts[TOP_START_X] = struts[BOTTOM_START_X] = 0;
                struts[TOP_END_X] = struts[BOTTOM_END_X] = sam_bar.width;
                xcb_change_property(
                        sam_bar.connection,
                        XCB_PROP_MODE_REPLACE,
                        sam_bar.window,
                        sam_bar.atoms[NET_WM_STRUT_PARTIAL],
                        XCB_ATOM_CARDINAL,
                        32, /* MAGIC NUMBER ? */
                        STRUTS_NUM_ARGS, struts
                );
        }

        xcb_flush(sam_bar.connection);
        sb_loop_main(&sam_bar);

        /* relinquish resources */
        for (i = 0; i < SB_PEN_MAX; i++) {
                xcb_render_free_picture(sam_bar.connection, sam_bar.pens[i]);
        }
        xcb_render_free_picture(sam_bar.connection, sam_bar.picture);
        xcb_free_colormap(sam_bar.connection, sam_bar.colormap);
        xcb_free_gc(sam_bar.connection, sam_bar.gc);
        xcbft_face_holder_destroy(sam_bar.face_holder);
        xcb_render_util_disconnect(sam_bar.connection);
        xcb_disconnect(sam_bar.connection);
        xcbft_done();
        /* if valgrind reports more than 18,612 reachable that might be a leak */

        return EXIT_SUCCESS;
}
