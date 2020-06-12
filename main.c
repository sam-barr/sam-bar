#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <poll.h>
#include <signal.h>
#include <time.h>

#include <sys/timerfd.h>
#include <sys/inotify.h>

#include <xcb/xcb.h>
#include <xcb/xcb_aux.h>
#include <xcb/xcb_renderutil.h>

#include <fontconfig/fontconfig.h>
#include <ft2build.h>
#include <freetype/freetype.h>

#include "fonts-for-xcb/utf8_utils/utf8.h"
#include "fonts-for-xcb/xcbft/xcbft.h"

#define SB_NUM_CHARS 3
#define SCREEN_NUMBER 0
#define ERROR NULL
#define DATE_BUF_SIZE sizeof("#1Jun#1 05#1Fri#1 07#1 38")
#define STDIN_LINE_LENGTH 55
#define VOLUME_LENGTH 15
#define BATTERY_LENGTH 20
#define BATTERY_DIRECTORY "/sys/class/power_supply/BAT0"
#define FONT_TEMPLATE "Hasklug Nerd Font:dpi=%d:size=%d:antialias=true:style=bold"
#define BACKGROUND_COLOR 0xB80F1117
/* jsyk: 0F1117B8 is pretty, but doesn't match your theme */

#define STRUTS_NUM_ARGS 12

#define DEBUG_BOOL(B) printf("%s\n", (B) ? "true" : "false")

#define CHARS "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ1234567890[] %"

enum {
    SB_POLL_STDIN = 0,
    SB_POLL_TIMER,
    SB_POLL_VOLUME,
    SB_POLL_BATTERY,
    SB_POLL_MAX
};

/* these names correspond to my alacritty config */
typedef enum {
    SB_FG = 0,
    SB_BLACK_B,
    SB_GREEN_N,
    SB_CYAN_B,
    SB_RED_N,
    SB_YELLOW_N,
    SB_PEN_MAX
} SB_PEN;

#define SB_MAKE_COLOR(r,g,b) { 0x##r##r, 0x##g##g, 0x##b##b, 0xFFFF }
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
typedef struct {
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

    int font_height, line_padding, x_off;
} SamBar;

