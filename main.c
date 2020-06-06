#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <unistd.h>
#include <poll.h>
#include <signal.h> 
#include <time.h>

#include <sys/timerfd.h>

#include <xcb/xcb.h>
#include <xcb/xcb_aux.h>
#include <xcb/xcb_renderutil.h>

#include "fonts-for-xcb/xcbft/xcbft.h"
#include "fonts-for-xcb/utf8_utils/utf8.h"

#define SB_NUM_CHARS 3
#define SCREEN_NUMBER 0
#define ERROR NULL
#define DPI 96
#define DATE_BUF_SIZE sizeof("#1Jun#1 05#1Fri#1 07#1 38")
#define STDIN_LINE_LENGTH 50
#define VOLUME_LENGTH 15

#define STRUTS_NUM_ARGS 12

#define DEBUG_BOOL(B) printf("%s\n", (B) ? "true" : "false")
#define TIME(stmt) clock_t t = clock(); \
    stmt \
    t = clock() - t; \
    printf("%f\n", ((double) t) / CLOCKS_PER_SEC);

#define CHARS "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ1234567890[] %"

/* these names correspond to my alacritty config */
typedef enum {
    SB_FG = 0,
    SB_BLACK_B,
    SB_GREEN_N,
    SB_CYAN_B,
    SB_PEN_MAX
} SB_PEN;

#define SB_MAKE_COLOR(r,g,b,a) { 0x##r##r, 0x##g##g, 0x##b##b, 0x##a##a }
const xcb_render_color_t SB_PEN_COLOR[SB_PEN_MAX] = {
    SB_MAKE_COLOR(D2, D4, DE, FF),
    SB_MAKE_COLOR(6B, 70, 89, FF),
    SB_MAKE_COLOR(B4, BE, 82, FF),
    SB_MAKE_COLOR(95, C4, CE, FF),
};
#undef SB_MAKE_COLOR

enum SB_ATOM {
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

