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
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

#include <xf86drm.h>
#include <xf86drmMode.h>

#include "baseparameter.h"
#include "compositor.h"
#include "compositor-drm.h"
#include "shared/helpers.h"
#include "weston.h"
#include "wlrandr-server-protocol.h"
#include "../../libhdmiset-master/drm/hdmiset.h"

struct wlrandr_impl {
	struct weston_compositor *ec;
	struct wl_global *global;
	struct wl_listener destroy_listener;
};

struct drm_mode {
	struct weston_mode base;
	drmModeModeInfo mode_info;
	uint32_t blob_id;
};

static char const *const device_template[] =
{
	"/dev/block/platform/1021c000.dwmmc/by-name/baseparameter",
	"/dev/block/platform/30020000.dwmmc/by-name/baseparameter",
	"/dev/block/platform/fe330000.sdhci/by-name/baseparameter",
	"/dev/block/platform/ff520000.dwmmc/by-name/baseparameter",
	"/dev/block/platform/ff0f0000.dwmmc/by-name/baseparameter",
	"/dev/block/rknand_baseparameter",
	NULL
};

static const char*
get_baseparam_file(void)
{
	int i = 0;

	while (device_template[i]) {
		if (!access(device_template[i], R_OK | W_OK))
			return device_template[i];
		i++;
	}
	return NULL;
}

static struct screen_info*
find_suitable_info_slot(int dpy, struct file_base_paramer* base_param, int type)
{
	struct disp_info* info;
	int found = 0;

	if (dpy == 0)
		info = &base_param->main;
	else
		info = &base_param->aux;

	for (int i = 0; i < 5; i++) {
		if (info->screen_list[i].type != 0 &&
		    info->screen_list[i].type == type) {
			found = i;
			break;
		} else if (info->screen_list[i].type !=0 && found == false) {
			found++;
		}
	}

	if (found == -1)
		found = 0;

	weston_log("findSuitableInfoSlot: %d type=%d", found, type);

	return &info->screen_list[found];
}

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
		    int32_t width, int32_t height, int32_t refresh,
		    uint32_t flags) {
	struct weston_output *output =
		weston_output_from_resource(output_resource);
	char envname[32];
	char modeline[16];

	weston_log("switching to %dx%d@%d flags 0x%x\n",
		   width, height, refresh, flags);
	sprintf(envname, "%s-MODE", output->name);
	if (width == 0 || height == 0)
		sprintf(modeline, "preferred");
	else if (refresh)
		sprintf(modeline, "%dx%d@%d-%d", width, height, refresh, flags);
	else
		sprintf(modeline, "%dx%d", width, height);

	int ret = wlrandr_set_mode(output, modeline);
	if(ret < 0) {
		switch(ret) {
		case -ENOENT:
			wlrandr_send_done(resource,
					  WLRANDR_EVENT_LIST_EVENT_SWITCH_MODE,
					  WLRANDR_SWITCH_MODE_RESULT_NOT_AVAILABLE);
			break;
		default:
			/* generic error */
			wlrandr_send_done(resource,
					  WLRANDR_EVENT_LIST_EVENT_SWITCH_MODE,
					  WLRANDR_SWITCH_MODE_RESULT_FAILED);
			break;
		}
	} else {
		setenv(envname, modeline, 1);
		wlrandr_send_done(resource,
				  WLRANDR_EVENT_LIST_EVENT_SWITCH_MODE,
				  WLRANDR_SWITCH_MODE_RESULT_OK);
	}
}

static void
wlrandr_get_output_name(struct wl_client *client,
			struct wl_resource *resource,
			struct wl_resource *output)
{
	struct weston_output *w_output =
		weston_output_from_resource(output);

	if (w_output)
		wlrandr_send_output_name(resource, w_output->name,
					 w_output->make, w_output->model);
	else
		wlrandr_send_output_name(resource, NULL, NULL, NULL);
}

static void
wlrandr_get_mode_list(struct wl_client *client,
		      struct wl_resource *resource,
		      struct wl_resource *output)
{
	struct weston_output *w_output =
		weston_output_from_resource(output);
	struct weston_mode *mode;
	struct drm_mode *drmmode = NULL;

