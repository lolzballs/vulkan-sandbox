#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <xcb/xcb.h>

#include "window.h"

static xcb_screen_t *
get_screen(xcb_connection_t *connection) {
	const xcb_setup_t *setup = xcb_get_setup(connection);
	xcb_screen_iterator_t iter = xcb_setup_roots_iterator(setup);
	return iter.data;
}

struct window *
window_create() {
	struct window *ini = calloc(1, sizeof(struct window));

	xcb_connection_t *xcb_connection = xcb_connect(NULL, NULL);
	assert(xcb_connection_has_error(xcb_connection) == 0);

	xcb_screen_t *screen = get_screen(xcb_connection);

	const uint32_t valwin[] = {
		XCB_EVENT_MASK_STRUCTURE_NOTIFY,
	};

	xcb_void_cookie_t cookie;

	xcb_window_t wid = xcb_generate_id(xcb_connection);
	cookie = xcb_create_window_checked(xcb_connection,
			XCB_COPY_FROM_PARENT, wid, screen->root,
			0, 0, 640, 480, 0,
			XCB_WINDOW_CLASS_INPUT_OUTPUT, screen->root_visual,
			XCB_CW_EVENT_MASK, valwin);
	assert(xcb_request_check(xcb_connection, cookie) == 0);

	const char *window_name = "vulkan";
	cookie = xcb_change_property_checked(xcb_connection,
			XCB_PROP_MODE_REPLACE, wid,
			XCB_ATOM_WM_NAME, XCB_ATOM_STRING, 8,
			strlen(window_name), window_name);
	assert(xcb_request_check(xcb_connection, cookie) == 0);

	xcb_intern_atom_cookie_t protocols_cookie =
		xcb_intern_atom(xcb_connection, 1, 12, "WM_PROTOCOLS");
	xcb_intern_atom_reply_t *protocols_reply =
		xcb_intern_atom_reply(xcb_connection, protocols_cookie, 0);
	xcb_intern_atom_cookie_t delete_cookie = 
		xcb_intern_atom(xcb_connection, 0, 16, "WM_DELETE_WINDOW");
	xcb_intern_atom_reply_t *delete_reply =
		xcb_intern_atom_reply(xcb_connection, delete_cookie, 0);

	cookie = xcb_change_property_checked(xcb_connection,
			XCB_PROP_MODE_APPEND, wid,
			protocols_reply->atom, XCB_ATOM_ATOM, 32,
			1, &delete_reply->atom);
	assert(xcb_request_check(xcb_connection, cookie) == 0);

	ini->xcb_connection = xcb_connection;
	ini->window_id = wid;
	ini->atom_delete_window = delete_reply->atom;
	ini->close_requested = false;

	free(protocols_reply);
	protocols_reply = NULL;
	free(delete_reply);
	delete_reply = NULL;

	return ini;
}

void
window_destroy(struct window *window) {
	xcb_destroy_window(window->xcb_connection, window->window_id);
	xcb_disconnect(window->xcb_connection);
	free(window);
}

void
window_show(struct window *window) {
	xcb_map_window(window->xcb_connection, window->window_id);
	xcb_flush(window->xcb_connection);
}

void
window_poll_event(struct window *window) {
	xcb_generic_event_t *event;
	while ((event = xcb_poll_for_event(window->xcb_connection)) != NULL) {
		switch (event->response_type & 0x7F) {
			case XCB_CONFIGURE_NOTIFY: {
				xcb_configure_notify_event_t *resize_event =
					(xcb_configure_notify_event_t *) event;
				window->width = resize_event->width;
				window->height = resize_event->height;
				window->resized = true;
			}
			case XCB_CLIENT_MESSAGE: {
				xcb_client_message_event_t *client_event =
					(xcb_client_message_event_t *) event;
				if (client_event->data.data32[0] == window->atom_delete_window) {
					window->close_requested = true;
				}
				break;
			}
			default:
				printf("unknown event: %d\n", event->response_type);
		}

		free(event);
		event = NULL;
	}

	if (xcb_connection_has_error(window->xcb_connection) > 0) {
		puts("connection closed");
		exit(1);
	}
}