enum StrutPartial {
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

void sb_test_cookie(const SamBar *sam_bar, xcb_void_cookie_t cookie, const char *message) {
    xcb_generic_error_t *error = xcb_request_check(sam_bar->connection, cookie);
    if (error != NULL) {
        fprintf(stderr, "%s\n", message);
        exit(EXIT_FAILURE);
    }
}

/*
 * Assumptions: strlen(message) % SB_NUM_CHARS == 0
 * This doesn't handle unicode in the slightest
 */
void sb_draw_text(const SamBar *sam_bar, int y, char *message) {
    char buffer[SB_NUM_CHARS + 1] = {0};
    SB_PEN pen;
    int i;
    struct utf_holder text;
    xcb_render_util_composite_text_stream_t *text_stream;

    for (; *message != '\0' && *message != '\n';
            message += SB_NUM_CHARS, y += sam_bar->font_height + sam_bar->line_padding) {
        if (*message == '#') {
            pen = message[1] - '0';
            message += 2;
        } else {
            pen = SB_FG;
        }
        for (i = 0; i < SB_NUM_CHARS; i++)
            buffer[i] = message[i];

        text = char_to_uint32(buffer);
        text_stream = xcb_render_util_composite_text_stream(
                sam_bar->glyphset,
                text.length,
                0);
        xcb_render_util_glyphs_32(text_stream, sam_bar->x_off, y, text.length, text.str);
        xcb_render_util_composite_text(
                sam_bar->connection,
                XCB_RENDER_PICT_OP_OVER,
                sam_bar->pens[pen],
                sam_bar->picture,
                0,
                0, 0, /* x, y */
                text_stream);
        xcb_render_util_composite_text_free(text_stream);
        utf_holder_destroy(text);
    }
}

int main(void) {
    SamBar sam_bar;
    int width, height, i;

    { /* initialize most of the xcb stuff sam_bar */
        int ptr = SCREEN_NUMBER;
        sam_bar.connection = xcb_connect(NULL, &ptr);
        if (xcb_connection_has_error(sam_bar.connection)) {
            xcb_disconnect(sam_bar.connection);
            return EXIT_FAILURE;
        }
        sam_bar.screen = xcb_setup_roots_iterator(xcb_get_setup(sam_bar.connection)).data;
        sam_bar.window = xcb_generate_id(sam_bar.connection);
        sam_bar.gc = xcb_generate_id(sam_bar.connection);
        sam_bar.picture = xcb_generate_id(sam_bar.connection);
        sam_bar.colormap = xcb_generate_id(sam_bar.connection);
        sam_bar.visual_id = xcb_aux_find_visual_by_attrs(sam_bar.screen, -1, 32)->visual_id;
        for (i = 0; i < SB_PEN_MAX; i++)
            sam_bar.pens[i] = xcbft_create_pen(sam_bar.connection, SB_PEN_COLOR[i]);
    }

    height = sam_bar.screen->height_in_pixels;

    /* initialize a 32 bit colormap */
    xcb_create_colormap(
            sam_bar.connection,
            XCB_COLORMAP_ALLOC_NONE,
            sam_bar.colormap,
            sam_bar.screen->root,
            sam_bar.visual_id);

    { /* load up fonts and glyphs */
        char searchlist[100] = {0},
             *current_display;
        FcStrSet *fontsearch;
        struct xcbft_patterns_holder font_patterns;
        struct utf_holder chars;
        int dpi, size;

        current_display = getenv("CURRENT_DISPLAY");
        if (strcmp("low", current_display) == 0) {
            sam_bar.font_height = 14;
            sam_bar.line_padding = 10;
            sam_bar.x_off = 2;
            size = 11;
            dpi = 96;
            width = 32;
        } else if (strcmp("high", current_display) == 0) {
            sam_bar.font_height = 32;
            sam_bar.line_padding = 24;
            sam_bar.x_off = 5;
            size = 7;
            dpi = 336;
            width = 75;
        } else {
            printf("Unknown CURRENT_DISPLAY: %s\n", current_display);
            return EXIT_FAILURE;
        }

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
                dpi).glyphset;
        utf_holder_destroy(chars);
    }

