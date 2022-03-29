#include "window.h"

int main() {
	struct window *window = window_create();
	window_show(window);

	while (!window->close_requested) {
		window_poll_event(window);
	}

	window_destroy(window);
}
