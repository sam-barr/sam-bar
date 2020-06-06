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

#include "fonts-for-xcb/xcbft/xcbft.h"

#define SB_NUM_CHARS 3
#define SCREEN_NUMBER 0
#define ERROR NULL
#define DPI 96
#define DATE_BUF_SIZE sizeof("#0Jun#0  5#0Fri#0 07#0 38")

#define STRUTS_NUM_ARGS 12

#define DEBUG_BOOL(B) printf("%s\n", (B) ? "true" : "false")
#define TIME(stmt) clock_t t = clock(); \
    stmt \
    t = clock() - t; \
    printf("%f\n", ((double) t) / CLOCKS_PER_SEC);

#define CHARS "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ1234567890:()[] "

// these names correspond to my alacritty config
typedef enum {
    SB_BLACK_B = 0,
    SB_FG,
    SB_GREEN_N,
    SB_PEN_MAX,
} SB_PEN;

#define SB_MAKE_COLOR(r,g,b,a) { 0x##r##r, 0x##g##g, 0x##b##b, 0x##a##a }
xcb_render_color_t SB_PEN_COLOR[SB_PEN_MAX] = {
    SB_MAKE_COLOR(6B, 70, 89, FF),
    SB_MAKE_COLOR(D2, D4, DE, FF),
    SB_MAKE_COLOR(B4, BE, 82, FF),
};
#undef SB_MAKE_COLOR

typedef struct {
    xcb_connection_t *connection;
    xcb_screen_t *screen;
    xcb_window_t window;
    xcb_gcontext_t gc;
    xcb_render_picture_t picture;
    xcb_render_picture_t pens[SB_PEN_MAX];
    xcb_render_glyphset_t glyphset;

    struct xcbft_face_holder faces;
} SamBar;

enum SB_ATOM {
    NET_WM_WINDOW_TYPE = 0,
    NET_WM_WINDOW_TYPE_DOCK,
    NET_WM_STRUT_PARTIAL,
    SB_ATOM_MAX,
};

#define SB_MAKE_ATOM_STRING(str) { str, sizeof(str) - 1 }
struct { char *name; int len; } SB_ATOM_STRING[SB_ATOM_MAX] = {
    SB_MAKE_ATOM_STRING("_NET_WM_WINDOW_TYPE"),
    SB_MAKE_ATOM_STRING("_NET_WM_WINDOW_TYPE_DOCK"),
    SB_MAKE_ATOM_STRING("_NET_WM_STRUT_PARTIAL"),
};
#undef SB_MAKE_ATOM_STRING

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

void sb_test_cookie(SamBar *sam_bar, xcb_void_cookie_t cookie, char *message) {
    xcb_generic_error_t *error = xcb_request_check(sam_bar->connection, cookie);
    if(error != NULL) {
        printf("%s\n", message);
        exit(1);
    }
}

/* mostly stolen from xcbft, but I cached precomputed a bunch of stuff to make it faster */
void sb_draw_text(
        SamBar *sam_bar,
        int16_t x,
        int16_t y,
        struct utf_holder text,
        SB_PEN pen_color)
{

    xcb_render_util_composite_text_stream_t *ts =
        xcb_render_util_composite_text_stream(
                sam_bar->glyphset,
                text.length,
                0);

    xcb_render_util_glyphs_32(ts, x, y, text.length, text.str);
    xcb_render_util_composite_text(
            sam_bar->connection,
            XCB_RENDER_PICT_OP_OVER,
            sam_bar->pens[pen_color],
            sam_bar->picture,
            0,
            0, 0, // x,y
            ts);

	xcb_render_util_composite_text_free(ts);
	xcb_render_util_disconnect(sam_bar->connection);
}

/*
 * Assumptions: strlen(message) % SB_NUM_CHARS == 0
 */
void sb_write_text(SamBar *sam_bar, int y, char *message) {
    char buffer[SB_NUM_CHARS + 1];
    buffer[SB_NUM_CHARS] = '\0';

    for(; *message != '\0' && *message != '\n'; message += SB_NUM_CHARS, y += 24) {
        SB_PEN pen;
        if(*message == '#') {
            pen = message[1] - '0';
            message += 2;
        } else {
            pen = SB_FG;
        }
        for(int i = 0; i < SB_NUM_CHARS; i++)
            buffer[i] = message[i];
        struct utf_holder text = char_to_uint32(buffer);
        sb_draw_text(sam_bar, 2, y, text, pen);
        utf_holder_destroy(text);
    }
}

void sb_handle_sigterm(int signum) {
    exit(signum);
}

