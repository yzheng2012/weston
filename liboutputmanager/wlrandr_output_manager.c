#include <stdbool.h>
#include <pthread.h>
#include <unistd.h>
#include <wayland-client.h>
#include "../protocol/wlrandr-client-protocol.h"
#include "../shared/config-parser.h"
#include "../shared/os-compatibility.h"
#include "wlrandr_output_manager.h"

struct randr_mode {
	int width, height, refresh;
	uint32_t flags;
	struct wl_list link;
};

struct event_result {
	uint32_t mode_switched;
	uint32_t mode_get_done;
	uint32_t mode_current_done;
	uint32_t output_name_done;
	uint32_t switch_result;
	uint32_t save_config_done;
};

struct randr_output {
	struct wl_output *output;
	int transform;
	uint32_t id;
	char *output_name;
	char *make;
	char *model;
	struct randr_mode current_mode;
	struct wl_list wl_modes;
	struct wl_list link;
};

struct wl_output_info {
	struct wl_display *display;
	struct wl_registry *registry;
	struct wlrandr *wlrandr;
	struct wl_list output_list;
};

#define EVENT_TIMEOUT_CNT 100

static struct wl_output_info m_output_info;
static struct event_result event_ret;
static pthread_mutex_t lock_;

static void
result_done(void *data, struct wlrandr *wlrander, uint32_t type, uint32_t result)
{
	if (type == WLRANDR_EVENT_LIST_EVENT_SWITCH_MODE)
		event_ret.mode_switched = 1;
	else if (type == WLRANDR_EVENT_LIST_EVENT_GET_MODE_LIST)
		event_ret.mode_get_done = 1;
	else if (type == WLRANDR_EVENT_LIST_EVENT_GET_CUR_MODE)
		event_ret.mode_current_done = 1;
	else if (type == WLRANDR_EVENT_LIST_EVENT_SAVE_CONFIG)
		event_ret.save_config_done = 1;
	event_ret.switch_result = result;
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
	event_ret.output_name_done = 1;
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
	wl_list_insert(&output->wl_modes, &mode->link);
}

static void
handle_current_mode(void *data,
		    struct wlrandr *wlrandr,
		    int32_t width,
		    int32_t height,
		    int32_t refresh)
{
	struct randr_output *output = data;

	output->current_mode.width = width;
	output->current_mode.height = height;
	output->current_mode.refresh = refresh;
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

	if (strcmp(interface, "wl_output") == 0) {
		struct randr_output *moutput;

		moutput = calloc(sizeof *moutput, 1);
		moutput->output = wl_registry_bind(registry, name, &wl_output_interface, 1);
		moutput->id = name;
		wl_list_init(&moutput->wl_modes);
		wl_list_insert(&m_output_info.output_list, &moutput->link);
	} else if (strcmp(interface, "wlrandr") == 0) {
		m_output_info.wlrandr = wl_registry_bind(registry, name, &wlrandr_interface, 1);
	}

}

static void
handle_global_remove(void *data, struct wl_registry *registry, uint32_t name)
{
	struct randr_mode *mode, *next_mode;
	struct randr_output *output, *next;

	pthread_mutex_lock(&lock_);

	wl_list_for_each_safe(output, next, &m_output_info.output_list, link) {
		if (output->id != name)
			continue;
		if (output->output_name)
			free(output->output_name);
		if (output->make)
			free(output->make);
		if (output->model)
			free(output->model);
		wl_list_for_each_safe(mode, next_mode, &output->wl_modes, link) {
			wl_list_remove(&mode->link);
			free(mode);
		}
		wl_output_destroy(output->output);
		wl_list_remove(&output->link);
		free(output);
	}

	pthread_mutex_unlock(&lock_);
}

static struct wl_registry_listener registry_listener = {
	handle_global,
	handle_global_remove
};

