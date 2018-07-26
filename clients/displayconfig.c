#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <wayland-client.h>

#include <xf86drm.h>
#include <xf86drmMode.h>
#include "displayconfig.h"
#include "wlrandr-client-protocol.h"
#include "../shared/config-parser.h"
#include "../shared/os-compatibility.h"
#include "../liboutputmanager/wlrandr_output_manager.h"

#define ARRAY_LENGTH(a) (sizeof (a) / sizeof (a)[0])

static struct wlrandr_output_list *find_output = NULL;

WL_EXPORT int init_display_config(void)
{
	wlrandr_init();
}

WL_EXPORT int get_display_hdmi_info(HdmiInfos_t *pHdmiInfo)
{
	int num_devices = 0;
	int mode_count = 0;
	int i = 0;
	struct wlrandr_output_mode *mode_list = NULL;
	struct wlrandr_output_list *output_list =
			wlrandr_get_output_devices(&num_devices);

	for (int j = 0; j < num_devices; j++) {
		if (num_devices == 1)
			find_output = &output_list[j];

		if (num_devices == 2) {
			if (strstr(output_list[j].name, "HDMI") > 0) {
				find_output = &output_list[j];
			}
		}
	}

	if (find_output == NULL)
		return -1;

	mode_list = wlrandr_get_mode_list(find_output->name, &mode_count);
	if (mode_count > 0) {
		for (int j=0; j<mode_count; j++) {
			pHdmiInfo->hdmi_info[j].xres = mode_list[j].width;
			pHdmiInfo->hdmi_info[j].yres = mode_list[j].height;
			pHdmiInfo->hdmi_info[j].refresh = mode_list[j].refresh;
			pHdmiInfo->hdmi_info[j].interlaced = mode_list[j].flags;
			printf("get mode:w=%d,h=%d,refresh=%d,interlaced=%d\n",
			       pHdmiInfo->hdmi_info[j].xres,
			       pHdmiInfo->hdmi_info[j].yres,
			       pHdmiInfo->hdmi_info[j].refresh,
			       pHdmiInfo->hdmi_info[j].interlaced);
		}
	}

	pHdmiInfo->count = mode_count;
	free(mode_list);
	mode_list = NULL;

	return i;
}

WL_EXPORT int
set_hdmi_mode(int width, int height, int refresh, int interlaced, int reserved)
{
	struct wlrandr_output_mode output_mode;

	if (find_output == NULL)
		return -1;

	output_mode.width = width;
	output_mode.height = height;
	output_mode.refresh = refresh;
	output_mode.flags = interlaced;
	wlrandr_set_mode(find_output->name, &output_mode);
}

WL_EXPORT int
deInit_display_config(void)
{
	wlrandr_deinit();
}

