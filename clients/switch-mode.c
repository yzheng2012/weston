/*
 * Copyright © 2013 Hardening <rdp.effort at gmail.com>
 *
 * Permission to use, copy, modify, distribute, and sell this software and
 * its documentation for any purpose is hereby granted without fee, provided
 * that the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the name of the copyright holders not be used in
 * advertising or publicity pertaining to distribution of the software
 * without specific, written prior permission.  The copyright holders make
 * no representations about the suitability of this software for any
 * purpose.  It is provided "as is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS
 * SOFTWARE, INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND
 * FITNESS, IN NO EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER
 * RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF
 * CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <wayland-client.h>
#include "wlrandr-client-protocol.h"
#include "../shared/config-parser.h"
#include "../shared/os-compatibility.h"

#define ARRAY_LENGTH(a) (sizeof (a) / sizeof (a)[0])

static struct wl_list output_list;
static struct wlrandr *wlRandr;
int mode_switched;
uint32_t switch_result;

struct randr_mode {
	int width, height, refresh, flags;
	struct wl_list link;
};

struct randr_output {
	struct wl_output *output;
	int transform;
	char *make;
	char *model;
	struct wl_list modes;

	struct wl_list link;
};



static void
switch_mode_done(void *data, struct wlrandr *wlrander, uint32_t result)
{
	mode_switched = 1;
	switch_result = result;
}

static const struct wlrandr_listener wlrander_listener = {
		switch_mode_done
};

static void
display_handle_geometry(void *data,
			struct wl_output *wl_output,
			int x,
			int y,
			int physical_width,
			int physical_height,
			int subpixel,
			const char *make,
			const char *model,
			int transform)
{
	struct randr_output *output;
	output = wl_output_get_user_data(wl_output);
	output->make = strdup(make);
	output->model = strdup(model);
	output->transform = transform;
}

static void
display_handle_mode(void *data,
		    struct wl_output *wl_output,
		    uint32_t flags,
		    int width,
		    int height,
		    int refresh)
{
	struct randr_output *output;
	struct randr_mode *mode;

	output = wl_output_get_user_data(wl_output);
	mode = malloc(sizeof *mode);
	mode->width = width;
	mode->height = height;
	mode->refresh = refresh;
	mode->flags = flags;
	wl_list_insert(&output->modes, &mode->link);
}

static const struct wl_output_listener output_listener = {
	display_handle_geometry,
	display_handle_mode
};


static void
handle_global(void *data, struct wl_registry *registry,
	      uint32_t name, const char *interface, uint32_t version)
{
	static struct randr_output *output;

	if (strcmp(interface, "wl_output") == 0) {
		output = calloc(sizeof *output, 1);
		output->output = wl_registry_bind(registry, name, &wl_output_interface, 1);
		wl_list_init(&output->modes);
		wl_list_insert(&output_list, &output->link);
		wl_output_add_listener(output->output, &output_listener, output);
	} else if (strcmp(interface, "wlrandr") == 0) {
		wlRandr = wl_registry_bind(registry, name, &wlrandr_interface, 1);
	}
}

static void
handle_global_remove(void *data, struct wl_registry *registry, uint32_t name)
{
	/* XXX: unimplemented */
}

static const struct wl_registry_listener registry_listener = {
	handle_global,
	handle_global_remove
};

static int
parse_mode(const char *mode, int *w, int *h)
{
	/* parses a mode with the format <width>x<height> */
	const char *ptr;
	ptr = strchr(mode, 'x');
	if(!ptr)
		return -1;
	*w = strtoul(mode, NULL, 0);
	if(!*w || *w > 5000)
		return -1;

	ptr++;
	if(!*ptr)
		return -1;
	*h = strtoul(ptr, NULL, 0);
	if(!*h || *h > 5000)
		return -1;
	return 0;
}

static void
usage(char *argv[]) {
	fprintf(stderr, "usage: %s [options]\n", argv[0]);
	fprintf(stderr, "  where options are:\n");
	fprintf(stderr, "  --mode <width>x<height>\n");
	fprintf(stderr, "  --refresh <rate> or -r <rate> or --rate <rate>\n");
	fprintf(stderr, "  --output <output>\n");
}

