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

#include <xf86drm.h>
#include <xf86drmMode.h>

#include "wlrandr-client-protocol.h"
#include "../shared/config-parser.h"
#include "../shared/os-compatibility.h"

#define ARRAY_LENGTH(a) (sizeof (a) / sizeof (a)[0])

static struct wl_list output_list;
static struct wlrandr *wlRandr;

int mode_switched;
uint32_t mode_list_getd;
uint32_t current_mode_getd;
uint32_t output_name_getd;
uint32_t switch_result;
uint32_t switch_result;

struct randr_mode {
	int width, height, refresh;
	uint32_t flags;
	struct wl_list link;
};

struct randr_output {
	struct wl_output *output;
	int transform;
	uint32_t id;
	char *output_name;
	char *make;
	char *model;
	struct randr_mode current_mode;
	struct wl_list modes;

	struct wl_list link;
};

static void
result_done(void *data, struct wlrandr *wlrander, uint32_t type, uint32_t result)
{
	if (type == WLRANDR_EVENT_LIST_EVENT_SWITCH_MODE)
		mode_switched = 1;
	else if (type == WLRANDR_EVENT_LIST_EVENT_GET_MODE_LIST)
		mode_list_getd = 1;
	else if (type == WLRANDR_EVENT_LIST_EVENT_GET_CUR_MODE)
		current_mode_getd = 1;
	switch_result = result;
}

static void
handle_output_device(void *data,
		     struct wlrandr *wlrandr,
		     const char *name,
		     const char* make,
		     const char* model)
{
	struct randr_output *output = data;

	output->output_name = strdup(name);
	output->make = strdup(make);
	output->model = strdup(model);
	output_name_getd = 1;
 }

static void
handle_mode(void *data,
	    struct wlrandr *wlrandr,
	    int32_t width,
	    int32_t height,
	    int32_t refresh,
	    uint32_t flags)
{
	struct randr_mode *mode;
	struct randr_output *output = data;

	mode = malloc(sizeof *mode);
	mode->width = width;
	mode->height = height;
	mode->refresh = refresh;
	mode->flags = flags;
	wl_list_insert(&output->modes, &mode->link);
}

static void
handle_current_mode(void *data,
		    struct wlrandr *wlrandr,
		    int32_t width,
		    int32_t height,
		    int32_t refresh,
		    uint32_t flags)
{
	struct randr_output *output = data;

	output->current_mode.width = width;
	output->current_mode.height = height;
	output->current_mode.refresh = refresh;
	output->current_mode.flags = flags;
}

static struct wlrandr_listener wlrander_listener = {
	result_done,
	handle_output_device,
	handle_mode,
	handle_current_mode,
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
	fprintf(stderr, "  --interlaced <interlaced>\n");
}

static struct randr_output *
find_output(const char *name) {
	struct randr_output *ret;

	wl_list_for_each(ret, &output_list, link) {
		if(strcmp(ret->output_name, name) == 0)
			return ret;
	}
	return 0;
}

static void
print_available_modes(struct randr_output *output) {
	struct randr_mode *mode;

	fprintf(stderr, "dev:%s  make:%s %s:\n", output->output_name, output->make, output->model);
	wl_list_for_each(mode, &output->modes, link) {
		fprintf(stderr, "  %dx%d%c\t%d\n",
			mode->width, mode->height,
			mode->flags & DRM_MODE_FLAG_INTERLACE ? 'i' : 'p',
			mode->refresh);
	}
	fprintf(stderr, "curmode:  %dx%d\t%d\n",
		output->current_mode.width,
		output->current_mode.height,
		output->current_mode.refresh);
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
	int width, height, refresh = -1, interlaced;

	const struct weston_option options[] = {
		{ WESTON_OPTION_STRING,  "mode", 0, &mode_string },
		{ WESTON_OPTION_STRING,  "output", 0, &output_name },
		{ WESTON_OPTION_UNSIGNED_INTEGER,  "rate", 0, &refresh },
		{ WESTON_OPTION_UNSIGNED_INTEGER,  "refresh", 0, &refresh },
		{ WESTON_OPTION_UNSIGNED_INTEGER,  "interlaced", 0, &interlaced }
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

	wl_list_for_each(output, &output_list, link)
		wlrandr_add_listener(wlRandr, &wlrander_listener, output);

	wl_list_for_each(output, &output_list, link) {
		output_name_getd = 0;
		wlrandr_get_output_name(wlRandr, output->output);
		while (!output_name_getd)
			wl_display_roundtrip(display);

		mode_list_getd = 0;
		wlrandr_get_output_mode_list(wlRandr, output->output);
		while (!mode_list_getd)
			wl_display_roundtrip(display);

		current_mode_getd = 0;
		wlrandr_get_current_mode(wlRandr, output->output);
		while (!current_mode_getd)
			wl_display_roundtrip(display);
	}

	if(output_name) {
		output = find_output(output_name);
		if(!output) {
			fprintf(stderr, "output %s not found\n", output_name);
			exit(EXIT_FAILURE);
		}
	} else {
		if(wl_list_length(&output_list) != 1) {
			printf("output:\n");
			wl_list_for_each(output, &output_list, link)
				printf("  %s\n", output->output_name);
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

	if(refresh < 0)
		refresh = 0;

	if (interlaced > 0)
		interlaced = DRM_MODE_FLAG_INTERLACE;
	else
		interlaced = 0;

	wlrandr_switch_mode(wlRandr, output->output, width, height, refresh, interlaced);
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
		printf("successfully switch to mode %dx%d%c-%d\n",
		       width, height, interlaced ? 'i' : 'p', refresh);
		break;
	}
	return 0;
}

