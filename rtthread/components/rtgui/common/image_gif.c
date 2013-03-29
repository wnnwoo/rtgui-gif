
/*
 *  spec: http://www.w3.org/Graphics/GIF/spec-gif89a.txt
 *  wiki: http://en.wikipedia.org/wiki/GIF
 */
 

#include <rtthread.h>
#include <rtgui/dc_hw.h>
#include <rtgui/image.h>
#include <rtgui/rtgui_system.h>
#include <rtgui/image_gif.h>
#include <rtgui/blit.h>
#include "libnsgif.h"

#ifdef RTGUI_IMAGE_GIF

#define hw_driver               (rtgui_graphic_driver_get_default())

struct rtgui_image_gifheader
{
	rt_uint8_t signature[3];
	rt_uint8_t version[3];
};

typedef struct rtgui_image_gifheader rtgui_image_gifheader_t;

struct rtgui_image_gif
{
    gif_animation *decoder;
	rt_uint32_t curr_frame;
	struct rtgui_filerw *filerw;
};

typedef struct rtgui_image_gif rtgui_image_gif_t;

static rt_bool_t rtgui_image_gif_check(struct rtgui_filerw *file);
static rt_bool_t rtgui_image_gif_load(struct rtgui_image *image, struct rtgui_filerw *file, rt_bool_t load);
static void rtgui_image_gif_unload(struct rtgui_image *image);
static void rtgui_image_gif_blit(struct rtgui_image *image, struct rtgui_dc *dc, struct rtgui_rect *rect);

struct rtgui_image_engine rtgui_image_gif_engine =
{
    "gif",
    { RT_NULL },
    rtgui_image_gif_check,
    rtgui_image_gif_load,
    rtgui_image_gif_unload,
    rtgui_image_gif_blit,
    NULL,
    NULL
};

static rt_bool_t rtgui_image_gif_check(struct rtgui_filerw *file)
{
    rtgui_image_gifheader_t header;	
    rt_int32_t start;
    rt_bool_t is_gif = RT_FALSE;

    if (file == RT_NULL) 
	{
		return RT_FALSE;
    }
	
    start = rtgui_filerw_tell(file);

    /* move to the begining of file */
    rtgui_filerw_seek(file, 0, SEEK_SET);

    if (rtgui_filerw_read(file, &header, 1, sizeof(header)) == sizeof(header))
    {
        if (rt_strncmp(header.signature, "GIF", 3) == 0)
        {
            is_gif = RT_TRUE;
        }
    }
    rtgui_filerw_seek(file, start, SEEK_SET);

    return is_gif;
}

static void *rtgui_image_gif_bitmap_create(rt_int32_t width, rt_int32_t height)
{
	return rt_calloc(width * height, 4);
}


static void rtgui_image_gif_bitmap_set_opaque(void *bitmap, bool opaque)
{

}


static rt_bool_t rtgui_image_gif_bitmap_test_opaque(void *bitmap)
{
	return RT_TRUE;
}


static rt_uint8_t *rtgui_image_gif_bitmap_get_buffer(void *bitmap)
{
	return (rt_uint8_t *)bitmap;
}


static void rtgui_image_gif_bitmap_destroy(void *bitmap)
{
	rt_free(bitmap);
}


static void rtgui_image_gif_bitmap_modified(void *bitmap)
{

}

static rt_bool_t rtgui_image_gif_load(struct rtgui_image *image, struct rtgui_filerw *file, rt_bool_t load)
{
	gif_bitmap_callback_vt callbacks =
	{
		rtgui_image_gif_bitmap_create,
		rtgui_image_gif_bitmap_destroy,
		rtgui_image_gif_bitmap_get_buffer,
		rtgui_image_gif_bitmap_set_opaque,
		rtgui_image_gif_bitmap_test_opaque,
		rtgui_image_gif_bitmap_modified
	};

	rtgui_image_gif_t *gif;
	rt_uint8_t *buffer;
	rt_int32_t start;
	rt_uint32_t size;
	gif_result result;
	
	if (image == RT_NULL || file == RT_NULL)
	{
		return RT_FALSE;
	}
	gif = (rtgui_image_gif_t *)rt_malloc(sizeof(rtgui_image_gif_t));
	/* only support to decode the first frame right now*/
	gif->curr_frame = 0;
	
	RT_ASSERT(gif != RT_NULL);
	gif->decoder = (gif_animation *)rt_malloc(sizeof(gif_animation));
	RT_ASSERT(gif->decoder != RT_NULL);
	gif_create(gif->decoder, &callbacks);

    start = rtgui_filerw_tell(file);

    rtgui_filerw_seek(file, 0, SEEK_END);
	size = rtgui_filerw_tell(file);
	rtgui_filerw_seek(file, 0, SEEK_SET);
	
	buffer = (rt_uint8_t *)rt_malloc(size);
	RT_ASSERT(buffer != NULL);
	
	RT_ASSERT(rtgui_filerw_read(file, buffer, 1, size) == size);

	do {
		result = gif_initialise(gif->decoder, size, buffer);
		if (result != GIF_OK && result != GIF_WORKING) 
		{
			RT_ASSERT(0);
		}
	} while (result != GIF_OK);

	gif->filerw = file;
	image->w = gif->decoder->width;
	image->h = gif->decoder->height;	
	image->frame_count = gif->decoder->frame_count;
	image->interval = gif->decoder->loop_count;
	image->data = gif;

	return RT_TRUE;
}

static void rtgui_image_gif_unload(struct rtgui_image *image)
{
    struct rtgui_image_gif *gif;

    if (image != RT_NULL)
    {
        gif = (struct rtgui_image_gif *)image->data;
		gif_finalise(gif->decoder);

		rt_free(gif->decoder);
		
		if (gif->filerw != RT_NULL)
        {
            rtgui_filerw_close(gif->filerw);
        }
		rt_free(gif);
    }
}


static void rtgui_image_gif_blit(struct rtgui_image *image, struct rtgui_dc *dc, struct rtgui_rect *dst_rect)
{
	rtgui_image_gif_t *gif;
	gif_result result;

	if (image == RT_NULL || dc == RT_NULL || dst_rect == RT_NULL)
	{
		return;
	}
	gif = (rtgui_image_gif_t *)image->data;
	result = gif_decode_frame(gif->decoder, gif->curr_frame);
	if (result == GIF_OK)
	{
		rt_uint32_t i, j, pos, height, width;
		rt_uint8_t *pixel;
		rtgui_color_t color;
		
		height = rtgui_rect_height(*dst_rect);
		height = height <= gif->decoder->height ? height : gif->decoder->height;
		width = rtgui_rect_width(*dst_rect);
		width = width <= gif->decoder->width ? width : gif->decoder->width;
		
		pixel = (rt_uint8_t *) gif->decoder->frame_image;
		
		for (i = 0; i < height; i++)
		{
			for (j = 0; j < width; j++)
			{
				pos = (i * gif->decoder->width + j) * 4;
				color = RTGUI_RGB(pixel[pos], pixel[pos + 1], pixel[pos + 2]);
				rtgui_dc_draw_color_point(
					dc, 
					(dst_rect->x1 + j), 
					(dst_rect->y1 + i), 
					color);
			}
		}	
	}
	gif->curr_frame++;
	gif->curr_frame = (gif->curr_frame % gif->decoder->frame_count);

}


void rtgui_image_gif_init(void)
{
    rtgui_image_register_engine(&rtgui_image_gif_engine);
}

#endif /* RTGUI_IMAGE_GIF */

