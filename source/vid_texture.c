//Includes.
#include "vid_texture.h"

#include <stdlib.h>
#include <string.h>

#include "system/draw/draw.h"
#include "system/util/err.h"
#include "system/util/log.h"

uint32_t Vid_get_min_texture_size(uint32_t width_or_height)
{
	if(width_or_height <= 16)
		return 16;
	else if(width_or_height <= 32)
		return 32;
	else if(width_or_height <= 64)
		return 64;
	else if(width_or_height <= 128)
		return 128;
	else if(width_or_height <= 256)
		return 256;
	else if(width_or_height <= 512)
		return 512;
	else
		return 1024;
}

bool Vid_texture_dimensions_prefer_nearest(uint32_t codec_width, uint32_t codec_height)
{
	if(codec_width == 0 || codec_height == 0)
		return false;
	/* 240p + 偶数宽：含 400/640/800×240（800×240 不必单独写宽=800，避免 800×600 等误伤） */
	if(codec_height == 240u && (codec_width & 1u) == 0u)
		return true;
	/* 400 宽 + 偶数高：如 400×360；400×240 已由上行覆盖 */
	if(codec_width == 400u && (codec_height & 1u) == 0u)
		return true;
	return false;
}

void Vid_large_texture_free(Large_image* large_image_data)
{
	if(!large_image_data || !large_image_data->images)
		return;

	for(uint16_t i = 0; i < large_image_data->num_of_images; i++)
		Draw_texture_free(&large_image_data->images[i]);

	large_image_data->image_width = 0;
	large_image_data->image_height = 0;
	large_image_data->num_of_images = 0;
	free(large_image_data->images);
	large_image_data->images = NULL;
}

void Vid_large_texture_set_filter(Large_image* large_image_data, bool filter)
{
	if(!large_image_data || !large_image_data->images)
		return;

	for(uint16_t i = 0; i < large_image_data->num_of_images; i++)
		Draw_set_texture_filter(&large_image_data->images[i], filter);
}

void Vid_large_texture_crop(Large_image* large_image_data, uint32_t width, uint32_t height)
{
	uint32_t width_offset = 0;
	uint32_t height_offset = 0;

	if(!large_image_data || !large_image_data->images
	|| (width == large_image_data->image_width && height == large_image_data->image_height))
		return;

	for(uint16_t i = 0; i < large_image_data->num_of_images; i++)
	{
		float texture_width = large_image_data->images[i].c2d.tex->width;
		float texture_height = large_image_data->images[i].c2d.tex->height;

		if(width_offset + texture_width >= width)
		{
			//Crop for X direction.
			uint32_t new_width = 0;

			if(width > width_offset)
				new_width = (width - width_offset);

			large_image_data->images[i].subtex->width = new_width;
			large_image_data->images[i].subtex->right = new_width / texture_width;
		}

		if(height_offset + texture_height >= height)
		{
			//Crop for Y direction.
			uint32_t new_height = 0;

			if(height > height_offset)
				new_height = (height - height_offset);

			large_image_data->images[i].subtex->height = new_height;
			large_image_data->images[i].subtex->bottom = (texture_height - new_height) / texture_height;
		}

		//Update offset.
		width_offset += texture_width;
		if(width_offset >= large_image_data->image_width)
		{
			width_offset = 0;
			height_offset += texture_height;
		}
	}
}

uint32_t Vid_large_texture_init(Large_image* large_image_data, uint32_t width, uint32_t height, Raw_pixel color_format, bool zero_initialize)
{
	uint16_t loop = 0;
	uint32_t width_offset = 0;
	uint32_t height_offset = 0;
	uint32_t result = DEF_ERR_OTHER;

	if(!large_image_data || width == 0 || height == 0)
		goto invalid_arg;

	//Calculate how many textures we need.
	if(width % DEF_DRAW_MAX_TEXTURE_SIZE > 0)
		loop = (width / DEF_DRAW_MAX_TEXTURE_SIZE) + 1;
	else
		loop = (width / DEF_DRAW_MAX_TEXTURE_SIZE);

	if(height % DEF_DRAW_MAX_TEXTURE_SIZE > 0)
		loop *= ((height / DEF_DRAW_MAX_TEXTURE_SIZE) + 1);
	else
		loop *= (height / DEF_DRAW_MAX_TEXTURE_SIZE);

	//Init parameters.
	large_image_data->image_width = 0;
	large_image_data->image_height = 0;
	large_image_data->num_of_images = 0;
	large_image_data->images = (Draw_image_data*)malloc(sizeof(Draw_image_data) * loop);
	if(!large_image_data->images)
		goto out_of_memory;

	for(uint32_t i = 0; i < loop; i++)
	{
		uint32_t texture_width = Vid_get_min_texture_size(width - width_offset);
		uint32_t texture_height = Vid_get_min_texture_size(height - height_offset);

		result = Draw_texture_init(&large_image_data->images[i], texture_width, texture_height, color_format);
		if(result != DEF_SUCCESS)
		{
			DEF_LOG_RESULT(Draw_texture_init, false, result);
			goto error_other;
		}

		if(zero_initialize)
		{
			uint8_t pixel_size = 0;

			if(color_format == RAW_PIXEL_RGB565LE)
				pixel_size = 2;
			else if(color_format == RAW_PIXEL_BGR888)
				pixel_size = 3;
			else if(color_format == RAW_PIXEL_ABGR8888)
				pixel_size = 4;

			memset(large_image_data->images[i].c2d.tex->data, 0x0, texture_width * texture_height * pixel_size);
		}
		large_image_data->num_of_images++;

		//Update offset.
		width_offset += DEF_DRAW_MAX_TEXTURE_SIZE;
		if(width_offset >= width)
		{
			width_offset = 0;
			height_offset += DEF_DRAW_MAX_TEXTURE_SIZE;
		}
	}

	return DEF_SUCCESS;

	invalid_arg:
	return DEF_ERR_INVALID_ARG;

	out_of_memory:
	Vid_large_texture_free(large_image_data);
	return DEF_ERR_OUT_OF_LINEAR_MEMORY;

	error_other:
	Vid_large_texture_free(large_image_data);
	return result;
}