    { /* initialize window */
        xcb_void_cookie_t cookie;
        int mask = XCB_CW_BACK_PIXEL
            | XCB_CW_BORDER_PIXEL
            | XCB_CW_OVERRIDE_REDIRECT
            | XCB_CW_COLORMAP;
        /* because we have a 32 bit visual/colormap, just directly use ARGB colors */
        int values[4];
        values[0] = BACKGROUND_COLOR;
        values[1] = 0xFFFFFFFF;
        values[2] = true;
        values[3] = sam_bar.colormap;

        cookie = xcb_create_window_checked(
                sam_bar.connection,
                32, /* 32 bits of depth */
                sam_bar.window,
                sam_bar.screen->root,
                0, 0, /* top corner of screen */
                width, height,
                0, /* border width */
                XCB_WINDOW_CLASS_INPUT_OUTPUT,
                sam_bar.visual_id,
                mask,
                values);
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
                values);
        sb_test_cookie(&sam_bar, cookie, "xcb_create_gc_checked failed");
    }

    { /* initialize picture (used for drawing text) */
        const xcb_render_query_pict_formats_reply_t *fmt_rep =
            xcb_render_util_query_formats(sam_bar.connection);
        xcb_render_pictforminfo_t *fmt = xcb_render_util_find_standard_format(
                fmt_rep,
                XCB_PICT_STANDARD_ARGB_32);
        int mask = XCB_RENDER_CP_POLY_MODE | XCB_RENDER_CP_POLY_EDGE;
        int values[2] = { XCB_RENDER_POLY_MODE_IMPRECISE, XCB_RENDER_POLY_EDGE_SMOOTH };
        xcb_void_cookie_t cookie = xcb_render_create_picture_checked(
                sam_bar.connection,
                sam_bar.picture,
                sam_bar.window,
                fmt->id,
                mask,
                values);
        sb_test_cookie(&sam_bar, cookie, "xcb_create_picture_checked failed");
    }

    { /* load atoms */
        xcb_intern_atom_cookie_t atom_cookies[SB_ATOM_MAX];
        for (i = 0; i < SB_ATOM_MAX; i++) {
            atom_cookies[i] = xcb_intern_atom(
                    sam_bar.connection,
                    0, /* "atom will be created if it does not already exist" */
                    SB_ATOM_STRING[i].len,
                    SB_ATOM_STRING[i].name);
        }
        for (i = 0; i < SB_ATOM_MAX; i++) {
            xcb_intern_atom_reply_t *reply = xcb_intern_atom_reply(
                    sam_bar.connection,
                    atom_cookies[i],
                    ERROR);
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
            &sam_bar.atoms[NET_WM_WINDOW_TYPE_DOCK]);

    {
        /* setup struts so windows don't overlap the bar */
        int struts[STRUTS_NUM_ARGS] = {0};
        struts[LEFT] = width;
        struts[LEFT_START_Y] = struts[RIGHT_START_Y] = 0;
        struts[LEFT_END_Y] = struts[RIGHT_END_Y] = height;
        struts[TOP_START_X] = struts[BOTTOM_START_X] = 0;
        struts[TOP_END_X] = struts[BOTTOM_END_X] = width;
        xcb_change_property(
                sam_bar.connection,
                XCB_PROP_MODE_REPLACE,
                sam_bar.window,
                sam_bar.atoms[NET_WM_STRUT_PARTIAL],
                XCB_ATOM_CARDINAL,
                32, /* MAGIC NUMBER ? */
                STRUTS_NUM_ARGS,
                struts);
    }

    xcb_map_window(sam_bar.connection, sam_bar.window);
    xcb_flush(sam_bar.connection);

    { /* main loop setup */
        struct pollfd pollfds[SB_POLL_MAX];
        struct itimerspec ts;
        bool redraw = false;
        unsigned long int elapsed = 0; /* hopefully I don't leave this running for > 100 years */
        xcb_rectangle_t rectangle;
        char time_string[DATE_BUF_SIZE] = {0},
             stdin_string[STDIN_LINE_LENGTH] = {0},
             volume_string[VOLUME_LENGTH] = {0},
             battery_string[BATTERY_LENGTH] = {0};
        int volume_pipe[2];
        FILE *volume_file, *capacity_file, *status_file;
        pid_t volume_pid;

        pipe(volume_pipe);
        volume_pid = fork();
        if (volume_pid == 0) {
            dup2(volume_pipe[1], STDOUT_FILENO);
            close(volume_pipe[0]);
            close(volume_pipe[1]);
            execl(INSTALL_DIR "/listen-volume.sh", "", (char *)NULL);
        }
        volume_file = fdopen(volume_pipe[0], "r");

        pollfds[SB_POLL_STDIN].fd = STDIN_FILENO;
        pollfds[SB_POLL_STDIN].events = POLLIN;
        pollfds[SB_POLL_TIMER].fd = timerfd_create(CLOCK_MONOTONIC, 0);
        pollfds[SB_POLL_TIMER].events = POLLIN;
        pollfds[SB_POLL_VOLUME].fd = volume_pipe[0];
        pollfds[SB_POLL_VOLUME].events = POLLIN;
        pollfds[SB_POLL_BATTERY].fd = inotify_init1(IN_NONBLOCK);
        pollfds[SB_POLL_BATTERY].events = POLLIN;

        capacity_file = fopen(BATTERY_DIRECTORY "/capacity", "r");
        status_file = fopen(BATTERY_DIRECTORY "/status", "r");
        inotify_add_watch(pollfds[SB_POLL_BATTERY].fd, BATTERY_DIRECTORY "/uevent", IN_ACCESS);
        strcpy(battery_string, "#1Bat");

        ts.it_interval.tv_sec = 1; /* fire every second */
        ts.it_interval.tv_nsec = 0;
        ts.it_value.tv_sec = 0;
        ts.it_value.tv_nsec = 1; /* initial fire happens *basically* instantly */
        timerfd_settime(pollfds[1].fd, 0, &ts, NULL);

        rectangle.x = rectangle.y = 0;
        rectangle.width = width;
        rectangle.height = height;

        /* main loop */
        for (;;) {
            /* blocks until one of the fds becomes open */
            poll(pollfds, SB_POLL_MAX, -1);
            if (pollfds[SB_POLL_STDIN].revents & POLLHUP) {
                /* stdin died, and so do we */
                break;
            } else if (pollfds[SB_POLL_STDIN].revents & POLLIN) {
                fgets(stdin_string, STDIN_LINE_LENGTH, stdin);
                redraw = true;
                if (*stdin_string == 'X' || *stdin_string == EOF)
                    break;
            } else if (pollfds[SB_POLL_TIMER].revents & POLLIN) {
                uint64_t num;
                time_t rawtime;
                struct tm *info;
                char prev_minute;

                read(pollfds[SB_POLL_TIMER].fd, &num, sizeof(uint64_t));
                elapsed += num;

                prev_minute = time_string[DATE_BUF_SIZE-2];
                time(&rawtime);
                info = localtime(&rawtime);
                strftime(time_string, DATE_BUF_SIZE, "#1%b#1 %d#1%a#1 %I#1 %M", info);
                redraw = prev_minute != time_string[DATE_BUF_SIZE-2];
            } else if (pollfds[SB_POLL_VOLUME].revents & POLLIN) {
                fgets(volume_string, VOLUME_LENGTH, volume_file);
                redraw = true;
            } else if (pollfds[SB_POLL_BATTERY].revents & POLLIN) {
                /* read the battery when the status changes */
                struct inotify_event event;
                read(pollfds[SB_POLL_BATTERY].fd, &event, sizeof(struct inotify_event));
                goto SB_READ_BATTERY;
            }


            /* read the battery every 30 seconds, starting the first second */
            if (elapsed % 30 == 1) {
                char status, capacity[4];
SB_READ_BATTERY:

                status = fgetc(status_file);
                fgets(capacity, 4, capacity_file);
                if (capacity[2] == '0') {
                    /* battery full */
                    strcpy(battery_string + 5, "#2Ful");
                } else {
                    battery_string[5] = '#';
                    /* decide color for percentage */
                    switch (capacity[0]) {
                        case  '9':
                        case  '8': battery_string[6] = '2'; break;
                        case  '7':
                        case  '6':
                        case  '5':
                        case  '4':
                        case  '3': battery_string[6] = '5'; break;
                        case  '2':
                        case  '1':
                        case '\n': battery_string[6] = '4'; break;
                    }

                    /* append the capacity */
                    if (capacity[1] == '\n') {
                        /* single digit, add a space */
                        battery_string[7] = ' ';
                        battery_string[8] = capacity[0];
                    } else {
                        battery_string[7] = capacity[0];
                        battery_string[8] = capacity[1];
                    }
                    battery_string[9] = '%';
                    /* Display if the battery is charging */
                    if (status == 'C')
                        strcpy(battery_string + 10, "#5Chg");
                    else
                        battery_string[10] = '\0';
                }

                redraw = true;
                fflush(status_file);
                rewind(status_file);
                fflush(capacity_file);
                rewind(capacity_file);
            }

            if (redraw) {
                int y = height;
                /* clear the screen */
                xcb_poly_fill_rectangle(
                        sam_bar.connection,
                        sam_bar.window,
                        sam_bar.gc,
                        1, /* 1 rectangle */
                        &rectangle);
                /* write the text */
                sb_draw_text(&sam_bar, sam_bar.font_height, stdin_string);
                y -= 4 * sam_bar.font_height + 5 * sam_bar.line_padding;
                sb_draw_text(&sam_bar, y, time_string);
                y -= 3 * sam_bar.font_height + 2 * sam_bar.line_padding;
                sb_draw_text(&sam_bar, y, volume_string);
                y -= 3 * sam_bar.font_height + 2 * sam_bar.line_padding;
                if (battery_string[10] != '\0')
                    y -= sam_bar.font_height + sam_bar.line_padding;
                sb_draw_text(&sam_bar, y, battery_string);
                xcb_flush(sam_bar.connection);
                redraw = false;
            }
        }

        /* relinquish loop resources */
        fclose(volume_file);
        fclose(status_file);
        fclose(capacity_file);
        kill(volume_pid, SIGTERM);
    }

    /* relinquish resources */
    for (i = 0; i < SB_PEN_MAX; i++)
        xcb_render_free_picture(sam_bar.connection, sam_bar.pens[i]);
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
