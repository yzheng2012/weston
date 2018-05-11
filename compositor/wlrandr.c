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
#include <errno.h>

#include "compositor.h"
#include "compositor-drm.h"
#include "weston.h"
#include "wlrandr-server-protocol.h"
#include "shared/helpers.h"

struct wlrandr_impl {
	struct weston_compositor *ec;
	struct wl_global *global;
	struct wl_listener destroy_listener;
};

static int
wlrandr_set_mode(struct weston_output *output, char *modeline)
{
	const struct weston_drm_output_api *api =
		weston_drm_output_get_api(output->compositor);
	int ret;

	if (output->current_mode)
		output->current_mode->flags &= ~WL_OUTPUT_MODE_CURRENT;

	ret = api->set_mode(output, WESTON_DRM_BACKEND_OUTPUT_PREFERRED, modeline);
	if (ret < 0)
		return ret;
	weston_output_disable(output);
	output->width = 0;
	output->height = 0;
	ret = weston_output_enable(output);
	return ret;
}

static void
wlrandr_switch_mode(struct wl_client *client,
		    struct wl_resource *resource,
		    struct wl_resource *output_resource,
		    int32_t width, int32_t height, int32_t refresh) {
	struct weston_output *output =
		weston_output_from_resource(output_resource);
	char modeline[16];

	weston_log("switching to %dx%d@%d\n", width, height, refresh);
	if (width == 0 || height == 0)
		sprintf(modeline, "preferred");
	else if (refresh)
		sprintf(modeline, "%dx%d@%d", width, height, refresh);
	else
		sprintf(modeline, "%dx%d", width, height);

	int ret = wlrandr_set_mode(output, modeline);
	if(ret < 0) {
		switch(ret) {
		case -ENOENT:
			wlrandr_send_done(resource, WLRANDR_SWITCH_MODE_RESULT_NOT_AVAILABLE);
			break;
		default:
			/* generic error */
			wlrandr_send_done(resource, WLRANDR_SWITCH_MODE_RESULT_FAILED);
			break;
		}
	} else {
		wlrandr_send_done(resource, WLRANDR_SWITCH_MODE_RESULT_OK);
	}
}

struct wlrandr_interface wlrandr_implementation = {
	wlrandr_switch_mode
};

static void
bind_wlrandr(struct wl_client *client,
	     void *data, uint32_t version, uint32_t id)
{
	struct wl_resource *resource;

	resource = wl_resource_create(client, &wlrandr_interface, 1, id);

	if (resource == NULL) {
                wl_client_post_no_memory(client);
                return;
	}

	wl_resource_set_implementation(resource, &wlrandr_implementation,
				       data, NULL);
}

static void
wlrandr_destroy(struct wl_listener *listener, void *data)
{
	struct wlrandr_impl *randr =
		container_of(listener, struct wlrandr_impl, destroy_listener);

	wl_global_destroy(randr->global);
	free(randr);
}

WL_EXPORT void
wlrandr_create(struct weston_compositor *ec)
{
	struct wlrandr_impl *randr;

	randr = malloc(sizeof *randr);
	if (randr == NULL)
		return;

	randr->ec = ec;
	randr->global = wl_global_create(ec->wl_display,
					 &wlrandr_interface, 1,
					 randr, bind_wlrandr);
	randr->destroy_listener.notify = wlrandr_destroy;
	wl_signal_add(&ec->destroy_signal, &randr->destroy_listener);
}
