#include "displayconfig.h"

int main(int argc, char *argv[])
{
	HdmiInfos_t hdmi_infos;

	memset((void*)&hdmi_infos, 0, sizeof(HdmiInfos_t));
	init_display_config();
	get_display_hdmi_info(&hdmi_infos);
	for(int i = 0; i < hdmi_infos.count; i++) {
		printf("hdmi_infos,w=%d,h=%d, refresh=%d, flag=0x%x,numHdmiMode=%d\n",
		       hdmi_infos.hdmi_info[i].xres, hdmi_infos.hdmi_info[i].yres,
		       hdmi_infos.hdmi_info[i].refresh, hdmi_infos.hdmi_info[i].interlaced,
		       hdmi_infos.count);
	}
	set_hdmi_mode(1280, 720, 50000, 0, 0);
	deInit_display_config();
	printf("hdmi_infos count=%d\n", hdmi_infos.count);

	return 0;
}




