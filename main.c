#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>

#include <xcb/xcb.h>

#include "fonts-for-xcb/xcbft/xcbft.h"

#define SB_NUM_CHARS 3
#define SCREEN_NUMBER 0
#define ERROR NULL
#define DPI 96

#define STRUTS_NUM_ARGS 12

#define sb_get_atom(sam_bar, atom_name) __sb_get_atom(sam_bar, atom_name, sizeof(atom_name)-1)

#define DEBUG_BOOL(B) printf("%s\n", (B) ? "true" : "false")

typedef struct {
    xcb_connection_t *connection;
    xcb_screen_t *screen;
    xcb_window_t window;

    struct xcbft_face_holder faces;
} SamBar;

enum StrutPartial {
    LEFT,
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

xcb_atom_t __sb_get_atom(SamBar *sam_bar, char *atom_name, int atom_name_length) {
    xcb_intern_atom_cookie_t atom_cookie = xcb_intern_atom(sam_bar->connection, 0, atom_name_length, atom_name);
    xcb_intern_atom_reply_t *reply = xcb_intern_atom_reply(sam_bar->connection, atom_cookie, ERROR);
    xcb_atom_t atom = reply->atom;
    free(reply);
    return atom;
}

void sb_test_cookie(SamBar *sam_bar, xcb_void_cookie_t cookie, char *message) {
    xcb_generic_error_t *error = xcb_request_check(sam_bar->connection, cookie);
    if(error != NULL) {
        printf("%s\n", message);
        exit(1);
    }
}

/*
 * Assumptions: strlen(message) % SB_NUM_CHARS == 0
 */
void sb_write_text(SamBar *sam_bar, int y, char *message) {
    char buffer[SB_NUM_CHARS + 1];
    buffer[SB_NUM_CHARS] = '\0';
    xcb_render_color_t text_color;
    text_color.red = 0x6B6B;
    text_color.green = 0x7070;
    text_color.blue = 0x8989;
    text_color.alpha = 0xFFFF;

    for(; *message != '\0'; message += SB_NUM_CHARS, y += 24) {
        for(int i = 0; i < SB_NUM_CHARS; i++)
            buffer[i] = message[i];
        struct utf_holder text = char_to_uint32(buffer);
        xcbft_draw_text(
                sam_bar->connection,
                sam_bar->window,
                3, y,
                text,
                text_color,
                sam_bar->faces,
                DPI);
        utf_holder_destroy(text);
    }
}

int main() {
    /* Initialize a bunch of shit */
    int ptr = SCREEN_NUMBER;
    SamBar sam_bar;
    sam_bar.connection = xcb_connect(NULL, &ptr);
    sam_bar.screen = xcb_setup_roots_iterator(xcb_get_setup(sam_bar.connection)).data;
    sam_bar.window = xcb_generate_id(sam_bar.connection);
    char searchlist[100] = {0};
    sprintf(searchlist, "Hasklug Nerd Font:dpi=%d:size=11:antialias=true:style=bold", DPI);
    FcStrSet *fontsearch = xcbft_extract_fontsearch_list(searchlist);
    struct xcbft_patterns_holder font_patterns = xcbft_query_fontsearch_all(fontsearch);
    FcStrSetDestroy(fontsearch);
    sam_bar.faces = xcbft_load_faces(font_patterns, DPI);
    xcbft_patterns_holder_destroy(font_patterns);

    int width = 32;
    int mask = XCB_CW_BACK_PIXEL | XCB_CW_BORDER_PIXEL | XCB_CW_OVERRIDE_REDIRECT;
    xcb_alloc_color_reply_t *reply = xcb_alloc_color_reply(
            sam_bar.connection,
            xcb_alloc_color(
                sam_bar.connection,
                sam_bar.screen->default_colormap,
                0x0F0F,
                0x1111,
                0x1717),
            ERROR);
    int values[3] = {reply->pixel, reply->pixel, true};
    free(reply);
    xcb_void_cookie_t cookie = xcb_create_window_checked(
            sam_bar.connection,
            sam_bar.screen->root_depth,
            sam_bar.window,
            sam_bar.screen->root,
            0, //top corner of screen
            0,
            width,
            sam_bar.screen->height_in_pixels,
            0, // border width
            XCB_WINDOW_CLASS_INPUT_OUTPUT,
            sam_bar.screen->root_visual,
            mask,
            values);
    sb_test_cookie(&sam_bar, cookie, "xcb_create_window_checked failed");

    /* setup struts so windows don't overlap the bar */
    int struts[STRUTS_NUM_ARGS] = {0};
    struts[LEFT] = width;
    struts[LEFT_START_Y] = struts[RIGHT_START_Y] = 0;
    struts[LEFT_END_Y] = struts[RIGHT_END_Y] = sam_bar.screen->height_in_pixels;
    struts[TOP_START_X] = struts[BOTTOM_START_X] = 0;
    struts[TOP_END_X] = struts[BOTTOM_END_X] = width;
    xcb_atom_t NET_WM_WINDOW_TYPE = sb_get_atom(&sam_bar, "_NET_WM_WINDOW_TYPE");
    xcb_atom_t NET_WM_WINDOW_TYPE_DOCK = sb_get_atom(&sam_bar, "_NET_WM_WINDOW_TYPE_DOCK");
    xcb_atom_t NET_WM_STRUT_PARTIAL = sb_get_atom(&sam_bar, "_NET_WM_STRUT_PARTIAL");

    xcb_change_property(
            sam_bar.connection,
            XCB_PROP_MODE_REPLACE,
            sam_bar.window,
            NET_WM_WINDOW_TYPE,
            XCB_ATOM_ATOM,
            32, // MAGIC NUMBER??
            1, // sending 1 argument
            &NET_WM_WINDOW_TYPE_DOCK);

    xcb_change_property(
            sam_bar.connection,
            XCB_PROP_MODE_REPLACE,
            sam_bar.window,
            NET_WM_STRUT_PARTIAL,
            XCB_ATOM_CARDINAL,
            32, // MAGIC NUMBER ?
            STRUTS_NUM_ARGS,
            struts);

    xcb_map_window(sam_bar.connection, sam_bar.window);
    xcb_flush(sam_bar.connection);

    /* try to do text stuff */
    sb_write_text(&sam_bar, 40, "123456789");
    sb_write_text(&sam_bar, 150, " 1  2 [3] 4  5  6  7  8  9 ");
    xcb_flush(sam_bar.connection);

    getchar();

    xcbft_face_holder_destroy(sam_bar.faces);
    xcb_disconnect(sam_bar.connection);
    FcFini();
}