static int
wlrandr_wait_result_timeout(int type)
{
	int timeout = 0, ret = 0;
	uint32_t* result = NULL;

	if (type == WLRANDR_EVENT_LIST_EVENT_SWITCH_MODE)
		result = &event_ret.mode_switched;
	else if (type == WLRANDR_EVENT_LIST_EVENT_GET_MODE_LIST)
		result = &event_ret.mode_get_done;
	else if (type == WLRANDR_EVENT_LIST_EVENT_GET_OUTPUT_NAME)
		result = &event_ret.output_name_done;
	else if (type == WLRANDR_EVENT_LIST_EVENT_GET_CUR_MODE)
		result = &event_ret.mode_current_done;
	else if (type == WLRANDR_EVENT_LIST_EVENT_SAVE_CONFIG)
		result = &event_ret.save_config_done;
	while (!(*result) && timeout <= EVENT_TIMEOUT_CNT) {
		wl_display_roundtrip(m_output_info.display);
		timeout++;
		usleep(10*10000);
	}

	if (timeout <= EVENT_TIMEOUT_CNT)
		ret = WLRANDR_RESULT_SUCCESS;
	else
		ret = -WLRANDR_RESULT_TIMEOUT;

	return ret;
}

WL_EXPORT struct wlrandr_output_mode*
wlrandr_get_mode_list(char* output_name, int* numofmodes)
{
	struct randr_output *output = NULL;
	struct randr_mode *mode = NULL, *next_mode = NULL;
	struct wlrandr_output_mode *outputmodes = NULL;
	int num_modes = 0, i = 0;
	bool found = false;

	pthread_mutex_lock(&lock_);

	wl_list_for_each(output, &m_output_info.output_list, link) {
		if (output->output_name && !strcmp(output_name, output->output_name)) {
			found = true;
			break;
		}
	}

	if (found == false) {
		*numofmodes = 0;
		return NULL;
	}

	wl_list_for_each_safe(mode, next_mode, &output->wl_modes, link) {
		wl_list_remove(&mode->link);
		free(mode);
	}

	event_ret.mode_get_done = 0;
	wlrandr_get_output_mode_list(m_output_info.wlrandr, output->output);
	wlrandr_wait_result_timeout(WLRANDR_EVENT_LIST_EVENT_GET_MODE_LIST);

	num_modes = wl_list_length(&output->wl_modes);
	if (num_modes > 0) {
		outputmodes = malloc(num_modes * sizeof(struct wlrandr_output_mode));
		wl_list_for_each(mode, &output->wl_modes, link) {
			outputmodes[i].width = mode->width;
			outputmodes[i].height = mode->height;
			outputmodes[i].refresh = mode->refresh;
			outputmodes[i].flags = mode->flags;
			i++;
		}
	}

	pthread_mutex_unlock(&lock_);
	*numofmodes = num_modes;
	return outputmodes;
}

WL_EXPORT int
wlrandr_get_curmode(char* output_name, struct wlrandr_output_mode *outputmode)
{
	struct randr_output *output;
	int ret = 0;

	if (!output_name || !outputmode)
		return -WLRANDR_RESULT_INPUT_ERROR;

	pthread_mutex_lock(&lock_);

	if (wl_list_length(&m_output_info.output_list) <= 0) {
		pthread_mutex_unlock(&lock_);
		return -WLRANDR_RESULT_NO_OUTPUT;
	}

	wl_list_for_each(output, &m_output_info.output_list, link) {
		if (output->output_name && !strcmp(output_name, output->output_name))
			break;
	}

	event_ret.mode_current_done = 0;
	wlrandr_get_current_mode(m_output_info.wlrandr, output->output);
	ret = wlrandr_wait_result_timeout(WLRANDR_EVENT_LIST_EVENT_GET_CUR_MODE);
	if (ret < 0) {
		pthread_mutex_unlock(&lock_);
		return ret;
	}

	outputmode->width = output->current_mode.width;
	outputmode->height = output->current_mode.height;
	outputmode->refresh = output->current_mode.refresh;
	outputmode->flags = output->current_mode.flags;

	pthread_mutex_unlock(&lock_);

	return WLRANDR_RESULT_SUCCESS;
}