static struct randr_output *
find_output(const char *name) {
	struct randr_output *ret;

	wl_list_for_each(ret, &output_list, link) {
		if(strcmp(ret->model, name) == 0)
			return ret;
	}
	return 0;
}

static void
print_available_modes(struct randr_output *output) {
	struct randr_mode *mode;
	fprintf(stderr, "%s %s:\n", output->make, output->model);
	wl_list_for_each(mode, &output->modes, link) {
		fprintf(stderr, "  %s%dx%d\t%d\n", (mode->flags & WL_OUTPUT_MODE_CURRENT) ? "*" : "",
				mode->width, mode->height, mode->refresh);
	}
}


int main(int argc, char *argv[])
{
	struct wl_display *display;
	struct wl_registry *registry;
	struct randr_output *output;
	struct randr_mode *mode;
	int mode_found;

	char *mode_string= NULL;
	char *output_name = NULL;
	int width, height, refresh = -1;

	const struct weston_option options[] = {
		{ WESTON_OPTION_STRING,  "mode", 0, &mode_string },
		{ WESTON_OPTION_STRING,  "output", 0, &output_name },
		{ WESTON_OPTION_UNSIGNED_INTEGER,  "rate", 0, &refresh },
		{ WESTON_OPTION_UNSIGNED_INTEGER,  "refresh", 0, &refresh }
	};

	if(parse_options(options, ARRAY_LENGTH(options), &argc, argv) < 0) {
		usage(argv);
		exit(EXIT_FAILURE);
	}

	display = wl_display_connect(NULL);
	if (display == NULL) {
		fprintf(stderr, "failed to create display: %m\n");
		exit(EXIT_FAILURE);
	}

	wl_list_init(&output_list);
	registry = wl_display_get_registry(display);
	wl_registry_add_listener(registry, &registry_listener, NULL);
	wl_display_dispatch(display);
	wl_display_roundtrip(display);
	if (wlRandr == NULL) {
		fprintf(stderr, "display doesn't support wlRandr\n");
		return -1;
	}

	wlrandr_add_listener(wlRandr, &wlrander_listener, wlRandr);

	if(output_name) {
		output = find_output(output_name);
		if(!output) {
			fprintf(stderr, "output %s not found\n", output_name);
			exit(EXIT_FAILURE);
		}
	} else {
		if(wl_list_length(&output_list) != 1) {
			fprintf(stderr, "multiple output detected, you should specify the "
					"target output with --output <output>\n");
			exit(EXIT_FAILURE);
		}

		output = wl_container_of(output_list.next, output, link);
	}

	if(!mode_string) {
		usage(argv);
		print_available_modes(output);
		exit(EXIT_SUCCESS);
	}

	if(parse_mode(mode_string, &width, &height) < 0) {
		fprintf(stderr, "invalid mode %s", mode_string);
		usage(argv);
		exit(EXIT_FAILURE);
	}

	mode_found = 0;
	wl_list_for_each(mode, &output->modes, link) {
		if(mode->width == width && mode->height == height) {
			if(refresh < 0 || mode->refresh == refresh) {
				mode_found = 1;
				break;
			}
		}
	}

	if(!mode_found) {
		fprintf(stderr, "mode %dx%d not available\n", width, height);
		exit(EXIT_FAILURE);
	}

	if(mode->flags & WL_OUTPUT_MODE_CURRENT) {
		fprintf(stderr, "mode %dx%d is already the current mode\n", width, height);
		exit(0);
	}

	if(refresh < 0)
		refresh = 0;
	wlrandr_switch_mode(wlRandr, output->output, width, height, refresh);
	mode_switched = 0;
	while (!mode_switched)
		wl_display_roundtrip(display);

	switch(switch_result) {
	case WLRANDR_SWITCH_MODE_RESULT_FAILED:
		fprintf(stderr, "something failed in weston during mode switch\n");
		break;
	case WLRANDR_SWITCH_MODE_RESULT_NOT_AVAILABLE:
		fprintf(stderr, "mode not available in weston\n");
		break;
	default:
		printf("successfully switch to mode %dx%d-%d\n", width, height, refresh);
		break;
	}
	return 0;
}