	wl_list_for_each(mode, &w_output->mode_list, link) {
		drmmode = container_of(mode, struct drm_mode, base);
		if (drmmode == NULL)
			return;

		bool r = check_mode((void*)&drmmode->mode_info);

		if (r == false && !strcmp(w_output->name, "HDMI-A-1"))
			continue;

		wlrandr_send_mode(resource,
				  mode->width,
				  mode->height,
				  mode->refresh,
				  mode->drm_flags);
	}

	wlrandr_send_done(resource, WLRANDR_EVENT_LIST_EVENT_GET_MODE_LIST, 0);
}

static void
wlrandr_get_current_mode(struct wl_client *client,
			 struct wl_resource *resource,
			 struct wl_resource *output)
{
	struct weston_output *w_output =
		weston_output_from_resource(output);
	struct weston_mode *mode;

	wl_list_for_each(mode, &w_output->mode_list, link) {
		if (mode->flags & WL_OUTPUT_MODE_CURRENT)
			wlrandr_send_current_mode(resource,
						  mode->width,
						  mode->height,
						  mode->refresh,
						  mode->drm_flags);
	}

	wlrandr_send_done(resource, WLRANDR_EVENT_LIST_EVENT_GET_CUR_MODE, 0);
}

static void
wlrandr_save_config(struct wl_client *client,
                   struct wl_resource *resource,
                   struct wl_resource *output,
                   uint32_t dpy)
{
	struct weston_output *w_output = weston_output_from_resource(output);
	struct file_base_paramer base_paramer;
	const char *baseparameterfile = get_baseparam_file();
	struct drm_mode *mode = NULL;
	uint32_t file = 0, length = 0;

	if (!baseparameterfile) {
		sync();
		return;
	}

	file = open(baseparameterfile, O_RDWR);
		if (file <= 0) {
			sync();
		return;
	}

	length = lseek(file, 0L, SEEK_END);
	lseek(file, 0L, SEEK_SET);
	if(length < sizeof(base_paramer)) {
		sync();
		close(file);
		return;
	}

	read(file, (void*)&(base_paramer.main), sizeof(base_paramer.main));
	lseek(file, BASE_OFFSET, SEEK_SET);
	read(file, (void*)&(base_paramer.aux), sizeof(base_paramer.aux));

	mode = container_of(w_output->current_mode, struct drm_mode, base);
	if (mode == NULL) {
		close(file);
		return;
	}

	struct screen_info* mslot =
		find_suitable_info_slot(dpy, &base_paramer,
					mode->mode_info.type);

	mslot->type = mode->mode_info.type;
	mslot->mode.hdisplay = mode->mode_info.hdisplay;
	mslot->mode.vdisplay= mode->mode_info.vdisplay;
	mslot->mode.hsync_start = mode->mode_info.hsync_start;
	mslot->mode.hsync_end = mode->mode_info.hsync_end;
	mslot->mode.vsync_start = mode->mode_info.vsync_start;
	mslot->mode.vsync_end = mode->mode_info.vsync_end;
	mslot->mode.clock = mode->mode_info.clock;
	mslot->mode.vtotal = mode->mode_info.vtotal;
	mslot->mode.htotal= mode->mode_info.htotal;
	mslot->mode.vscan = mode->mode_info.vscan;
	mslot->mode.flags = mode->mode_info.flags;

	if (dpy == 0) {
		lseek(file, 0L, SEEK_SET);
		write(file, (char*)(&base_paramer.main), sizeof(base_paramer.main));
	} else {
		lseek(file, BASE_OFFSET, SEEK_SET);
		write(file, (char*)(&base_paramer.aux), sizeof(base_paramer.aux));
	}

	close(file);
	sync();
	wlrandr_send_done(resource, WLRANDR_EVENT_LIST_EVENT_SAVE_CONFIG, 0);
}



struct wlrandr_interface wlrandr_implementation = {
	wlrandr_switch_mode,
	wlrandr_get_output_name,
	wlrandr_get_mode_list,
	wlrandr_get_current_mode,
	wlrandr_save_config
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
	parse_white_mode();
}