int main() {
    /* handle SIGTERM */
    signal(SIGINT, sb_handle_sigterm);
    signal(SIGTERM, sb_handle_sigterm);

    /* Initialize a bunch of shit */
    int ptr = SCREEN_NUMBER;
    SamBar sam_bar;
    sam_bar.connection = xcb_connect(NULL, &ptr);
    sam_bar.screen = xcb_setup_roots_iterator(xcb_get_setup(sam_bar.connection)).data;
    sam_bar.window = xcb_generate_id(sam_bar.connection);
    sam_bar.gc = xcb_generate_id(sam_bar.connection);
    sam_bar.picture = xcb_generate_id(sam_bar.connection);
    char searchlist[100] = {0};
    sprintf(searchlist, "Hasklug Nerd Font:dpi=%d:size=11:antialias=true:style=bold", DPI);
    FcStrSet *fontsearch = xcbft_extract_fontsearch_list(searchlist);
    struct xcbft_patterns_holder font_patterns = xcbft_query_fontsearch_all(fontsearch);
    FcStrSetDestroy(fontsearch);
    sam_bar.faces = xcbft_load_faces(font_patterns, DPI);
    xcbft_patterns_holder_destroy(font_patterns);
    struct utf_holder chars = char_to_uint32(CHARS);
    sam_bar.glyphset = xcbft_load_glyphset(sam_bar.connection, sam_bar.faces, chars, DPI).glyphset;
    utf_holder_destroy(chars);

    /* initialize a visual with 32 bits of depth to allow for alpha channels (transparency) */
    xcb_visualtype_t *visual_type = xcb_aux_find_visual_by_attrs(sam_bar.screen, -1, 32);
    int depth = xcb_aux_get_depth_of_visual(sam_bar.screen, visual_type->visual_id);
    xcb_colormap_t colormap = xcb_generate_id(sam_bar.connection);
    xcb_create_colormap(
            sam_bar.connection,
            XCB_COLORMAP_ALLOC_NONE,
            colormap,
            sam_bar.screen->root,
            visual_type->visual_id);

    int width = 34;
    int height = sam_bar.screen->height_in_pixels;
    int mask = XCB_CW_BACK_PIXEL | XCB_CW_BORDER_PIXEL | XCB_CW_OVERRIDE_REDIRECT | XCB_CW_COLORMAP;
    /* Then we can just use 32-bit ARGB colors directly */
    /* jsyk: 0F1117B1 is really pretty, but doesn't match your theme */
    int values[4] = { 0xB80F1117, 0xFFFFFFFF, true, colormap };
    xcb_void_cookie_t cookie = xcb_create_window_checked(
            sam_bar.connection,
            depth,
            sam_bar.window,
            sam_bar.screen->root,
            0, 0, //top corner of screen
            width, height,
            0, // border width
            XCB_WINDOW_CLASS_INPUT_OUTPUT,
            visual_type->visual_id,
            mask,
            values);
    sb_test_cookie(&sam_bar, cookie, "xcb_create_window_checked failed");
    mask = XCB_GC_FOREGROUND | XCB_GC_BACKGROUND;
    cookie = xcb_create_gc_checked(
            sam_bar.connection,
            sam_bar.gc,
            sam_bar.window,
            mask,
            values);
    sb_test_cookie(&sam_bar, cookie, "xcb_create_gc_checked failed");
    {
        const xcb_render_query_pict_formats_reply_t *fmt_rep =
            xcb_render_util_query_formats(sam_bar.connection);
        xcb_render_pictforminfo_t *fmt = xcb_render_util_find_standard_format(
                fmt_rep,
                XCB_PICT_STANDARD_ARGB_32);
        mask = XCB_RENDER_CP_POLY_MODE | XCB_RENDER_CP_POLY_EDGE;
        values[0] = XCB_RENDER_POLY_MODE_IMPRECISE;
        values[1] = XCB_RENDER_POLY_EDGE_SMOOTH;
        cookie = xcb_render_create_picture_checked(
                sam_bar.connection,
                sam_bar.picture,
                sam_bar.window,
                fmt->id,
                mask,
                values);
        sb_test_cookie(&sam_bar, cookie, "xcb_create_picture_checked failed");
    }
    for(int i = 0; i < SB_PEN_MAX; i++)
        sam_bar.pens[i] = xcbft_create_pen(sam_bar.connection, SB_PEN_COLOR[i]);

    /* setup struts so windows don't overlap the bar */
    int struts[STRUTS_NUM_ARGS] = {0};
    struts[LEFT] = width;
    struts[LEFT_START_Y] = struts[RIGHT_START_Y] = 0;
    struts[LEFT_END_Y] = struts[RIGHT_END_Y] = height;
    struts[TOP_START_X] = struts[BOTTOM_START_X] = 0;
    struts[TOP_END_X] = struts[BOTTOM_END_X] = width;

    /* load atoms */
    xcb_intern_atom_cookie_t atom_cookies[SB_ATOM_MAX];
    for(int i = 0; i < SB_ATOM_MAX; i++) {
        atom_cookies[i] = xcb_intern_atom(
                sam_bar.connection,
                0, // "atom will be created if it does not already exist"
                SB_ATOM_STRING[i].len,
                SB_ATOM_STRING[i].name);
    }
    xcb_atom_t atoms[SB_ATOM_MAX];
    for(int i = 0; i < SB_ATOM_MAX; i++) {
        xcb_intern_atom_reply_t *reply = xcb_intern_atom_reply(
                sam_bar.connection,
                atom_cookies[i],
                ERROR);
        atoms[i] = reply->atom;
        free(reply);
    }

    xcb_change_property(
            sam_bar.connection,
            XCB_PROP_MODE_REPLACE,
            sam_bar.window,
            atoms[NET_WM_WINDOW_TYPE],
            XCB_ATOM_ATOM,
            32, // MAGIC NUMBER??
            1, // sending 1 argument
            &atoms[NET_WM_WINDOW_TYPE_DOCK]);
    xcb_change_property(
            sam_bar.connection,
            XCB_PROP_MODE_REPLACE,
            sam_bar.window,
            atoms[NET_WM_STRUT_PARTIAL],
            XCB_ATOM_CARDINAL,
            32, // MAGIC NUMBER ?
            STRUTS_NUM_ARGS,
            struts);

    xcb_map_window(sam_bar.connection, sam_bar.window);
    xcb_flush(sam_bar.connection);

    /* main loop setup */
    struct pollfd pollfds[2];
    pollfds[0].fd = STDIN_FILENO;
    pollfds[0].events = POLLIN;
    pollfds[1].fd = timerfd_create(CLOCK_MONOTONIC, 0);
    pollfds[1].events = POLLIN;
    struct itimerspec ts;
    ts.it_interval.tv_sec = 1;
    ts.it_interval.tv_nsec = 0;
    ts.it_value.tv_sec = 0;
    ts.it_value.tv_nsec = 1;
    timerfd_settime(pollfds[1].fd, 0, &ts, NULL);

    /* main loop */
    char *stdin_line = malloc(1); // before we read stdin we free line, so start malloced
    *stdin_line = '\0';
    char time_string[DATE_BUF_SIZE] = {0};
    bool redraw = false;
    while(1) {
        /* blocks until one of the fds becomes open */
        poll(pollfds, 2, -1);
        if(pollfds[0].revents & POLLHUP) {
            /* stdin died, and so do we */
            break;
        } else if(pollfds[0].revents & POLLIN) {
            redraw = true;
            free(stdin_line);
            size_t len = 0;
            getline(&stdin_line, &len, stdin);
            printf("length: %ld\n", strlen(stdin_line));
            if(*stdin_line == 'X' || *stdin_line == EOF) {
                free(stdin_line);
                break;
            }
        } else if(pollfds[1].revents & POLLIN) {
            uint64_t num;
            read(pollfds[1].fd, &num, sizeof(uint64_t));

            char prev_minute = time_string[DATE_BUF_SIZE-2];
            time_t rawtime;
            time(&rawtime);
            struct tm *info = localtime(&rawtime);
            strftime(time_string, DATE_BUF_SIZE, "#0%b#0 %e#0%a#0 %I#0 %M", info);
            redraw = prev_minute != time_string[DATE_BUF_SIZE-2];
            /* only redraw if the minute changed */
        }

        if(redraw) {
            xcb_rectangle_t rectangle = { 0, 0, width, height };
            xcb_poly_fill_rectangle(
                    sam_bar.connection,
                    sam_bar.window,
                    sam_bar.gc,
                    1, // 1 rectangle
                    &rectangle);
            sb_write_text(&sam_bar, 20, stdin_line);
            sb_write_text(&sam_bar, height - 105, time_string);
            xcb_flush(sam_bar.connection);
            redraw = false;
        }
    }

    xcbft_face_holder_destroy(sam_bar.faces);
    xcb_free_gc(sam_bar.connection, sam_bar.gc);
    xcb_render_free_picture(sam_bar.connection, sam_bar.picture);
    for(int i = 0; i < SB_PEN_MAX; i++) {
        xcb_render_free_picture(sam_bar.connection, sam_bar.pens[i]);
    }
    xcb_disconnect(sam_bar.connection);
    FcFini();
    // if valgrind reports more than 18,612 reachable that might be a leak
}
