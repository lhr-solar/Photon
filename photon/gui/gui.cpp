/*[μ] the photon graphical user interface*/
#include <stdio.h>
#include <string>
#include <cstring>
#include "gui.hpp"
#include "../engine/include.hpp"

Gui::Gui(){};
Gui::~Gui(){
    xcb_destroy_window(connection, window);
	xcb_disconnect(connection);
};

void Gui::initWindow(){
    initxcbConnection();
    setupWindow();
}

xcb_visualtype_t* get_argb_visual(xcb_screen_t* screen) {
    xcb_depth_iterator_t depth_iter = xcb_screen_allowed_depths_iterator(screen);
    for (; depth_iter.rem; xcb_depth_next(&depth_iter)) {
        if (depth_iter.data->depth == 32) {
            xcb_visualtype_iterator_t visual_iter = xcb_depth_visuals_iterator(depth_iter.data);
            for (; visual_iter.rem; xcb_visualtype_next(&visual_iter)) {
                return visual_iter.data; // Found ARGB visual
            }
        }
    }
    return NULL; // No ARGB visual on this screen
}

void Gui::initxcbConnection(){
    const xcb_setup_t *setup;
    xcb_screen_iterator_t iter;
    int src;

    connection = xcb_connect(NULL, &src);
    assert(connection);
    if(xcb_connection_has_error(connection)){
        printf("[!] Could not find a compatible Vulkan ICD!\n");
        fflush(stdout);
        exit(1);
    }

    setup = xcb_get_setup(connection);
    iter = xcb_setup_roots_iterator(setup);
    while(src-- > 0)
        xcb_screen_next(&iter);
    screen = iter.data;
}

static inline xcb_intern_atom_reply_t* intern_atom_helper(xcb_connection_t *conn, bool only_if_exists, const char *str)
{
	xcb_intern_atom_cookie_t cookie = xcb_intern_atom(conn, only_if_exists, strlen(str), str);
	return xcb_intern_atom_reply(conn, cookie, NULL);
}

void Gui::setupWindow(){
    uint32_t value_mask, value_list[32];

    window = xcb_generate_id(connection);

    xcb_visualtype_t* argb_visual = get_argb_visual(screen);
    xcb_colormap_t colormap = xcb_generate_id(connection);
    xcb_create_colormap(connection, XCB_COLORMAP_ALLOC_NONE, colormap, screen->root, argb_visual->visual_id);


	value_mask = XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK;
    value_list[0] = screen-> black_pixel;
	value_list[1] =
		XCB_EVENT_MASK_KEY_RELEASE |
		XCB_EVENT_MASK_KEY_PRESS |
		XCB_EVENT_MASK_EXPOSURE |
		XCB_EVENT_MASK_STRUCTURE_NOTIFY |
		XCB_EVENT_MASK_POINTER_MOTION |
		XCB_EVENT_MASK_BUTTON_PRESS |
		XCB_EVENT_MASK_BUTTON_RELEASE;

    if(settings.fullscreen){
        width = screen->width_in_pixels;
		height = screen->height_in_pixels;
    }
    
    xcb_create_window(connection,
		XCB_COPY_FROM_PARENT,
		window, screen->root,
		0, 0, width, height, 0,
		XCB_WINDOW_CLASS_INPUT_OUTPUT,
		screen->root_visual,
		value_mask, value_list);
    
    xcb_intern_atom_reply_t* reply = intern_atom_helper(connection, true, "WM_PROTOCOLS");
	atom_wm_delete_window = intern_atom_helper(connection, false, "WM_DELETE_WINDOW");

    xcb_change_property(connection, XCB_PROP_MODE_REPLACE,
		window, (*reply).atom, 4, 32, 1,
		&(*atom_wm_delete_window).atom);

    std::string windowTitle = getWindowTitle();
	xcb_change_property(connection, XCB_PROP_MODE_REPLACE,
		window, XCB_ATOM_WM_NAME, XCB_ATOM_STRING, 8,
		title.size(), windowTitle.c_str());

    free(reply);

    std::string wm_class;
	wm_class = wm_class.insert(0, name);
	wm_class = wm_class.insert(name.size(), 1, '\0');
	wm_class = wm_class.insert(name.size() + 1, title);
	wm_class = wm_class.insert(wm_class.size(), 1, '\0');
	xcb_change_property(connection, XCB_PROP_MODE_REPLACE, window, XCB_ATOM_WM_CLASS, XCB_ATOM_STRING, 8, wm_class.size() + 2, wm_class.c_str());

    if (settings.fullscreen){
        xcb_intern_atom_reply_t *atom_wm_state = intern_atom_helper(connection, false, "_NET_WM_STATE");
        xcb_intern_atom_reply_t *atom_wm_fullscreen = intern_atom_helper(connection, false, "_NET_WM_STATE_FULLSCREEN");
        xcb_change_property(connection,
            XCB_PROP_MODE_REPLACE,
            window, atom_wm_state->atom,
            XCB_ATOM_ATOM, 32, 1,
            &(atom_wm_fullscreen->atom));
        free(atom_wm_fullscreen);
        free(atom_wm_state);
    }

	xcb_map_window(connection, window);

    xcb_flush(connection);
}

std::string Gui::getWindowTitle() const {
    // TODO: accelerator + fps
	std::string windowTitle =  title ; //+ deviceProperties.deviceName };
    //windowTitle += " - " + std::to_string(frameCounter) + " fps";
	return windowTitle;
}
