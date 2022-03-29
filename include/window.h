#ifndef WINDOW_H
#define WINDOW_H

#include <stdbool.h>

#include <xcb/xcb.h>

struct window {
	xcb_connection_t *xcb_connection;
	xcb_window_t window_id;

	xcb_atom_t atom_delete_window;
	bool close_requested;
};

struct window *window_create();
void window_destroy(struct window *window);
void window_show(struct window *window);
void window_poll_event(struct window *window);

#endif