uint32_t Vid_large_texture_set_data(Large_image* large_image_data, uint8_t* raw_image, uint32_t width, uint32_t height, bool use_direct)
{
	uint16_t loop = 0;
	uint32_t width_offset = 0;
	uint32_t height_offset = 0;
	uint32_t result = DEF_ERR_OTHER;

	if(!large_image_data || !large_image_data->images || large_image_data->num_of_images == 0 || !raw_image || width == 0 || height == 0)
		goto invalid_arg;

	large_image_data->image_width = width;
	large_image_data->image_height = height;

	if(use_direct)
	{
		result = Draw_set_texture_data_direct(&large_image_data->images[0], raw_image, width, height);
		if(result != DEF_SUCCESS)
		{
			DEF_LOG_RESULT(Draw_set_texture_data_direct, false, result);
			goto error_other;
		}
	}
	else
	{
		//Calculate how many textures we need.
		if(width % DEF_DRAW_MAX_TEXTURE_SIZE > 0)
			loop = (width / DEF_DRAW_MAX_TEXTURE_SIZE) + 1;
		else
			loop = (width / DEF_DRAW_MAX_TEXTURE_SIZE);

		if(height % DEF_DRAW_MAX_TEXTURE_SIZE > 0)
			loop *= ((height / DEF_DRAW_MAX_TEXTURE_SIZE) + 1);
		else
			loop *= (height / DEF_DRAW_MAX_TEXTURE_SIZE);

		if(loop > large_image_data->num_of_images)
			loop = large_image_data->num_of_images;

		for(uint16_t i = 0; i < loop; i++)
		{
			result = Draw_set_texture_data(&large_image_data->images[i], raw_image, width, height, width_offset, height_offset);
			if(result != DEF_SUCCESS)
			{
				DEF_LOG_RESULT(Draw_set_texture_data, false, result);
				goto error_other;
			}

			//Update offset.
			width_offset += DEF_DRAW_MAX_TEXTURE_SIZE;
			if(width_offset >= width)
			{
				width_offset = 0;
				height_offset += DEF_DRAW_MAX_TEXTURE_SIZE;
			}
		}
	}

	return DEF_SUCCESS;

	invalid_arg:
	return DEF_ERR_INVALID_ARG;

	error_other:
	return result;
}

void Vid_large_texture_draw(Large_image* large_image_data, double x_offset, double y_offset, double pic_width, double pic_height)
{
	uint32_t width_offset = 0;
	uint32_t height_offset = 0;
	double width_factor = 0;
	double height_factor = 0;

	if(!large_image_data || !large_image_data->images || large_image_data->num_of_images == 0
	|| large_image_data->image_width == 0 || large_image_data->image_height == 0)
		return;

	width_factor = pic_width / large_image_data->image_width;
	height_factor = pic_height / large_image_data->image_height;

	for(uint16_t i = 0; i < large_image_data->num_of_images; i++)
	{
		if(large_image_data->images[i].subtex)
		{
			double texture_x_offset = x_offset + (width_offset * width_factor);
			double texture_y_offset = y_offset + (height_offset * height_factor);
			double texture_width = large_image_data->images[i].subtex->width * width_factor;
			double texture_height = large_image_data->images[i].subtex->height * height_factor;
			Draw_texture(&large_image_data->images[i], DEF_DRAW_NO_COLOR, texture_x_offset, texture_y_offset, texture_width, texture_height);

			//Update offset.
			width_offset += large_image_data->images[i].c2d.tex->width;
			if(width_offset >= large_image_data->image_width)
			{
				width_offset = 0;
				height_offset += large_image_data->images[i].c2d.tex->height;
			}
		}
	}
}