    struct xcbft_face_holder faces;
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

void sb_test_cookie(SamBar *sam_bar, xcb_void_cookie_t cookie, const char *message) {
    xcb_generic_error_t *error = xcb_request_check(sam_bar->connection, cookie);
    if(error != NULL) {
        printf("%s\n", message);
        exit(1);
    }
}

/*
 * Assumptions: strlen(message) % SB_NUM_CHARS == 0
 * This doesn't handle unicode in the slightest
 */
void sb_draw_text(SamBar *sam_bar, int y, char *message) {
    char buffer[SB_NUM_CHARS + 1] = {0};
    SB_PEN pen;
    int i;
    struct utf_holder text;
    xcb_render_util_composite_text_stream_t *text_stream;

    for(; *message != '\0' && *message != '\n'; message += SB_NUM_CHARS, y += 24) {
        if(*message == '#') {
            pen = message[1] - '0';
            message += 2;
        } else {
            pen = SB_FG;
        }
        for(i = 0; i < SB_NUM_CHARS; i++)
            buffer[i] = message[i];

        text = char_to_uint32(buffer);
        text_stream = xcb_render_util_composite_text_stream(
                sam_bar->glyphset,
                text.length,
                0);
        xcb_render_util_glyphs_32(text_stream, 2, y, text.length, text.str);
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

int main() {
    SamBar sam_bar;
    int width, height, i;

    { /* initialize most of the xcb stuff sam_bar */
        int ptr = SCREEN_NUMBER;
        sam_bar.connection = xcb_connect(NULL, &ptr);
        if(xcb_connection_has_error(sam_bar.connection)) {
            xcb_disconnect(sam_bar.connection);
            return -1;
        }
        sam_bar.screen = xcb_setup_roots_iterator(xcb_get_setup(sam_bar.connection)).data;
        sam_bar.window = xcb_generate_id(sam_bar.connection);
        sam_bar.gc = xcb_generate_id(sam_bar.connection);
        sam_bar.picture = xcb_generate_id(sam_bar.connection);
        sam_bar.colormap = xcb_generate_id(sam_bar.connection);
        sam_bar.visual_id = xcb_aux_find_visual_by_attrs(sam_bar.screen, -1, 32)->visual_id;
        for(i = 0; i < SB_PEN_MAX; i++)
            sam_bar.pens[i] = xcbft_create_pen(sam_bar.connection, SB_PEN_COLOR[i]);
    }

    /* initialize a 32 bit colormap */
    xcb_create_colormap(
            sam_bar.connection,
            XCB_COLORMAP_ALLOC_NONE,
            sam_bar.colormap,
            sam_bar.screen->root,
            sam_bar.visual_id);

    width = 34;
    height = sam_bar.screen->height_in_pixels;

    { /* load up fonts and glyphs */
        char searchlist[100] = {0};
        FcStrSet *fontsearch;
        struct xcbft_patterns_holder font_patterns;
        struct utf_holder chars;

        sprintf(searchlist, "Hasklug Nerd Font:dpi=%d:size=11:antialias=true:style=bold", DPI);
        fontsearch = xcbft_extract_fontsearch_list(searchlist);
        font_patterns = xcbft_query_fontsearch_all(fontsearch);
        FcStrSetDestroy(fontsearch);
        sam_bar.faces = xcbft_load_faces(font_patterns, DPI);
        xcbft_patterns_holder_destroy(font_patterns);
        chars = char_to_uint32(CHARS);
        sam_bar.glyphset = xcbft_load_glyphset(
                sam_bar.connection,
                sam_bar.faces,
                chars,
                DPI).glyphset;
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
        values[0] = 0xB80F1117; /* jsyk: 0F1117B1 is pretty, but doesn't match your theme */
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
        int values[2] = { 0xB80F1117, 0xFFFFFFFF };
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
        for(i = 0; i < SB_ATOM_MAX; i++) {
            atom_cookies[i] = xcb_intern_atom(
                    sam_bar.connection,
                    0, /* "atom will be created if it does not already exist" */
                    SB_ATOM_STRING[i].len,
                    SB_ATOM_STRING[i].name);
        }
        for(i = 0; i < SB_ATOM_MAX; i++) {
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

    {
        /* main loop setup */
        struct pollfd pollfds[3];
        struct itimerspec ts;
        bool redraw = false;
        unsigned long int elapsed = 0; /* hopefully I don't leave this running for > 100 years */
        xcb_rectangle_t rectangle;
        char time_string[DATE_BUF_SIZE] = {0},
             stdin_string[STDIN_LINE_LENGTH] = {0},
             volume_string[VOLUME_LENGTH] = {0};
        int volume_pipe[2];
        FILE *volume_file;
        pid_t volume_pid;

        pipe(volume_pipe);
        volume_pid = fork();
        if(volume_pid == 0) {
            dup2(volume_pipe[1], STDOUT_FILENO);
            close(volume_pipe[0]);
            close(volume_pipe[1]);
            execl(INSTALL_DIR "/listen-volume.sh", "", (char *)NULL);
        }
        volume_file = fdopen(volume_pipe[0], "r");

        pollfds[0].fd = STDIN_FILENO;
        pollfds[0].events = POLLIN;
        pollfds[1].fd = timerfd_create(CLOCK_MONOTONIC, 0);
        pollfds[1].events = POLLIN;
        pollfds[2].fd = volume_pipe[0];
        pollfds[2].events = POLLIN;

        ts.it_interval.tv_sec = 1; /* fire every second */
        ts.it_interval.tv_nsec = 0;
        ts.it_value.tv_sec = 0;
        ts.it_value.tv_nsec = 1; /* initial fire happens *basically* instantly */
        timerfd_settime(pollfds[1].fd, 0, &ts, NULL);

        rectangle.x = rectangle.y = 0;
        rectangle.width = width;
        rectangle.height = height;

        /* main loop */
        for(;;) {
            /* blocks until one of the fds becomes open */
            poll(pollfds, 3, -1);
            if(pollfds[0].revents & POLLHUP) {
                /* stdin died, and so do we */
                break;
            } else if(pollfds[0].revents & POLLIN) {
                fgets(stdin_string, STDIN_LINE_LENGTH, stdin);
                redraw = true;
                if(*stdin_string == 'X' || *stdin_string == EOF)
                    break;
            } else if(pollfds[1].revents & POLLIN) {
                uint64_t num;
                time_t rawtime;
                struct tm *info;
                char prev_minute;

                read(pollfds[1].fd, &num, sizeof(uint64_t));
                elapsed += num;

                prev_minute = time_string[DATE_BUF_SIZE-2];
                time(&rawtime);
                info = localtime(&rawtime);
                strftime(time_string, DATE_BUF_SIZE, "#1%b#1 %d#1%a#1 %I#1 %M", info);
                redraw = prev_minute != time_string[DATE_BUF_SIZE-2];
            } else if(pollfds[2].revents & POLLIN) {
                fgets(volume_string, VOLUME_LENGTH, volume_file);
                redraw = true;
            }

            if(redraw) {
                /* clear the screen */
                xcb_poly_fill_rectangle(
                        sam_bar.connection,
                        sam_bar.window,
                        sam_bar.gc,
                        1, /* 1 rectangle */
                        &rectangle);
                /* write the text */
                sb_draw_text(&sam_bar, 20, stdin_string);
                sb_draw_text(&sam_bar, height - 105, time_string);
                sb_draw_text(&sam_bar, height - 175, volume_string);
                xcb_flush(sam_bar.connection);
                redraw = false;
            }
        }

        fclose(volume_file);
        kill(volume_pid, SIGTERM);
    }

    /* relinquish resources */
    for(i = 0; i < SB_PEN_MAX; i++)
        xcb_render_free_picture(sam_bar.connection, sam_bar.pens[i]);
    xcb_render_free_picture(sam_bar.connection, sam_bar.picture);
    xcb_free_colormap(sam_bar.connection, sam_bar.colormap);
    xcb_free_gc(sam_bar.connection, sam_bar.gc);
    xcbft_face_holder_destroy(sam_bar.faces);
    xcb_render_util_disconnect(sam_bar.connection);
    xcb_disconnect(sam_bar.connection);
    xcbft_done();
    /* if valgrind reports more than 18,612 reachable that might be a leak */

    return 0;
}
