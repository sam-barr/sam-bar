#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>

#include <xcb/xcb.h>
#include <X11/Xft/Xft.h>
#include <X11/Xlib.h>

#define SB_NUM_CHARS 3
#define SCREEN_NUMBER 0
#define ERROR NULL

#define STRUTS_NUM_ARGS 12

#define sb_get_atom(sam_bar, atom_name) __sb_get_atom(sam_bar, atom_name, sizeof(atom_name)-1)

#define DEBUG_BOOL(B) printf("%s\n", (B) ? "true" : "false")

typedef struct _SamBar {
    xcb_connection_t *connection;
    xcb_screen_t *screen;
    xcb_window_t window;
    xcb_pixmap_t pixmap;
    xcb_gcontext_t gc;
    
    Display *display;
    XftFont *font;
    XGlyphInfo glyph_info;
    Visual *visual;
    XftDraw *xft_draw;
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

int main() {
    /* Initialize a bunch of shit */
    int ptr = SCREEN_NUMBER;
    SamBar sam_bar;
    sam_bar.connection = xcb_connect(NULL, &ptr);
    sam_bar.screen = xcb_setup_roots_iterator(xcb_get_setup(sam_bar.connection)).data;
    sam_bar.window = xcb_generate_id(sam_bar.connection);
    sam_bar.pixmap = xcb_generate_id(sam_bar.connection);
    sam_bar.gc = xcb_generate_id(sam_bar.connection);
    sam_bar.display = XOpenDisplay(NULL);
    sam_bar.font = XftFontOpenName(sam_bar.display, SCREEN_NUMBER, "Hasklug Nerd Font");
    XftTextExtents8(sam_bar.display, sam_bar.font, (FcChar8 *)"A", 1, &sam_bar.glyph_info);
    sam_bar.visual = XDefaultVisual(sam_bar.display, SCREEN_NUMBER);

    int width = (SB_NUM_CHARS + 1) * sam_bar.glyph_info.xOff;
    int mask = XCB_CW_BACK_PIXEL | XCB_CW_BORDER_PIXEL | XCB_CW_OVERRIDE_REDIRECT;
    int values[3] = {sam_bar.screen->white_pixel, sam_bar.screen->black_pixel, true};

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
    cookie = xcb_create_pixmap_checked(
            sam_bar.connection,
            sam_bar.screen->root_depth,
            sam_bar.pixmap,
            sam_bar.window,
            width,
            sam_bar.screen->height_in_pixels);
    sb_test_cookie(&sam_bar, cookie, "xcb_create_pixmap_checked failed");
    values[0] = sam_bar.screen->black_pixel;
    values[1] = sam_bar.screen->white_pixel;
    cookie = xcb_create_gc_checked(
            sam_bar.connection,
            sam_bar.gc,
            sam_bar.pixmap,
            XCB_GC_FOREGROUND,
            values);
    sb_test_cookie(&sam_bar, cookie, "xcb_create_gc_checked failed");
    sam_bar.xft_draw = XftDrawCreate(
            sam_bar.display,
            sam_bar.pixmap,
            sam_bar.visual,
            sam_bar.screen->default_colormap);

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

    /* try to draw a string */
    XftColor color;
    XftColorAllocName(
            sam_bar.display,
            sam_bar.visual,
            sam_bar.screen->default_colormap,
            "#000000", 
            &color);
    //XftDrawString8(
    //        sam_bar.xft_draw,
    //        &color,
    //        sam_bar.font,
    //        5,
    //        5,
    //        (FcChar8 *)"HW!",
    //        3);
    xcb_rectangle_t rect[4] = {{0, 0, width, sam_bar.screen->height_in_pixels}};
    cookie = xcb_poly_fill_rectangle_checked(
            sam_bar.connection,
            sam_bar.pixmap,
            sam_bar.gc,
            1,
            rect);
    sb_test_cookie(&sam_bar, cookie, "poly fill failed");
    cookie = xcb_copy_area_checked(
            sam_bar.connection,
            sam_bar.pixmap,
            sam_bar.window,
            sam_bar.gc,
            0, 0, 0, 0,
            width,
            sam_bar.screen->height_in_pixels);
    sb_test_cookie(&sam_bar, cookie, "copy area failed");
    xcb_flush(sam_bar.connection);
    sleep(2);

    XftDrawDestroy(sam_bar.xft_draw);
    XftFontClose(sam_bar.display, sam_bar.font);
    XftColorFree(sam_bar.display, sam_bar.visual, sam_bar.screen->default_colormap, &color);
    XCloseDisplay(sam_bar.display);
    xcb_free_gc(sam_bar.connection, sam_bar.gc);
    xcb_free_pixmap(sam_bar.connection, sam_bar.pixmap);
    xcb_disconnect(sam_bar.connection);
}
