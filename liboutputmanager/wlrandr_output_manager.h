#ifndef WLRANDR_DISPLAYMANAGER_H
#define WLRANDR_DISPLAYMANAGER_H

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

struct wlrandr_output_mode{
	int width, height, refresh;
	uint32_t flags;
};
struct wlrandr_output_list{
	char* name;
	char* make;
	char* model;
	int id;
};
enum {
	WLRANDR_RESULT_SUCCESS=0,
	WLRANDR_RESULT_NO_OUTPUT=1,
	WLRANDR_RESULT_INPUT_ERROR,
	WLRANDR_RESULT_NO_DISPLAY,
	WLRANDR_RESULT_TIMEOUT,
};
#ifdef __cplusplus
extern "C"
{
#endif

/**
 * init wlrandr
 */
int wlrandr_init(void);

/**
 * deinit wlrandr, free resources
 */
int wlrandr_deinit(void);

/**
 * Update the newest message from server
 */
void wlrandr_update(void);

/**
 * Get wlrandr_output_list of the wl_output
 *
 * Given wlrandr_output_list return. The returnd value is an array.
 * num_devices is the array size.
 * wlrandr_output_list: name is drm connector type name, it is used to look up wl_output.
 *
 * If no display devices connectd, return NULL.
 *
 * @param num_devices the array size of wlrandr_output_list
 */
struct wlrandr_output_list* wlrandr_get_output_devices(int* num_devices);

/**
 * Get the mode list of wl_output
 *
 * Given wlrandr_output_mode return. If output_name not found, NULL will be returned.
 * the returnd value is an array, numofmodes is the array size. Who master the retured array,
 * must free it when no longer use.
 *
 * @param output_name drm connector type name that used to look up wl_output
 * @param numofmodes be assigned to the array sizes of the wlrandr_output_mode
 */
struct wlrandr_output_mode* wlrandr_get_mode_list(char* output_name, int* numofmodes);

/**
 * Get the current mode of wl_output
 *
 * Given result return. If output_name not found, outputmode will be NULL.
 * If the returnd value is 0 then func is successful.
 *
 * @param output_name drm connector type name that used to look up wl_output
 * @param outputmode the current mode
 */
int wlrandr_get_curmode(char* output_name, struct wlrandr_output_mode *outputmode);

/**
 * Change mode of wl_output
 *
 * Given result return. outputmode must not be NULL.
 * If the returnd value is 0 then func is successful.
 *
 * @param output_name drm connector type name that used to look up wl_output
 * @param outputmode the mode you want to set
 */
int wlrandr_set_mode(char* output_name, struct wlrandr_output_mode *outputmode);

#ifdef __cplusplus
}
#endif

#endif

