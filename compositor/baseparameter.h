#ifndef WLRANDR_CONFIG_H
#define WLRANDR_CONFIG_H

#define BASE_OFFSET 8*1024

struct drm_display_mode {
    /* Proposed mode values */
    int clock;		/* in kHz */
    int hdisplay;
    int hsync_start;
    int hsync_end;
    int htotal;
    int vdisplay;
    int vsync_start;
    int vsync_end;
    int vtotal;
    int vrefresh;
    int vscan;
    uint32_t flags;
    int picture_aspect_ratio;
};

struct lut_data{
    uint16_t size;
    uint16_t lred[1024];
    uint16_t lgreen[1024];
    uint16_t lblue[1024];
};

enum output_format {
    output_rgb=0,
    output_ycbcr444=1,
    output_ycbcr422=2,
    output_ycbcr420=3,
    output_ycbcr_high_subsampling=4,  // (YCbCr444 > YCbCr422 > YCbCr420 > RGB)
    output_ycbcr_low_subsampling=5	, // (RGB > YCbCr420 > YCbCr422 > YCbCr444)
    invalid_output=6,
};

enum  output_depth{
    automatic=0,
    depth_24bit=8,
    depth_30bit=10,
};

struct overscan {
    uint32_t maxvalue;
    unsigned short leftscale;
    unsigned short rightscale;
    unsigned short topscale;
    unsigned short bottomscale;
};

struct hwc_inital_info{
    char device[128];
    uint32_t framebuffer_width;
    uint32_t framebuffer_height;
    float fps;
};

struct bcsh_info {
    unsigned short brightness;
    unsigned short contrast;
    unsigned short saturation;
    unsigned short hue;
};

struct screen_info {
    int type;
    struct drm_display_mode mode;// 52 bytes
    enum output_format  format; // 4 bytes
    enum output_depth depthc; // 4 bytes
    uint32_t feature;//4 //4 bytes
};

struct disp_info {
    struct screen_info screen_list[5];
    struct overscan scan;//12 bytes
    struct hwc_inital_info hwc_info; //140 bytes
    struct bcsh_info bcsh;
    unsigned int reserve[128];
    struct lut_data mlutdata;/*6k+4*/
};

struct file_base_paramer
{
    struct disp_info main;
    struct disp_info aux;
};

#endif