WL_EXPORT int
wlrandr_set_mode(char* output_name, struct wlrandr_output_mode *outputmode)
{
	struct randr_output *output = NULL;
	int num_devices = 0;
	int ret;

	if (!output_name || !outputmode)
		return -WLRANDR_RESULT_INPUT_ERROR;


	if (wl_list_length(&m_output_info.output_list) <= 0) {
		return -WLRANDR_RESULT_NO_OUTPUT;
	}

	wl_list_for_each(output, &m_output_info.output_list, link) {
		if (output->output_name && !strcmp(output_name, output->output_name))
			break;
	}
	if (output == NULL) {
		return -1;
	}
	event_ret.mode_switched = 0;
	wlrandr_switch_mode(m_output_info.wlrandr, output->output,
			    outputmode->width, outputmode->height,
			    outputmode->refresh, outputmode->flags);
	ret = wlrandr_wait_result_timeout(WLRANDR_EVENT_LIST_EVENT_SWITCH_MODE);
    wlrandr_save_config(m_output_info.wlrandr, output->output, 0);
	ret = wlrandr_wait_result_timeout(WLRANDR_EVENT_LIST_EVENT_SAVE_CONFIG);
	int num_devices = 0;
    wlrandr_get_output_devices(&num_devices);
	if (ret < 0)
		return ret;

	return WLRANDR_RESULT_SUCCESS;
}

WL_EXPORT struct wlrandr_output_list*
wlrandr_get_output_devices(int* num_devices)
{
	struct randr_output *output;
	struct wlrandr_output_list* outputlist = NULL;
	int i = 0, len = 0, ret = 0;

	pthread_mutex_lock(&lock_);
	len = wl_list_length(&m_output_info.output_list);
	pthread_mutex_unlock(&lock_);
	if (len <= 0) {
		*num_devices = 0;
		return NULL;
	}

	outputlist = (struct wlrandr_output_list*) malloc(len * sizeof(struct wlrandr_output_list));
	wl_list_for_each(output, &m_output_info.output_list, link) {
		event_ret.output_name_done = 0;
		wlrandr_get_output_name(m_output_info.wlrandr, output->output);
		ret = wlrandr_wait_result_timeout(WLRANDR_EVENT_LIST_EVENT_GET_OUTPUT_NAME);
		if (ret < 0) {
			pthread_mutex_unlock(&lock_);
			return NULL;
		}
		outputlist[i].name = strdup(output->output_name);
		outputlist[i].make = strdup(output->make);
		outputlist[i].model =  strdup(output->model);
		outputlist[i].id = i;
		i++;
	}
	pthread_mutex_unlock(&lock_);
	*num_devices = len;
	return outputlist;
}

WL_EXPORT void
wlrandr_update(void)
{
	if (m_output_info.display)
		wl_display_roundtrip(m_output_info.display);
}

WL_EXPORT int
wlrandr_init(void)
{
	struct randr_output *output;

	m_output_info.display = wl_display_connect(NULL);
	if (m_output_info.display == NULL)
		return -WLRANDR_RESULT_NO_DISPLAY;

	wl_list_init(&m_output_info.output_list);
	m_output_info.registry = wl_display_get_registry(m_output_info.display);

	wl_registry_add_listener(m_output_info.registry, &registry_listener, NULL);
	wl_display_dispatch(m_output_info.display);
	wl_display_roundtrip(m_output_info.display);

	if (m_output_info.wlrandr == NULL) {
		wl_display_disconnect(m_output_info.display);
		return -WLRANDR_RESULT_NO_OUTPUT;
	}

	wl_list_for_each(output, &m_output_info.output_list, link)
		wlrandr_add_listener(m_output_info.wlrandr, &wlrander_listener, output);

	pthread_mutex_init(&lock_, NULL);

	return WLRANDR_RESULT_SUCCESS;
}

WL_EXPORT int
wlrandr_deinit(void)
{
	struct randr_mode *mode, *next_mode;
	struct randr_output *output, *next;

	wl_display_roundtrip(m_output_info.display);
	wl_list_for_each_safe(output, next, &m_output_info.output_list, link) {
		if (output->output_name)
			free(output->output_name);
		if (output->make)
			free(output->make);
		if (output->model)
			free(output->model);
		wl_list_for_each_safe(mode, next_mode, &output->wl_modes, link) {
			wl_list_remove(&mode->link);
			free(mode);
		}
		wl_output_destroy(output->output);
		wl_list_remove(&output->link);
		free(output);
	}
    wl_registry_destroy(m_output_info.wlrandr);
	wl_display_flush(m_output_info.display);
	 wl_display_roundtrip(m_output_info.display);
	wl_display_disconnect(m_output_info.display);
	m_output_info.wlrandr = NULL;
	pthread_mutex_destroy(&lock_);

	return WLRANDR_RESULT_SUCCESS;
}
