// rdopng.cpp
// Copyright (C) 2022 Richard Geldreich, Jr. All Rights Reserved.
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// Normal map specific references:
// "Survey of Efficient Representations for Independent Unit Vectors" by Cigolle, Donow, et al
// https://jcgt.org/published/0003/02/01/
// "Objective Image Quality Assessment of Texture Compression" by Griffin, Olano
// https://tsapps.nist.gov/publication/get_pdf.cfm?pub_id=914900

#if _MSC_VER
// For sprintf(), strcpy() 
#define _CRT_SECURE_NO_WARNINGS (1)
#endif

#include "encoder/basisu.h"
#include "encoder/basisu_enc.h"

#define MINIZ_HEADER_FILE_ONLY
#define MINIZ_NO_ZLIB_COMPATIBLE_NAMES
#include "encoder/basisu_miniz.h"

#include "encoder/lodepng.h"

// Set BASISU_CATCH_EXCEPTIONS if you want exceptions to crash the app, otherwise main() catches them.
#ifndef BASISU_CATCH_EXCEPTIONS
	#define BASISU_CATCH_EXCEPTIONS 1
#endif

#define RDO_PNG_VERSION "v1.10"

#define RDO_PNG_USE_APPROX_ACOS (1)

const float DEF_MAX_SMOOTH_STD_DEV = 35.0f;
const float DEF_SMOOTH_MAX_MSE_SCALE = 250.0f;
const float DEF_MAX_ULTRA_SMOOTH_STD_DEV = 5.0F;
const float DEF_ULTRA_SMOOTH_MAX_MSE_SCALE = 1500.0F;

const float QOI_DEF_SMOOTH_MAX_MSE_SCALE = 2500.0f;
const float QOI_DEF_ULTRA_SMOOTH_MAX_MSE_SCALE = 5000.0f;

const float LZ4I_DEF_SMOOTH_MAX_MSE_SCALE = 8000.0f;
const float LZ4I_DEF_ULTRA_SMOOTH_MAX_MSE_SCALE = 10000.0f;

using namespace basisu;
using namespace buminiz;

extern bool g_use_miniz;

namespace buminiz
{
	extern uint64_t g_defl_freq[2][TDEFL_MAX_HUFF_SYMBOLS];
}

const float RAD_TO_DEG = 57.29577951f;

const uint32_t MAX_DELTA_COLORS = 12;

enum
{
	PNG_NO_FILTER = 0,
	PNG_PREV_PIXEL_FILTER = 1,
	PNG_PREV_SCANLINE_FILTER = 2,
	PNG_AVG_FILTER = 3,
	PNG_PAETH_FILTER = 4
};

struct match_order
{
	uint8_t v[MAX_DELTA_COLORS + 1];
};

static const match_order g_match_order8[] =
{
	{1, 8,0,0,0,0,0,0,0},
	{2, 7,1,0,0,0,0,0,0},
	{2, 1,7,0,0,0,0,0,0},
	{2, 6,2,0,0,0,0,0,0},
	{2, 2,6,0,0,0,0,0,0},
	{2, 5,3,0,0,0,0,0,0},
	{2, 3,5,0,0,0,0,0,0}
};
const uint32_t NUM_MATCH_ORDER_8 = sizeof(g_match_order8) / sizeof(g_match_order8[0]);

static const match_order g_match_order4[] =
{
	{ 1, 4 },
	{ 2, 1, 3 },
	{ 2, 3, 1 },
	{ 2, 2, 2 },
	{ 3, 1, 2, 1 },
	{ 3, 2, 1, 1 },
	{ 3, 1, 1, 2 },
	{ 4, 1, 1, 1, 1 }
};
const uint32_t NUM_MATCH_ORDER_4 = sizeof(g_match_order4) / sizeof(g_match_order4[0]);

static const match_order g_match_order12[] =
{
	{1, 12,0,0,0,0,0,0,0},
	{2, 11,1,0,0,0,0,0,0},
	{2, 1,11,0,0,0,0,0,0},
	{2, 10,2,0,0,0,0,0,0},
	{2, 2,10,0,0,0,0,0,0},
	{2, 9,3,0,0,0,0,0,0},
	{2, 3,9,0,0,0,0,0,0},
	{2, 8,4,0,0,0,0,0,0},
	{2, 4,8,0,0,0,0,0,0},
	{2, 7,5,0,0,0,0,0,0},
	{2, 5,7,0,0,0,0,0,0},
	{3, 6,3,3,0,0,0,0,0},
	{3, 3,3,6,0,0,0,0,0}
};
const uint32_t NUM_MATCH_ORDER_12 = sizeof(g_match_order12) / sizeof(g_match_order12[0]);

static const match_order g_match_order6[] =
{
	{ 1, 6 },

	{ 2, 1, 5 },
	{ 2, 5, 1 },

	{ 2, 3, 3 },
	{ 3, 2, 2, 2 },

	{ 2, 2, 4 },
	{ 2, 4, 2 },
	{ 3, 1, 1, 4 },
	{ 3, 4, 1, 1 },

	{ 3, 1, 2, 3 },
	{ 3, 2, 1, 3 },
	{ 3, 3, 1, 2 },
	{ 3, 3, 2, 1 },
	{ 4, 1, 1, 1, 3 },
	{ 4, 3, 1, 1, 1 },

	{ 4, 1, 2, 1, 2 },
	{ 4, 2, 1, 1, 2 },
	{ 4, 1, 2, 2, 1 },
	{ 4, 2, 2, 1, 1 },
	{ 4, 1, 1, 2, 2 },

	{ 6, 1, 1, 1, 1, 1, 1 },
};
const uint32_t NUM_MATCH_ORDER_6 = sizeof(g_match_order6) / sizeof(g_match_order6[0]);

static const match_order g_match_order6c[] =
{
	{1, 6,0,0,0,0,0},
	{2, 5,1,0,0,0,0},
	{2, 4,2,0,0,0,0},
	{2, 3,3,0,0,0,0},
	{2, 2,4,0,0,0,0},
	{2, 1,5,0,0,0,0},
	{3, 4,1,1,0,0,0},
	{3, 3,2,1,0,0,0},
	{3, 2,3,1,0,0,0},
	{3, 1,4,1,0,0,0},
	{3, 3,1,2,0,0,0},
	{3, 2,2,2,0,0,0},
	{3, 1,3,2,0,0,0},
	{3, 2,1,3,0,0,0},
	{3, 1,2,3,0,0,0},
	{3, 1,1,4,0,0,0},
	{4, 3,1,1,1,0,0},
	{4, 2,2,1,1,0,0},
	{4, 1,3,1,1,0,0},
	{4, 2,1,2,1,0,0},
	{4, 1,2,2,1,0,0},
	{4, 1,1,3,1,0,0},
	{4, 2,1,1,2,0,0},
	{4, 1,2,1,2,0,0},
	{4, 1,1,2,2,0,0},
	{4, 1,1,1,3,0,0},
	{5, 2,1,1,1,1,0},
	{5, 1,2,1,1,1,0},
	{5, 1,1,2,1,1,0},
	{5, 1,1,1,2,1,0},
	{5, 1,1,1,1,2,0},
	{6, 1,1,1,1,1,1},
};
const uint32_t NUM_MATCH_ORDER_6C = sizeof(g_match_order6c) / sizeof(g_match_order6c[0]);

// These values are in bytes, where=1 literal and >=4 match length.
static const match_order g_lz4_match_order_12_bytes[] =
{
	{ 1, 12,0,0,0,0,0,0,0,0,0,0,0 },
	{ 2, 11,1,0,0,0,0,0,0,0,0,0,0 },
	{ 2, 8,4,0,0,0,0,0,0,0,0,0,0 },
	{ 2, 7,5,0,0,0,0,0,0,0,0,0,0 },
	{ 2, 6,6,0,0,0,0,0,0,0,0,0,0 },
	{ 2, 5,7,0,0,0,0,0,0,0,0,0,0 },
	{ 2, 4,8,0,0,0,0,0,0,0,0,0,0 },
	{ 2, 1,11,0,0,0,0,0,0,0,0,0,0 },
	{ 3, 10,1,1,0,0,0,0,0,0,0,0,0 },
	{ 3, 7,4,1,0,0,0,0,0,0,0,0,0 },
	{ 3, 6,5,1,0,0,0,0,0,0,0,0,0 },
	{ 3, 5,6,1,0,0,0,0,0,0,0,0,0 },
	{ 3, 4,7,1,0,0,0,0,0,0,0,0,0 },
	{ 3, 1,10,1,0,0,0,0,0,0,0,0,0 },
	{ 3, 7,1,4,0,0,0,0,0,0,0,0,0 },
	{ 3, 4,4,4,0,0,0,0,0,0,0,0,0 },
	{ 3, 1,7,4,0,0,0,0,0,0,0,0,0 },
	{ 3, 6,1,5,0,0,0,0,0,0,0,0,0 },
	{ 3, 1,6,5,0,0,0,0,0,0,0,0,0 },
	{ 3, 5,1,6,0,0,0,0,0,0,0,0,0 },
	{ 3, 1,5,6,0,0,0,0,0,0,0,0,0 },
	{ 3, 4,1,7,0,0,0,0,0,0,0,0,0 },
	{ 3, 1,4,7,0,0,0,0,0,0,0,0,0 },
	{ 3, 1,1,10,0,0,0,0,0,0,0,0,0 },
	{ 4, 9,1,1,1,0,0,0,0,0,0,0,0 },
	{ 4, 6,4,1,1,0,0,0,0,0,0,0,0 },
	{ 4, 5,5,1,1,0,0,0,0,0,0,0,0 },
	{ 4, 4,6,1,1,0,0,0,0,0,0,0,0 },
	{ 4, 1,9,1,1,0,0,0,0,0,0,0,0 },
	{ 4, 6,1,4,1,0,0,0,0,0,0,0,0 },
	{ 4, 1,6,4,1,0,0,0,0,0,0,0,0 },
	{ 4, 5,1,5,1,0,0,0,0,0,0,0,0 },
	{ 4, 1,5,5,1,0,0,0,0,0,0,0,0 },
	{ 4, 4,1,6,1,0,0,0,0,0,0,0,0 },
	{ 4, 1,4,6,1,0,0,0,0,0,0,0,0 },
	{ 4, 1,1,9,1,0,0,0,0,0,0,0,0 },
	{ 4, 6,1,1,4,0,0,0,0,0,0,0,0 },
	{ 4, 1,6,1,4,0,0,0,0,0,0,0,0 },
	{ 4, 1,1,6,4,0,0,0,0,0,0,0,0 },
	{ 4, 5,1,1,5,0,0,0,0,0,0,0,0 },
	{ 4, 1,5,1,5,0,0,0,0,0,0,0,0 },
	{ 4, 1,1,5,5,0,0,0,0,0,0,0,0 },
	{ 4, 4,1,1,6,0,0,0,0,0,0,0,0 },
	{ 4, 1,4,1,6,0,0,0,0,0,0,0,0 },
	{ 4, 1,1,4,6,0,0,0,0,0,0,0,0 },
	{ 4, 1,1,1,9,0,0,0,0,0,0,0,0 },
	{ 5, 8,1,1,1,1,0,0,0,0,0,0,0 },
	{ 5, 5,4,1,1,1,0,0,0,0,0,0,0 },
	{ 5, 4,5,1,1,1,0,0,0,0,0,0,0 },
	{ 5, 1,8,1,1,1,0,0,0,0,0,0,0 },
	{ 5, 5,1,4,1,1,0,0,0,0,0,0,0 },
	{ 5, 1,5,4,1,1,0,0,0,0,0,0,0 },
	{ 5, 4,1,5,1,1,0,0,0,0,0,0,0 },
	{ 5, 1,4,5,1,1,0,0,0,0,0,0,0 },
	{ 5, 1,1,8,1,1,0,0,0,0,0,0,0 },
	{ 5, 5,1,1,4,1,0,0,0,0,0,0,0 },
	{ 5, 1,5,1,4,1,0,0,0,0,0,0,0 },
	{ 5, 1,1,5,4,1,0,0,0,0,0,0,0 },
	{ 5, 4,1,1,5,1,0,0,0,0,0,0,0 },
	{ 5, 1,4,1,5,1,0,0,0,0,0,0,0 },
	{ 5, 1,1,4,5,1,0,0,0,0,0,0,0 },
	{ 5, 1,1,1,8,1,0,0,0,0,0,0,0 },
	{ 5, 5,1,1,1,4,0,0,0,0,0,0,0 },
	{ 5, 1,5,1,1,4,0,0,0,0,0,0,0 },
	{ 5, 1,1,5,1,4,0,0,0,0,0,0,0 },
	{ 5, 1,1,1,5,4,0,0,0,0,0,0,0 },
	{ 5, 4,1,1,1,5,0,0,0,0,0,0,0 },
	{ 5, 1,4,1,1,5,0,0,0,0,0,0,0 },
	{ 5, 1,1,4,1,5,0,0,0,0,0,0,0 },
	{ 5, 1,1,1,4,5,0,0,0,0,0,0,0 },
	{ 5, 1,1,1,1,8,0,0,0,0,0,0,0 },
	{ 6, 7,1,1,1,1,1,0,0,0,0,0,0 },
	{ 6, 4,4,1,1,1,1,0,0,0,0,0,0 },
	{ 6, 1,7,1,1,1,1,0,0,0,0,0,0 },
	{ 6, 4,1,4,1,1,1,0,0,0,0,0,0 },
	{ 6, 1,4,4,1,1,1,0,0,0,0,0,0 },
	{ 6, 1,1,7,1,1,1,0,0,0,0,0,0 },
	{ 6, 4,1,1,4,1,1,0,0,0,0,0,0 },
	{ 6, 1,4,1,4,1,1,0,0,0,0,0,0 },
	{ 6, 1,1,4,4,1,1,0,0,0,0,0,0 },
	{ 6, 1,1,1,7,1,1,0,0,0,0,0,0 },
	{ 6, 4,1,1,1,4,1,0,0,0,0,0,0 },
	{ 6, 1,4,1,1,4,1,0,0,0,0,0,0 },
	{ 6, 1,1,4,1,4,1,0,0,0,0,0,0 },
	{ 6, 1,1,1,4,4,1,0,0,0,0,0,0 },
	{ 6, 1,1,1,1,7,1,0,0,0,0,0,0 },
	{ 6, 4,1,1,1,1,4,0,0,0,0,0,0 },
	{ 6, 1,4,1,1,1,4,0,0,0,0,0,0 },
	{ 6, 1,1,4,1,1,4,0,0,0,0,0,0 },
	{ 6, 1,1,1,4,1,4,0,0,0,0,0,0 },
	{ 6, 1,1,1,1,4,4,0,0,0,0,0,0 },
	{ 6, 1,1,1,1,1,7,0,0,0,0,0,0 },
	{ 7, 6,1,1,1,1,1,1,0,0,0,0,0 },
	{ 7, 1,6,1,1,1,1,1,0,0,0,0,0 },
	{ 7, 1,1,6,1,1,1,1,0,0,0,0,0 },
	{ 7, 1,1,1,6,1,1,1,0,0,0,0,0 },
	{ 7, 1,1,1,1,6,1,1,0,0,0,0,0 },
	{ 7, 1,1,1,1,1,6,1,0,0,0,0,0 },
	{ 7, 1,1,1,1,1,1,6,0,0,0,0,0 },
	{ 8, 5,1,1,1,1,1,1,1,0,0,0,0 },
	{ 8, 1,5,1,1,1,1,1,1,0,0,0,0 },
	{ 8, 1,1,5,1,1,1,1,1,0,0,0,0 },
	{ 8, 1,1,1,5,1,1,1,1,0,0,0,0 },
	{ 8, 1,1,1,1,5,1,1,1,0,0,0,0 },
	{ 8, 1,1,1,1,1,5,1,1,0,0,0,0 },
	{ 8, 1,1,1,1,1,1,5,1,0,0,0,0 },
	{ 8, 1,1,1,1,1,1,1,5,0,0,0,0 },
	{ 9, 4,1,1,1,1,1,1,1,1,0,0,0 },
	{ 9, 1,4,1,1,1,1,1,1,1,0,0,0 },
	{ 9, 1,1,4,1,1,1,1,1,1,0,0,0 },
	{ 9, 1,1,1,4,1,1,1,1,1,0,0,0 },
	{ 9, 1,1,1,1,4,1,1,1,1,0,0,0 },
	{ 9, 1,1,1,1,1,4,1,1,1,0,0,0 },
	{ 9, 1,1,1,1,1,1,4,1,1,0,0,0 },
	{ 9, 1,1,1,1,1,1,1,4,1,0,0,0 },
	{ 9, 1,1,1,1,1,1,1,1,4,0,0,0 },
	{ 12, 1,1,1,1,1,1,1,1,1,1,1,1 }
};
const uint32_t NUM_LZ4_MATCH_ORDER_12 = sizeof(g_lz4_match_order_12_bytes) / sizeof(g_lz4_match_order_12_bytes[0]);

enum speed_mode
{
	cNormalSpeed,
	cFasterSpeed,
	cFastestSpeed
};

struct rdo_png_params
{
	rdo_png_params()
	{
		clear();
	}

	void clear()
	{
		m_orig_img.clear();
		m_output_file_data.clear();
		m_lambda = 300.0f;
		m_level = 0;
		m_psnr = 0;
		m_angular_rms_error = 0;
		m_y_psnr = 0;
		m_bpp = 0;
		m_print_debug_output = false;
		m_debug_images = false;
		m_print_progress = false;
		m_print_stats = false;

		m_use_chan_weights = false;
		m_chan_weights[0] = 1;
		m_chan_weights[1] = 1;
		m_chan_weights[2] = 1;
		m_chan_weights[3] = 1;
				
		{
			float LW = 2;
			float AW = 1.5;
			float BW = 1;
			float l = sqrtf(LW * LW + AW * AW + BW * BW);
			LW /= l;
			AW /= l;
			BW /= l;
			m_chan_weights_lab[0] = LW; // L
			m_chan_weights_lab[1] = AW; // a
			m_chan_weights_lab[2] = BW; // b
			m_chan_weights_lab[3] = 1.5f; // alpha
		}
				
		m_use_reject_thresholds = true;
		m_reject_thresholds[0] = 32;
		m_reject_thresholds[1] = 32;
		m_reject_thresholds[2] = 32;
		m_reject_thresholds[3] = 32;

		m_reject_thresholds_lab[0] = .05f;
		//m_reject_thresholds_lab[1] = .075f;
		m_reject_thresholds_lab[1] = .05f;
		
		m_transparent_reject_test = false;

		m_perceptual_error = true;
		
		m_match_only = false;
		
		m_two_pass = false;
		
		m_alpha_is_opacity = true;

		m_speed_mode = cFastestSpeed;
		
		m_normal_map = false;
		m_snorm8 = false;
		
		m_print_normal_map_metrics = false;
				
		m_max_smooth_std_dev = DEF_MAX_SMOOTH_STD_DEV;
		m_smooth_max_mse_scale = DEF_SMOOTH_MAX_MSE_SCALE;
		m_max_ultra_smooth_std_dev = DEF_MAX_ULTRA_SMOOTH_STD_DEV;
		m_ultra_smooth_max_mse_scale = DEF_ULTRA_SMOOTH_MAX_MSE_SCALE;

		m_no_mse_scaling = false;
	}

	void print()
	{
		printf("orig image: %ux%u has alpha: %u\n", m_orig_img.get_width(), m_orig_img.get_height(), m_orig_img.has_alpha());
		printf("lambda: %f\n", m_lambda);
		printf("level: %u\n", m_level);
		printf("chan weights: %u %u %u %u\n", m_chan_weights[0], m_chan_weights[1], m_chan_weights[2], m_chan_weights[3]);
		printf("use chan weights: %u\n", m_use_chan_weights);
		printf("chan weights lab: %f %f %f %f\n", m_chan_weights_lab[0], m_chan_weights_lab[1], m_chan_weights_lab[2], m_chan_weights_lab[3]);
		printf("reject thresholds: %u %u %u %u\n", m_reject_thresholds[0], m_reject_thresholds[1], m_reject_thresholds[2], m_reject_thresholds[3]);
		printf("reject thresholds lab: %f %f\n", m_reject_thresholds_lab[0], m_reject_thresholds_lab[1]);
		printf("use reject thresholds: %u\n", m_use_reject_thresholds);
		printf("transparent reject test: %u\n", m_transparent_reject_test);
		printf("print debug output: %u\n", m_print_debug_output);
		printf("debug images: %u\n", m_debug_images);
		printf("print progress: %u\n", m_print_progress);
		printf("print stats: %u\n", m_print_stats);
		printf("perceptual error: %u\n", m_perceptual_error);
		printf("match only: %u\n", m_match_only);
		printf("two pass: %u\n", m_two_pass);
		printf("alpha is opacity: %u\n", m_alpha_is_opacity);
		printf("speed mode: %u\n", (uint32_t)m_speed_mode);
		printf("normal map: %u\n", m_normal_map);
		printf("snorm8: %u\n", m_snorm8);
		printf("print normal map metrics: %u\n", m_print_normal_map_metrics);
		printf("max smooth std dev: %f\n", m_max_smooth_std_dev);
		printf("smooth max mse scale: %f\n", m_smooth_max_mse_scale);
		printf("max ultra smooth std dev: %f\n", m_max_ultra_smooth_std_dev);
		printf("ultra smooth max mse scale: %f\n", m_ultra_smooth_max_mse_scale);
		printf("no MSE scaling: %u\n", m_no_mse_scaling);
	}

	// TODO: results - move
	float m_psnr;
	float m_angular_rms_error;
	float m_y_psnr;
	float m_bpp;
	
	// This is the output image data, but note for PNG you can't save it at the right size without the scanline predictor values.
	image m_output_image;

	image m_orig_img;

	uint8_vec m_output_file_data;
		
	float m_lambda;

	uint32_t m_level;
				
	uint32_t m_chan_weights[4];
	float m_chan_weights_lab[4];
	bool m_use_chan_weights;

	uint32_t m_reject_thresholds[4];
	float m_reject_thresholds_lab[2];
	bool m_use_reject_thresholds;

	bool m_transparent_reject_test;

	bool m_print_debug_output;
	bool m_debug_images;
	bool m_print_progress;
	bool m_print_stats;

	bool m_perceptual_error;

	bool m_match_only;
	bool m_two_pass;

	bool m_alpha_is_opacity;

	speed_mode m_speed_mode;

	bool m_normal_map;
	bool m_snorm8;
	bool m_print_normal_map_metrics;

	float m_max_smooth_std_dev;
	float m_smooth_max_mse_scale;
	float m_max_ultra_smooth_std_dev;
	float m_ultra_smooth_max_mse_scale;
	
	bool m_no_mse_scaling;
};

struct rdo_png_level
{
	int m_num_scanlines_to_check;
	uint32_t m_first_filter;
	uint32_t m_last_filter;
	bool m_double_width;

	uint32_t m_M;

	int m_search_dist;
	bool m_exhaustive_search;

	const uint32_t m_num_match_order_a;
	const match_order* m_pMatch_order_a;
	const uint32_t m_num_match_order_b;
	const match_order* m_pMatch_order_b;
};

static const rdo_png_level g_levels[30] =
{
	// 4 pixels wide
	
	// 0-1
	{ 1, 3, 3, false, 4, 16, false, NUM_MATCH_ORDER_4, g_match_order4, 0, nullptr },
	{ 1, 3, 3, false, 4, 32, false, NUM_MATCH_ORDER_4, g_match_order4, 0, nullptr },

	// 2-3
	{ 2, 3, 3, false, 4, 32, false, NUM_MATCH_ORDER_4, g_match_order4, 0, nullptr },
	{ 2, 3, 4, false, 4, 32, false, NUM_MATCH_ORDER_4, g_match_order4, 0, nullptr },

	// 4-5
	{ 2, 3, 4, false, 4, 64, false, NUM_MATCH_ORDER_4, g_match_order4, 0, nullptr },
	{ 4, 3, 4, false, 4, 64, false, NUM_MATCH_ORDER_4, g_match_order4, 0, nullptr },

	// 6-7
	{ 4, 3, 4, false, 4, 128, false, NUM_MATCH_ORDER_4, g_match_order4, 0, nullptr },
	{ 4, 3, 4, false, 4, 256, false, NUM_MATCH_ORDER_4, g_match_order4, 0, nullptr },

	// 8-9
	{ 6, 3, 4, false, 4, 256, false, NUM_MATCH_ORDER_4, g_match_order4, 0, nullptr },
	{ 8, 3, 4, false, 4, 256, false, NUM_MATCH_ORDER_4, g_match_order4, 0, nullptr },

	// 6 pixels wide - greater compression
	// 10-11
	{ 1, 3, 3, false, 6, 16, false, NUM_MATCH_ORDER_6, g_match_order6, 0, nullptr },
	{ 1, 3, 4, false, 6, 32, false, NUM_MATCH_ORDER_6, g_match_order6, 0, nullptr },

	// 12-13
	{ 2, 3, 4, false, 6, 32, false, NUM_MATCH_ORDER_6C, g_match_order6c, 0, nullptr },
	{ 4, 3, 4, false, 6, 64, false, NUM_MATCH_ORDER_6C, g_match_order6c, 0, nullptr },
	
	// 14-15	
	{ 4, 3, 4, false, 6, 128, false, NUM_MATCH_ORDER_6C, g_match_order6c, 0, nullptr },
	{ 4, 3, 4, false, 6, 256, false, NUM_MATCH_ORDER_6C, g_match_order6c, 0, nullptr },

	// 16-17
	{ 8, 3, 4, false, 6, 256, false, NUM_MATCH_ORDER_6C, g_match_order6c, 0, nullptr },
	{ 8, 1, 4, false, 6, 256, false, NUM_MATCH_ORDER_6C, g_match_order6c, 0, nullptr },
		
	// double matching, 6 or 12 pixels wide
	// 18-19
	{ 1, 3, 3, true, 6, 16, false, NUM_MATCH_ORDER_6, g_match_order6, NUM_MATCH_ORDER_12, g_match_order12 },
	{ 1, 3, 4, true, 6, 32, false, NUM_MATCH_ORDER_6C, g_match_order6c, NUM_MATCH_ORDER_12, g_match_order12 },

	// 20-21
	{ 4, 3, 4, true, 6, 64, false, NUM_MATCH_ORDER_6, g_match_order6, NUM_MATCH_ORDER_12, g_match_order12 },
	{ 4, 3, 4, true, 6, 128, false, NUM_MATCH_ORDER_6C, g_match_order6c, NUM_MATCH_ORDER_12, g_match_order12 },

	// 22-23
	{ 4, 3, 4, true, 6, 256, false, NUM_MATCH_ORDER_6C, g_match_order6c, NUM_MATCH_ORDER_12, g_match_order12 },
	{ 8, 3, 4, true, 6, 256, false, NUM_MATCH_ORDER_6C, g_match_order6c, NUM_MATCH_ORDER_12, g_match_order12 },

	// Exhaustive searching (for tiny images/testing)
	// 24-25
	{ 4, 1, 4, false, 4, 256, true, NUM_MATCH_ORDER_4, g_match_order4, 0, nullptr },
	{ 8, 1, 4, false, 4, 256, true, NUM_MATCH_ORDER_4, g_match_order4, 0, nullptr },

	// 26-27
	{ 4, 1, 4, false, 6, 256, true, NUM_MATCH_ORDER_6C, g_match_order6c, 0, nullptr },
	{ 8, 1, 4, false, 6, 256, true, NUM_MATCH_ORDER_6C, g_match_order6c, 0, nullptr },

	// 28-29
	{ 4, 1, 4, false, 6, 256, true, NUM_MATCH_ORDER_6, g_match_order6, 0, nullptr },
	{ 8, 1, 4, false, 6, 256, true, NUM_MATCH_ORDER_6, g_match_order6, 0, nullptr },
};
const uint32_t MAX_LEVELS = sizeof(g_levels) / sizeof(g_levels[0]);

static const uint16_t g_tdefl_len_sym[256] = {
  257,258,259,260,261,262,263,264,265,265,266,266,267,267,268,268,269,269,269,269,270,270,270,270,271,271,271,271,272,272,272,272,
  273,273,273,273,273,273,273,273,274,274,274,274,274,274,274,274,275,275,275,275,275,275,275,275,276,276,276,276,276,276,276,276,
  277,277,277,277,277,277,277,277,277,277,277,277,277,277,277,277,278,278,278,278,278,278,278,278,278,278,278,278,278,278,278,278,
  279,279,279,279,279,279,279,279,279,279,279,279,279,279,279,279,280,280,280,280,280,280,280,280,280,280,280,280,280,280,280,280,
  281,281,281,281,281,281,281,281,281,281,281,281,281,281,281,281,281,281,281,281,281,281,281,281,281,281,281,281,281,281,281,281,
  282,282,282,282,282,282,282,282,282,282,282,282,282,282,282,282,282,282,282,282,282,282,282,282,282,282,282,282,282,282,282,282,
  283,283,283,283,283,283,283,283,283,283,283,283,283,283,283,283,283,283,283,283,283,283,283,283,283,283,283,283,283,283,283,283,
  284,284,284,284,284,284,284,284,284,284,284,284,284,284,284,284,284,284,284,284,284,284,284,284,284,284,284,284,284,284,284,285 };

static const uint8_t g_tdefl_len_extra[256] = {
	0,0,0,0,0,0,0,0,1,1,1,1,1,1,1,1,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,
	4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,
	5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,
	5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,0 };

static const uint8_t g_tdefl_small_dist_sym[512] = {
	0,1,2,3,4,4,5,5,6,6,6,6,7,7,7,7,8,8,8,8,8,8,8,8,9,9,9,9,9,9,9,9,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,11,11,11,11,11,11,
	11,11,11,11,11,11,11,11,11,11,12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,13,
	13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,14,14,14,14,14,14,14,14,14,14,14,14,
	14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,
	14,14,14,14,14,14,14,14,14,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,
	15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,16,16,16,16,16,16,16,16,16,16,16,16,16,
	16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,
	16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,
	16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,17,17,17,17,17,17,17,17,17,17,17,17,17,17,
	17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,
	17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,
	17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17 };

static const uint8_t g_tdefl_large_dist_sym[128] = {
  0,0,18,19,20,20,21,21,22,22,22,22,23,23,23,23,24,24,24,24,24,24,24,24,25,25,25,25,25,25,25,25,26,26,26,26,26,26,26,26,26,26,26,26,
  26,26,26,26,27,27,27,27,27,27,27,27,27,27,27,27,27,27,27,27,28,28,28,28,28,28,28,28,28,28,28,28,28,28,28,28,28,28,28,28,28,28,28,28,
  28,28,28,28,28,28,28,28,29,29,29,29,29,29,29,29,29,29,29,29,29,29,29,29,29,29,29,29,29,29,29,29,29,29,29,29,29,29,29,29 };

static const uint8_t g_tdefl_small_dist_extra[512] =
{
	0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 5, 5, 5, 5, 5, 5, 5, 5,
	5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
	6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
	6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
	7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
	7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
	7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
	7, 7, 7, 7, 7, 7, 7, 7
};

static const uint8_t g_tdefl_large_dist_extra[128] =
{
	0, 0, 8, 8, 9, 9, 9, 9, 10, 10, 10, 10, 10, 10, 10, 10, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12,
	12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13,
	13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13
};

static inline float square(float f)
{
	return f * f;
}

static inline uint32_t byteswap_32(uint32_t v)
{
	return ((v & 0xFF) << 24) | (((v >> 8) & 0xFF) << 16) | (((v >> 16) & 0xFF) << 8) | ((v >> 24) & 0xFF);
}

class tracked_stat
{
public:
	tracked_stat() { clear(); }

	inline void clear() { m_num = 0; m_total = 0; m_total2 = 0; }

	inline void update(uint32_t val) { m_num++; m_total += val; m_total2 += val * val; }

	inline tracked_stat& operator += (uint32_t val) { update(val); return *this; }

	inline uint32_t get_number_of_values() { return m_num; }
	inline uint64_t get_total() const { return m_total; }
	inline uint64_t get_total2() const { return m_total2; }

	inline float get_average() const { return m_num ? (float)m_total / m_num : 0.0f; };
	inline float get_std_dev() const { return m_num ? sqrtf((float)(m_num * m_total2 - m_total * m_total)) / m_num : 0.0f; }
	inline float get_variance() const { float s = get_std_dev(); return s * s; }

private:
	uint32_t m_num;
	uint64_t m_total;
	uint64_t m_total2;
};

static inline vec3F decode_normal(const color_rgba& c, const rdo_png_params& params)
{
	if (params.m_snorm8)
	{
		// snomr8 - supported by GPU's. Zero can be represented exactly, two values for -1.
		return vec3F(
			clamp((float)(c.r - 128) * (1.0f / 127.0f), -1.0f, 1.0f),
			clamp((float)(c.g - 128) * (1.0f / 127.0f), -1.0f, 1.0f),
			clamp((float)(c.b - 128) * (1.0f / 127.0f), -1.0f, 1.0f));
	}
	else
	{
		// unorm8 - zero cannot be represented exactly
		return vec3F((c.r * (1.0f / 255.0f)) * 2.0f - 1.0f, (c.g * (1.0f / 255.0f)) * 2.0f - 1.0f, (c.b * (1.0f / 255.0f)) * 2.0f - 1.0f);
	}
}

static inline color_rgba encode_normal(const vec3F& v, int alpha, const rdo_png_params& params)
{
	color_rgba result;

	if (params.m_snorm8)
	{
		result.set((int)std::round(v[0] * 127.0f) + 128, (int)std::round(v[1] * 127.0f) + 128, (int)std::round(v[2] * 127.0f) + 128, alpha);
	}
	else
	{
		result.set(
			(int)std::round(((v[0] * .5f) + .5f) * 255.0f),
			(int)std::round(((v[1] * .5f) + .5f) * 255.0f),
			(int)std::round(((v[2] * .5f) + .5f) * 255.0f),
			alpha);
	}

	return result;
}

static color_rgba encode_normal_exhaustive(const vec3F& v, int alpha, const rdo_png_params& params)
{
	color_rgba result;

	float best_dot = -1e+9f;
	color_rgba best_color(0);
	for (uint32_t i = 0; i < 8; i++)
	{
		if (params.m_snorm8)
		{
			result.set(
				(int)((i & 1) ? floorf : ceilf)(v[0] * 127.0f) + 128,
				(int)((i & 2) ? floorf : ceilf)(v[1] * 127.0f) + 128,
				(int)((i & 4) ? floorf : ceilf)(v[2] * 127.0f) + 128, alpha);
		}
		else
		{
			result.set(
				(int)((i & 1) ? floorf : ceilf)(((v[0] * .5f) + .5f) * 255.0f),
				(int)((i & 2) ? floorf : ceilf)(((v[1] * .5f) + .5f) * 255.0f),
				(int)((i & 4) ? floorf : ceilf)(((v[2] * .5f) + .5f) * 255.0f), alpha);

			color_rgba result2(
				(int)((i & 1) ? floorf : ceilf)(((v[0] * .5f) + .5f) * 255.0f),
				(int)((i & 2) ? floorf : ceilf)(((v[1] * .5f) + .5f) * 255.0f),
				(int)((i & 4) ? floorf : ceilf)(((v[2] * .5f) + .5f) * 255.0f), alpha);
		}

		vec3F decoded_v(decode_normal(result, params));
		decoded_v.normalize_in_place();

		float dot = decoded_v.dot(v);
		if (dot > best_dot)
		{
			best_dot = dot;
			best_color = result;
		}
	}

	return best_color;
}

static inline uint32_t compute_match_cost(uint32_t dist, uint32_t match_len_in_bytes, const huffman_encoding_table& lit_tab, const huffman_encoding_table& dist_tab)
{
	assert(match_len_in_bytes >= 3 && match_len_in_bytes <= 258);
	assert(dist >= 1 && dist <= 32768);
		
	uint32_t len_sym = g_tdefl_len_sym[match_len_in_bytes - 3];
	uint32_t len_cost = lit_tab.get_code_sizes()[len_sym] + g_tdefl_len_extra[match_len_in_bytes - 3];
	assert(lit_tab.get_code_sizes()[len_sym]);

	uint32_t adj_dist = dist - 1;

	uint32_t dist_cost;
	if (adj_dist < 512)
	{
		dist_cost = dist_tab.get_code_sizes()[g_tdefl_small_dist_sym[adj_dist]];
		dist_cost += g_tdefl_small_dist_extra[adj_dist];
	}
	else
	{
		dist_cost = dist_tab.get_code_sizes()[g_tdefl_large_dist_sym[adj_dist >> 8]];
		dist_cost += g_tdefl_large_dist_extra[adj_dist >> 8];
	}

	return len_cost + dist_cost;
}

// c b
// a x
static inline uint8_t paeth(int a, int b, int c)
{
	int p = a + b - c, pa = abs(p - a), pb = abs(p - b), pc = abs(p - c);
	
	if (pa <= pb && pa <= pc) 
		return (uint8_t)(a);

	if (pb <= pc) 
		return (uint8_t)(b);

	return (uint8_t)(c);
}

static inline uint8_t avg(int a, int b, int c)
{
	return (uint8_t)((a + b) / 2);
}

static inline color_rgba png_predict(const color_rgba& trial_c, uint32_t x, uint32_t y, const image& coded_img, uint32_t filter, uint32_t num_comps)
{
	assert(filter);
		
	const color_rgba ca(x ? coded_img(x - 1, y) : g_black_color);
	const color_rgba cb(y ? coded_img(x, y - 1) : g_black_color);
	const color_rgba cc((x && y) ? coded_img(x - 1, y - 1) : g_black_color);

	color_rgba res;

	for (uint32_t c = 0; c < num_comps; c++)
	{
		uint32_t pa = ca[c];
		uint32_t pb = cb[c];
		uint32_t pc = cc[c];

		uint32_t d;
		if (filter == PNG_PAETH_FILTER)
			d = paeth(pa, pb, pc);
		else if (filter == PNG_AVG_FILTER)
			d = avg(pa, pb, pc);
		else if (filter == PNG_PREV_SCANLINE_FILTER)
			d = pb;
		else
		{
			assert(filter == PNG_PREV_PIXEL_FILTER);
			d = pa;
		}

		res[c] = (uint8_t)(trial_c[c] - d);
	}

	if (num_comps == 3)
		res[3] = 255;

	return res;
}

static inline color_rgba png_unpredict(const color_rgba& delta_c, uint32_t x, uint32_t y, const image& coded_img, uint32_t filter, uint32_t num_comps)
{
	color_rgba res;

	const color_rgba ca(x ? coded_img(x - 1, y) : g_black_color);
	const color_rgba cb(y ? coded_img(x, y - 1) : g_black_color);
	const color_rgba cc((x && y) ? coded_img(x - 1, y - 1) : g_black_color);

	for (uint32_t c = 0; c < num_comps; c++)
	{
		uint32_t pa = ca[c];
		uint32_t pb = cb[c];
		uint32_t pc = cc[c];
		
		uint32_t d;
		if (filter == 4)
			d = paeth(pa, pb, pc);
		else if (filter == 3)
			d = avg(pa, pb, pc);
		else if (filter == 2)
			d = pb;
		else
		{
			assert(filter == PNG_PREV_PIXEL_FILTER);
			d = pa;
		}

		res[c] = (uint8_t)(delta_c[c] + d);
	}

	if (num_comps == 3)
		res[3] = 255;

	return res;
}

struct Lab { float L; float a; float b; };
struct RGB { float r; float g; float b; };

static inline Lab linear_srgb_to_oklab(RGB c)
{
	float l = 0.4122214708f * c.r + 0.5363325363f * c.g + 0.0514459929f * c.b;
	float m = 0.2119034982f * c.r + 0.6806995451f * c.g + 0.1073969566f * c.b;
	float s = 0.0883024619f * c.r + 0.2817188376f * c.g + 0.6299787005f * c.b;

	float l_ = std::cbrtf(l);
	float m_ = std::cbrtf(m);
	float s_ = std::cbrtf(s);

	return 
	{
		0.2104542553f * l_ + 0.7936177850f * m_ - 0.0040720468f * s_,
		1.9779984951f * l_ - 2.4285922050f * m_ + 0.4505937099f * s_,
		0.0259040371f * l_ + 0.7827717662f * m_ - 0.8086757660f * s_,
	};
}

static float g_srgb_to_linear[256];

static float f_inv(float x)
{
	if (x <= 0.04045f)
		return x / 12.92f;
	else
		return powf(((x + 0.055f) / 1.055f), 2.4f);
}

static void init_srgb_to_linear()
{
	for (uint32_t i = 0; i < 256; i++)
		g_srgb_to_linear[i] = f_inv(i / 255.0f);
}

#pragma pack(push, 1)
struct Lab16
{
	uint16_t m_L, m_a, m_b;
};
#pragma pack(pop)

basisu::vector<Lab16> g_srgb_to_oklab16;

const float SCALE_L = 1.0f / 65535.0f;
const float SCALE_A = (1.0f / 65535.0f) * (0.276216f - (-0.233887f));
const float OFS_A = -0.233887f;
const float SCALE_B = (1.0f / 65535.0f) * (0.198570f - (-0.311528f));
const float OFS_B = -0.311528f;

const float MIN_L = 0.000000f, MAX_L = 1.000000f;
const float MIN_A = -0.233888f, MAX_A = 0.276217f;
const float MIN_B = -0.311529f, MAX_B = 0.198570f;

static inline Lab srgb_to_oklab(const color_rgba &c)
{
	const Lab16 &l = g_srgb_to_oklab16[c.r + c.g * 256 + c.b * 65536];
	
	Lab res;
	res.L = l.m_L * SCALE_L;
	res.a = l.m_a * SCALE_A + OFS_A;
	res.b = l.m_b * SCALE_B + OFS_B;

	return res;
}

static inline Lab srgb_to_oklab_norm(const color_rgba& c)
{
	const Lab16& l = g_srgb_to_oklab16[c.r + c.g * 256 + c.b * 65536];

	Lab res;
	res.L = l.m_L * SCALE_L;
	res.a = l.m_a * SCALE_L;
	res.b = l.m_b * SCALE_L;

	return res;
}

static void init_oklab_table(const char *pExec, bool quiet, bool caching_enabled)
{
	g_srgb_to_oklab16.resize(256 * 256 * 256);

	std::string path(pExec);

	if (caching_enabled)
	{
		string_get_pathname(pExec, path);
		path += "oklab.bin";

		uint8_vec file_data;
		if (read_file_to_vec(path.c_str(), file_data))
		{
			if (file_data.size() == 256 * 256 * 256 * 6)
			{
				memcpy(g_srgb_to_oklab16.data(), file_data.data(), file_data.size_in_bytes());
				if (!quiet)
					printf("Read Oklab table data from file %s\n", path.c_str());
				return;
			}
		}
	}
	
	if (!quiet)
		printf("Computing Oklab table\n");

	for (uint32_t r = 0; r <= 255; r++)
	{
		//printf("%u\n", r);

		for (uint32_t g = 0; g <= 255; g++)
		{
			for (uint32_t b = 0; b <= 255; b++)
			{
				color_rgba c(r, g, b, 255);
				Lab l(linear_srgb_to_oklab({ g_srgb_to_linear[c.r], g_srgb_to_linear[c.g], g_srgb_to_linear[c.b] }));

				assert(l.L >= MIN_L && l.L <= MAX_L);
				assert(l.a >= MIN_A && l.a <= MAX_A);
				assert(l.b >= MIN_B && l.b <= MAX_B);
				
				float lL = std::round(((l.L - MIN_L) / (MAX_L - MIN_L)) * 65535.0f);
				float la = std::round(((l.a - MIN_A) / (MAX_A - MIN_A)) * 65535.0f);
				float lb = std::round(((l.b - MIN_B) / (MAX_B - MIN_B)) * 65535.0f);

				lL = clamp(lL, 0.0f, 65535.0f);
				la = clamp(la, 0.0f, 65535.0f);
				lb = clamp(lb, 0.0f, 65535.0f);

				Lab16& v = g_srgb_to_oklab16[r + g * 256 + b * 65536];
				v.m_L = (uint16_t)lL;
				v.m_a = (uint16_t)la;
				v.m_b = (uint16_t)lb;

				Lab cl = srgb_to_oklab(c);

				//printf("%f %f %f, %f %f %f\n", l.L, l.a, l.b, cl.L, cl.a, cl.b);
			}
		}
	}

	if (caching_enabled)
	{
		if (write_data_to_file(path.c_str(), g_srgb_to_oklab16.data(), g_srgb_to_oklab16.size_in_bytes()))
		{
			if (!quiet)
				printf("Wrote oklab lookup table to file %s\n", path.c_str());
		}
		else
		{
			fprintf(stderr, "Failed writing oklab lookup table to file %s\n", path.c_str());
		}
	}
}

const uint32_t ACOS_LOOKUP_SIZE = 1024;
float g_acos_lookup[ACOS_LOOKUP_SIZE + 1];
const float ACOS_LOW_ANGLE_THRESHOLD = .95f;

static inline float approx_acos(float f)
{
	const bool is_neg = f < 0.0f;
	f = clamp(fabs(f), 0.0f, 1.0f);
		
	float r;
	// Use Taylor at low angles, otherwise table+bilinear.
	if (f >= ACOS_LOW_ANGLE_THRESHOLD)
	{
		r = sqrtf(2.0f * (1.0f - f)) * RAD_TO_DEG;
	}
	else
	{
		float fract = f - floor(f);
		int index = (int)(f * (ACOS_LOOKUP_SIZE - 1));
		assert(index < ACOS_LOOKUP_SIZE);
		r = g_acos_lookup[index] * (1.0f - fract) + g_acos_lookup[index + 1] * fract;
	}

	return is_neg ? (180.0f - r) : r;
}

static void init_acos_lookup()
{
	for (uint32_t i = 0; i < ACOS_LOOKUP_SIZE; i++)
		g_acos_lookup[i] = acos((float)i / (float)(ACOS_LOOKUP_SIZE - 1)) * RAD_TO_DEG;

	g_acos_lookup[ACOS_LOOKUP_SIZE] = g_acos_lookup[ACOS_LOOKUP_SIZE - 1];

#if 0
	double tot_err = 0;
	const uint32_t N = 32768;
	double max_err = 0;
	for (uint32_t i = 0; i < N; i++)
	{
		float f = ((float)i / (float)(N - 1)) * 2.0f - 1.0f;
		float err = approx_acos(f) - acos(f) * RAD_TO_DEG;
		printf("%f %f %f %f\n", f, approx_acos(f), acos(f) * RAD_TO_DEG, err);
		tot_err += fabs(err);
		max_err = maximum<double>(max_err, fabs(err));
	}
	printf("Total err: %f, avg: %f, max: %f\n", tot_err, tot_err / N, max_err);
	exit(0);
#endif
}

static inline float compute_se(const color_rgba& a, const color_rgba& orig, uint32_t num_comps, const rdo_png_params &params)
{
	float dist;
			
	if (params.m_normal_map)
	{
		vec3F caf(decode_normal(a, params));
		vec3F cbf(decode_normal(orig, params));

		float len_a = caf.length();
		if (len_a != 0)
			caf /= len_a;

		float len_b = cbf.length();
		if (len_b != 0)
			cbf /= len_b;

		float dot = caf.dot(cbf);
		
#if RDO_PNG_USE_APPROX_ACOS
		float ang_err = approx_acos(dot);
#else
		float ang_err = acosf(clamp<float>(dot, -1.0f, 1.0f)) * RAD_TO_DEG;
#endif
								
		float len_err = fabsf(len_a - 1.0f);
		// If the length is close enough to 1.0 then don't incentivize the encoder to reduce it.
		const float LEN_ERR_THRESH = .1f;
		if (len_err < LEN_ERR_THRESH)
			len_err = 0.0f;
		else
			len_err -= LEN_ERR_THRESH;
		len_err *= 255.0f;

		const float ANG_ERR_SCALE = 4.0f; // normalization factor, so lambda is roughly comparable to -linear
		const float LEN_ERR_SCALE = .1f; // prevent the encoder from over-optimizing for length=1.0
		dist = square(ang_err) * ANG_ERR_SCALE + square(len_err) * LEN_ERR_SCALE;

		if (num_comps == 4)
		{
			int da = (int)a[3] - (int)orig[3];
			dist += (float)params.m_chan_weights[3] * square((float)da);
		}
	}
	else if (params.m_perceptual_error)
	{
		Lab la = srgb_to_oklab_norm(a);
		Lab lb = srgb_to_oklab_norm(orig);

		la.L -= lb.L;
		la.a -= lb.a;
		la.b -= lb.b;
						
		float L_d = la.L * la.L;
		float a_d = la.a * la.a;
		float b_d = la.b * la.b;

		L_d *= params.m_chan_weights_lab[0];
		a_d *= params.m_chan_weights_lab[1];
		b_d *= params.m_chan_weights_lab[2];
						
		dist = L_d + a_d + b_d;
	
		// TODO: Scales the error to bring it into a range where lambda will be roughly comparable to plain MSE.
		const float NORM_ERROR_SCALE = 350000.0f;
		dist *= NORM_ERROR_SCALE;

		if (num_comps == 4)
		{
			int da = (int)a[3] - (int)orig[3];
			dist += params.m_chan_weights_lab[3] * square((float)da);
		}
	}
	else if (params.m_use_chan_weights)
	{
		int dr = (int)a[0] - (int)orig[0];
		int dg = (int)a[1] - (int)orig[1];
		int db = (int)a[2] - (int)orig[2];

		uint32_t idist = (uint32_t)(params.m_chan_weights[0] * (uint32_t)(dr * dr) + params.m_chan_weights[1] * (uint32_t)(dg * dg) + params.m_chan_weights[2] * (uint32_t)(db * db));
		if (num_comps == 4)
		{
			int da = (int)a[3] - (int)orig[3];
			idist += params.m_chan_weights[3] * (uint32_t)(da * da);
		}

		dist = (float)idist;
	}
	else
	{
		int dr = (int)a[0] - (int)orig[0];
		int dg = (int)a[1] - (int)orig[1];
		int db = (int)a[2] - (int)orig[2];

		uint32_t idist = (uint32_t)(dr * dr + dg * dg + db * db);
		if (num_comps == 4)
		{
			int da = (int)a[3] - (int)orig[3];
			idist += da * da;
		}

		dist = (float)idist;
	}

	return dist;
}

static inline bool should_reject(const color_rgba& trial_color, const color_rgba& orig_color, uint32_t num_comps, const rdo_png_params& params)
{
	if ((params.m_transparent_reject_test) && (num_comps == 4))
	{
		if ((orig_color[3] == 0) && (trial_color[3] > 0))
			return true;

		if ((orig_color[3] == 255) && (trial_color[3] < 255))
			return true;
	}

	if (params.m_use_reject_thresholds)
	{
		if (params.m_perceptual_error)
		{
			Lab t(srgb_to_oklab_norm(trial_color));
			Lab o(srgb_to_oklab_norm(orig_color));

			float L_diff = fabs(t.L - o.L);
									
			if (L_diff > params.m_reject_thresholds_lab[0])
				return true;

			float ab_dist = squaref(t.a - o.a) + squaref(t.b - o.b);
			
			if (ab_dist > (params.m_reject_thresholds_lab[1] * params.m_reject_thresholds_lab[1]))
				return true;

			if (num_comps == 4)
			{
				uint32_t delta_a = abs((int)trial_color[3] - (int)orig_color[3]);
				if (delta_a > params.m_reject_thresholds[3])
					return true;
			}
		}
		else
		{
			uint32_t delta_r = abs((int)trial_color[0] - (int)orig_color[0]);
			uint32_t delta_g = abs((int)trial_color[1] - (int)orig_color[1]);
			uint32_t delta_b = abs((int)trial_color[2] - (int)orig_color[2]);

			if (delta_r > params.m_reject_thresholds[0])
				return true;
			if (delta_g > params.m_reject_thresholds[1])
				return true;
			if (delta_b > params.m_reject_thresholds[2])
				return true;

			if (num_comps == 4)
			{
				uint32_t delta_a = abs((int)trial_color[3] - (int)orig_color[3]);
				if (delta_a > params.m_reject_thresholds[3])
					return true;
			}
		}
	}

	return false;
}

static inline int compute_png_match_dist(int xa, int ya, int xb, int yb, int width, int height, int num_comps)
{
	return (xa * num_comps + (ya * (width * num_comps + 1))) - (xb * num_comps + (yb * (width * num_comps + 1)));
}

static void find_optimal1(
	color_rgba& best_delta_color, float& best_bits, float& best_squared_err, float& best_t, uint32_t& best_type,
	uint32_t x, uint32_t y,
	const image& orig_img, const image& coded_img, const image& delta_img,
	float lambda, const huffman_encoding_table& h0, const huffman_encoding_table& h1, 
	const vector2D<float>& smooth_block_mse_scales,
	uint32_t filter, uint32_t num_comps, const rdo_png_level *pLevel, const rdo_png_params &params)
{
	const uint32_t width = orig_img.get_width(), height = orig_img.get_height();

	color_rgba orig_color(orig_img(x, y));
	color_rgba orig_delta_color(png_predict(orig_color, x, y, coded_img, filter, num_comps));

	best_delta_color = orig_delta_color;
	best_bits = (float)(h0.get_code_sizes()[best_delta_color[0]] + h0.get_code_sizes()[best_delta_color[1]] + h0.get_code_sizes()[best_delta_color[2]]);
	if (num_comps == 4)
		best_bits += (float)h0.get_code_sizes()[best_delta_color[3]];

	best_t = best_bits * lambda;
	best_squared_err = 0;
	best_type = 0;

	if (!params.m_match_only)
	{
		bool all_zero = true;
		if (orig_delta_color.r + orig_delta_color.g + orig_delta_color.b)
			all_zero = false;
		if ((num_comps == 4) && orig_delta_color.a)
			all_zero = false;

		if (!all_zero)
		{
			for (uint32_t t = 1; t < ((num_comps == 4) ? 16U : 8U); t++)
			{
				color_rgba delta_color(orig_delta_color);
				for (uint32_t c = 0; c < num_comps; c++)
				{
					if (t & (1 << c))
					{
						int8_t v = (int8_t)delta_color[c];
						if (v < 0)
							delta_color[c]++;
						else if (v > 0)
							delta_color[c]--;
					}
				}

				color_rgba trial_coded_color(png_unpredict(delta_color, x, y, coded_img, filter, num_comps));

				if (!should_reject(trial_coded_color, orig_color, num_comps, params))
				{
					float mse = compute_se(trial_coded_color, orig_color, num_comps, params);
					float bits = (float)(h0.get_code_sizes()[delta_color[0]] + h0.get_code_sizes()[delta_color[1]] + h0.get_code_sizes()[delta_color[2]]);
					if (num_comps == 4)
						bits += (float)h0.get_code_sizes()[delta_color[3]];

					float trial_t = smooth_block_mse_scales(x, y) * mse + bits * lambda;
					if (trial_t < best_t)
					{
						best_delta_color = delta_color;
						best_t = trial_t;
						best_bits = bits;
						best_squared_err = mse;
						best_type = 1;
					}
				}
			}
		}
	}

	for (int yd = 0; yd < (int)pLevel->m_num_scanlines_to_check; yd++)
	{
		if (((int)y - yd) < 0)
			break;

		int x_start, x_end;
		const int total_passes = ((yd == 1) && !pLevel->m_exhaustive_search) ? 2 : 1;
		for (int pass = 0; pass < total_passes; pass++)
		{
			if (pLevel->m_exhaustive_search)
			{
				x_end = yd ? ((int)width - 1) : ((int)x - 1);
				x_start = 0;
			}
			else
			{
				if (!yd)
				{
					if (x < 1)
						continue;

					x_start = maximum<int>((int)x - pLevel->m_search_dist * 2, 0);
					x_end = maximum<int>((int)x - 1, 0);
				}
				else if ((yd == 1) && (pass == 0))
				{
					if (width <= (uint32_t)pLevel->m_search_dist*2)
						continue;

					x_start = maximum<int>((int)width - pLevel->m_search_dist, 0);
					x_end = width - 1;
				}
				else
				{
					x_start = maximum<int>((int)x - pLevel->m_search_dist, 0);
					x_end = minimum<int>((int)x + pLevel->m_search_dist, (int)width - 1);
				}
			}

			for (int xd = x_end; xd >= x_start; xd--)
			{
				assert(xd < (int)width);
				assert((yd != 0) || (xd < (int)x));

				const uint32_t match_dist = compute_png_match_dist(x, y, xd, y - yd, width, height, num_comps);
				assert(match_dist >= 3);

				color_rgba delta_color(delta_img(xd, y - yd));

				color_rgba trial_coded_color(png_unpredict(delta_color, x, y, coded_img, filter, num_comps));
				
				float mse = compute_se(trial_coded_color, orig_img(x, y), num_comps, params);
				float bits = (float)compute_match_cost(match_dist, num_comps, h0, h1);
				float trial_t = smooth_block_mse_scales(x, y) * mse + bits * lambda;
				if (trial_t < best_t)
				{
					if (!should_reject(trial_coded_color, orig_color, num_comps, params))
					{
						best_delta_color = delta_img(xd, y - yd);
						best_t = trial_t;
						best_bits = bits;
						best_squared_err = mse;
						best_type = 2;
					}
				}

			} // xd

		} // pass

	} // yd
}

static void find_optimal_n(
	int n,
	color_rgba* pBest_delta_colors, float& best_bits, float& best_squared_err, float& best_t, 
	uint32_t x, uint32_t y,
	const image& orig_img, image& coded_img, const image& delta_img,
	float lambda, const huffman_encoding_table& h0, const huffman_encoding_table& h1, 
	const vector2D<float>& smooth_block_mse_scales,
	uint32_t filter, uint32_t num_comps, const rdo_png_level *pLevel, const rdo_png_params &params)
{
	assert(n >= 1 && n <= MAX_DELTA_COLORS);
	const uint32_t width = orig_img.get_width(), height = orig_img.get_height();
	const float oon = 1.0f / (float)n;
	
	for (int yd = 0; yd < (int)pLevel->m_num_scanlines_to_check; yd++)
	{
		if (((int)y - yd) < 0)
			break;

		int x_start, x_end;
		const int total_passes = ((yd == 1) && !pLevel->m_exhaustive_search) ? 2 : 1;
		for (int pass = 0; pass < total_passes; pass++)
		{
			if (pLevel->m_exhaustive_search)
			{
				x_end = yd ? ((int)width - n) : ((int)x - n);
				x_start = 0;
			}
			else
			{
				if (!yd)
				{
					if ((int)x < n)
						continue;

					x_start = maximum<int>((int)x - pLevel->m_search_dist * 2, 0);
					x_end = maximum<int>((int)x - n, 0);
				}
				else if ((yd == 1) && (pass == 0))
				{
					if (width <= (uint32_t)pLevel->m_search_dist * 2)
						continue;

					x_start = maximum<int>((int)width - pLevel->m_search_dist, 0);
					x_end = width - n;
				}
				else
				{
					x_start = maximum<int>((int)x - pLevel->m_search_dist, 0);
					x_end = minimum<int>((int)x + pLevel->m_search_dist, (int)width - n);
				}
			}

			for (int xd = x_end; xd >= x_start; xd--)
			{
				assert((xd + n - 1) < (int)width);
				assert((yd != 0) || ((xd + n - 1) < (int)x));

				const uint32_t match_dist = compute_png_match_dist(x, y, xd, y - yd, width, height, num_comps);
				assert(match_dist >= 3);

				color_rgba delta_color[MAX_DELTA_COLORS];
				for (uint32_t i = 0; i < (uint32_t)n; i++)
					delta_color[i] = delta_img(xd + i, y - yd);

				color_rgba trial_coded_color[MAX_DELTA_COLORS];
				for (uint32_t i = 0; i < (uint32_t)n; i++)
				{
					trial_coded_color[i] = png_unpredict(delta_color[i], x + i, y, coded_img, filter, num_comps);
					coded_img(x + i, y) = trial_coded_color[i];
				}

				float se = 0.0f;
				for (uint32_t i = 0; i < (uint32_t)n; i++)
					se += compute_se(trial_coded_color[i], orig_img(x + i, y), num_comps, params);

				float mse = se * oon;

				float bits = (float)compute_match_cost(match_dist, n * num_comps, h0, h1);

				float mse_scale = 0.0f;
				for (uint32_t i = 0; i < (uint32_t)n; i++)
					mse_scale = maximum(mse_scale, smooth_block_mse_scales(x + i, y));

				float trial_t = mse_scale * mse + bits * lambda;
				if (trial_t < best_t)
				{
					bool reject_flag = false;
					for (uint32_t i = 0; i < (uint32_t)n; i++)
					{
						if (should_reject(trial_coded_color[i], orig_img(x + i, y), num_comps, params))
						{
							reject_flag = true;
							break;
						}
					}
					if (!reject_flag)
					{
						for (uint32_t i = 0; i < (uint32_t)n; i++)
							pBest_delta_colors[i] = delta_color[i];

						best_t = trial_t;
						best_bits = bits;
						best_squared_err = se;
					}
				}
			} // xd

		} // pass
	
	} // yd
}

static float compute_image_metrics(const image& a, const image& b, uint32_t num_comps, float& y_psnr, bool print)
{
	image_metrics im;
	im.calc(a, b, 0, 3);
	if (print)
		im.print("RGB    ");

	float psnr = im.m_psnr;

	if (num_comps == 4)
	{
		im.calc(a, b, 0, 4);
		if (print)
			im.print("RGBA   ");

		psnr = im.m_psnr;
	}

	if (print)
	{
		im.calc(a, b, 0, 1);
		im.print("R      ");

		im.calc(a, b, 1, 1);
		im.print("G      ");

		im.calc(a, b, 2, 1);
		im.print("B      ");

		if (num_comps == 4)
		{
			im.calc(a, b, 3, 1);
			im.print("A      ");
		}
	}

	im.calc(a, b, 0, 0);
	if (print)
		im.print("Y 709  ");

	y_psnr = im.m_psnr;

	if (print)
		printf("\n");
		
	return psnr;
}

static float compute_normal_map_image_metrics(const image& enc_img, const image& orig_img, bool print_flag, const rdo_png_params& params)
{
	float max_err = -1e+9f, min_err = 1e+9f;

	double total_err = 0.0f, total_err2 = 0.0f;
	running_stat len_a_stats, len_b_stats;

	uint32_t total_invalid_a = 0, total_invalid_b = 0;

	const float INVALID_LEN_THRESHOLD = .4f;

	for (uint32_t y = 0; y < orig_img.get_height(); y++)
	{
		for (uint32_t x = 0; x < orig_img.get_width(); x++)
		{
			const color_rgba& ca = enc_img(x, y);
			const color_rgba& cb = orig_img(x, y);
						
			vec3F caf(decode_normal(ca, params));
			vec3F cbf(decode_normal(cb, params));
			
			float len_a = caf.length();
			len_a_stats.push(len_a);
			
			if (len_a < INVALID_LEN_THRESHOLD)
				total_invalid_a++;
			if (len_a > (1.0f + INVALID_LEN_THRESHOLD))
				total_invalid_a++;

			if (len_a != 0)
				caf /= len_a;

			float len_b = cbf.length();
			len_b_stats.push(len_b);

			if (len_b < INVALID_LEN_THRESHOLD)
				total_invalid_b++;

			if (len_b != 0)
				cbf /= len_b;

			float dot = clamp<float>(caf.dot(cbf), -1.0f, 1.0f);
			
			float err_degrees = acosf(dot) * RAD_TO_DEG;

			max_err = maximum(max_err, err_degrees);
			min_err = minimum(min_err, err_degrees);
			total_err += err_degrees;
			total_err2 += err_degrees * err_degrees;
		} // x
	} // y

	const double total_pixels = (double)orig_img.get_total_pixels();

	if (print_flag)
	{
		printf("Total apparently invalid (len < %3.3f or >%3.3f): Encoded: %u Original: %u\n", INVALID_LEN_THRESHOLD, INVALID_LEN_THRESHOLD + 1.0f, total_invalid_a, total_invalid_b);
		printf("Length statistics: Encoded: Avg %3.3f Std Dev %3.3f, Original: Avg %3.3f Std Dev: %3.3f\n",
			len_a_stats.get_mean(), len_a_stats.get_std_dev(),
			len_b_stats.get_mean(), len_b_stats.get_std_dev());

		printf("Angular error:\n");
		printf("Minimum: %3.3f degrees\nMaximum: %3.3f degrees\n", min_err, max_err);
		printf("Average: %3.3f degrees\n", total_err / total_pixels);
		printf("Std Dev: %3.3f degrees\n", sqrt(total_pixels * total_err2 - total_err * total_err) / total_pixels);
	}

	const double rms_error = sqrt(total_err2 / total_pixels);
	
	if (print_flag)
	{
		printf("RMS:     %3.3f degrees\n\n", rms_error);
	}

	return (float)rms_error;
}

struct find_optimal_hash_key
{
	uint32_t m_x_ofs;
	color_rgba m_prev_delta_colors[MAX_DELTA_COLORS];

	operator size_t() const
	{
		return hash_hsieh((const uint8_t*)&m_x_ofs, sizeof(m_x_ofs) + sizeof(color_rgba) * m_x_ofs);
	}

	bool operator== (const find_optimal_hash_key& rhs) const
	{
		if (m_x_ofs != rhs.m_x_ofs)
			return false;
		for (uint32_t i = 0; i < m_x_ofs; i++)
			if (m_prev_delta_colors[i] != rhs.m_prev_delta_colors[i])
				return false;
		return true;
	}
};

struct find_optimal_hash_value
{
	color_rgba m_delta_colors[MAX_DELTA_COLORS];
	float m_bits;
	float m_t;
	float m_squared_err;
};

typedef basisu::hash_map<find_optimal_hash_key, find_optimal_hash_value> find_optimal_hash_map;

static color_rgba get_match_len_color(uint32_t l)
{
	color_rgba c = g_black_color;
	switch (l)
	{
	case 1: c.set(255, 0, 0, 255); break;
	case 2: c.set(255, 255, 0, 255); break;
	case 3: c.set(255, 0, 255, 255); break;
	case 4: c.set(128, 128, 128, 255); break;
	case 5: c.set(255, 128, 255, 255); break;
	case 6: c.set(255, 255, 128, 255); break;
	case 7: c.set(255, 128, 0, 255); break;
	case 8: c.set(255, 64, 64, 255); break;
	case 9: c.set(255, 255, 64, 255); break;
	case 10: c.set(64, 64, 255, 255); break;
	case 11: c.set(255, 64, 255, 255); break;
	case 12: c.set(255, 255, 255, 255); break;
	}
	return c;
}

static void eval_matches(int m, 
	uint32_t num_match_order, const match_order *pMatch_order,
	int x, int y, 
	float &best_t, float &best_se, float &best_bits, color_rgba *best_delta_color, uint32_t &best_idx,
	find_optimal_hash_map* pFind_optimal_hashers,
	int filter,
	float lambda, 
	const image& orig_img,
	image& delta_img,
	image& coded_img,
	const huffman_encoding_table &h0, 
	const huffman_encoding_table& h1,
	const vector2D<float> &smooth_block_mse_scales, uint32_t num_comps, const rdo_png_level *pLevel, const rdo_png_params &params)
{
	assert(pMatch_order[0].v[1] == m);

	best_t = 1e+9f;
	best_se = 1e+9f;
	best_bits = 1e+9f;
	best_idx = 0;

	float mse_smooth_factor = 0;
	for (uint32_t i = 0; i < (uint32_t)m; i++)
		mse_smooth_factor = maximum(mse_smooth_factor, smooth_block_mse_scales(x + i, y));

	for (uint32_t i = 0; i < (uint32_t)m; i++)
		pFind_optimal_hashers[i].reset();
	
	for (uint32_t i = 0; i < num_match_order; i++)
	{
		const uint32_t n = pMatch_order[i].v[0];

		color_rgba delta_color[MAX_DELTA_COLORS];
		float bits[MAX_DELTA_COLORS], st[MAX_DELTA_COLORS], squared_err[MAX_DELTA_COLORS];
		for (uint32_t j = 0; j < (uint32_t)m; j++)
		{
			bits[j] = 1e+9f;
			st[j] = 1e+9f;
			squared_err[j] = 1e+9;
		}

		uint32_t x_ofs = 0;
		for (uint32_t j = 0; j < n; j++)
		{
			const uint32_t len = pMatch_order[i].v[j + 1];
			assert((int)len <= m);

			st[j] = 1e+9f;
														
			if (len == 1)
			{
				uint32_t best_type;

				find_optimal_hash_key k;
				k.m_x_ofs = x_ofs;
				for (uint32_t q = 0; q < x_ofs; q++)
					k.m_prev_delta_colors[q] = delta_img(x + q, y);

				auto find_res = pFind_optimal_hashers[0].find(k);
				if (find_res != pFind_optimal_hashers[0].end())
				{
					const find_optimal_hash_value& v = find_res->second;

					delta_color[j] = v.m_delta_colors[0];
					bits[j] = v.m_bits;
					st[j] = v.m_t;
					squared_err[j] = v.m_squared_err;
				}
				else
				{
					find_optimal1(
						delta_color[j], bits[j], squared_err[j], st[j], best_type,
						x + x_ofs, y,
						orig_img, coded_img, delta_img,
						lambda, h0, h1, 
						smooth_block_mse_scales, filter, num_comps, pLevel, params);

					find_optimal_hash_value v;
					v.m_delta_colors[0] = delta_color[j];
					v.m_bits = bits[j];
					v.m_t = st[j];
					v.m_squared_err = squared_err[j];

					pFind_optimal_hashers[0].insert(k, v);
				}
			}
			else
			{
				find_optimal_hash_key k;
				k.m_x_ofs = x_ofs;
				for (uint32_t q = 0; q < x_ofs; q++)
					k.m_prev_delta_colors[q] = delta_img(x + q, y);

				auto find_res = pFind_optimal_hashers[len - 1].find(k);
				if (find_res != pFind_optimal_hashers[len - 1].end())
				{
					const find_optimal_hash_value& v = find_res->second;

					for (uint32_t q = 0; q < len; q++)
						delta_color[j + q] = v.m_delta_colors[q];

					bits[j] = v.m_bits;
					st[j] = v.m_t;
					squared_err[j] = v.m_squared_err;
				}
				else
				{
					find_optimal_n(len,
						delta_color + j, bits[j], squared_err[j], st[j],
						x + x_ofs, y,
						orig_img, coded_img, delta_img,
						lambda, h0, h1, 
						smooth_block_mse_scales, filter, num_comps, pLevel, params);

					find_optimal_hash_value v;
					for (uint32_t q = 0; q < len; q++)
						v.m_delta_colors[q] = delta_color[j + q];
					v.m_bits = bits[j];
					v.m_t = st[j];
					v.m_squared_err = squared_err[j];

					pFind_optimal_hashers[len - 1].insert(k, v);
				}
			}

			for (uint32_t k = 0; k < len; k++)
			{
				delta_img(x + x_ofs + k, y) = delta_color[j + k];
				coded_img(x + x_ofs + k, y) = png_unpredict(delta_color[j + k], x + x_ofs + k, y, coded_img, filter, num_comps);
			}

			x_ofs += len;
		}
		assert(x_ofs == m);

		float total_bits = 0.0f;
		float total_se = 0.0f;
		for (uint32_t j = 0; j < n; j++)
		{
			total_bits += bits[j];
			total_se += squared_err[j];
		}
		float mse = total_se / (float)m;
		float t = mse * mse_smooth_factor + total_bits * lambda;
		if (t < best_t)
		{
			best_t = t;
			best_idx = i;
			best_bits = total_bits;
			best_se = total_se;

			for (uint32_t k = 0; k < (uint32_t)m; k++)
			{
				best_delta_color[k] = delta_img(x + k, y);
			}

			if (mse == 0.0f)
				break;
		}

	} // i

	assert(best_t != 1e+9f);
}

static void create_smooth_maps(
	vector2D<float> &smooth_block_mse_scales,
	const image& orig_img,
	rdo_png_params &params)
{
	const uint32_t width = orig_img.get_width();
	const uint32_t height = orig_img.get_height();
	const uint32_t total_pixels = orig_img.get_total_pixels();
	const bool has_alpha = orig_img.has_alpha();
	const uint32_t num_comps = has_alpha ? 4 : 3;

	if (params.m_no_mse_scaling)
	{
		smooth_block_mse_scales.set_all(1.0f);
		return;
	}

	image smooth_vis(width, height);
	image alpha_edge_vis(width, height);
	image ultra_smooth_vis(width, height);

	for (uint32_t y = 0; y < height; y++)
	{
		for (uint32_t x = 0; x < width; x++)
		{
			float alpha_edge_yl = 0.0f;
			if ((num_comps == 4) && (params.m_alpha_is_opacity))
			{
				tracked_stat alpha_comp_stats;
				for (int yd = -3; yd <= 3; yd++)
				{
					for (int xd = -3; xd <= 3; xd++)
					{
						const color_rgba& p = orig_img.get_clamped((int)x + xd, (int)y + yd);
						alpha_comp_stats.update(p[3]);
					}
				}

				float max_std_dev = alpha_comp_stats.get_std_dev();

				float yl = clampf(max_std_dev / params.m_max_smooth_std_dev, 0.0f, 1.0f);
				alpha_edge_yl = yl * yl;
			}

			{
				tracked_stat comp_stats[4];
				for (int yd = -1; yd <= 1; yd++)
				{
					for (int xd = -1; xd <= 1; xd++)
					{
						const color_rgba& p = orig_img.get_clamped((int)x + xd, (int)y + yd);
						comp_stats[0].update(p[0]);
						comp_stats[1].update(p[1]);
						comp_stats[2].update(p[2]);
						if (num_comps == 4)
							comp_stats[3].update(p[3]);
					}
				}

				float max_std_dev = 0.0f;
				for (uint32_t i = 0; i < num_comps; i++)
					max_std_dev = std::max(max_std_dev, comp_stats[i].get_std_dev());

				float yl = clampf(max_std_dev / params.m_max_smooth_std_dev, 0.0f, 1.0f);
				yl = yl * yl;

				smooth_block_mse_scales(x, y) = lerp(params.m_smooth_max_mse_scale, 1.0f, yl);

				if (num_comps == 4)
				{
					alpha_edge_vis(x, y).set((int)std::round(alpha_edge_yl * 255.0f));

					smooth_block_mse_scales(x, y) = lerp(smooth_block_mse_scales(x, y), params.m_smooth_max_mse_scale, alpha_edge_yl);
				}

				smooth_vis(x, y).set(clamp((int)((smooth_block_mse_scales(x, y) - 1.0f) / (params.m_smooth_max_mse_scale - 1.0f) * 255.0f + .5f), 0, 255));
			}

			{
				tracked_stat comp_stats[4];

				const int S = 5;
				for (int yd = -S; yd < S; yd++)
				{
					for (int xd = -S; xd < S; xd++)
					{
						const color_rgba& p = orig_img.get_clamped((int)x + xd, (int)y + yd);
						comp_stats[0].update(p[0]);
						comp_stats[1].update(p[1]);
						comp_stats[2].update(p[2]);
						if (num_comps == 4)
							comp_stats[3].update(p[3]);
					}
				}

				float max_std_dev = 0.0f;
				for (uint32_t i = 0; i < num_comps; i++)
					max_std_dev = std::max(max_std_dev, comp_stats[i].get_std_dev());

				float yl = clampf(max_std_dev / params.m_max_ultra_smooth_std_dev, 0.0f, 1.0f);
				yl = powf(yl, 3.0f);

				smooth_block_mse_scales(x, y) = lerp(params.m_ultra_smooth_max_mse_scale, smooth_block_mse_scales(x, y), yl);

				ultra_smooth_vis(x, y).set((int)std::round(yl * 255.0f));
			}

		}
	}

	if (params.m_debug_images)
	{
		save_png("dbg_smooth_vis.png", smooth_vis);
		save_png("dbg_alpha_edge_vis.png", alpha_edge_vis);
		save_png("dbg_ultra_smooth_vis.png", ultra_smooth_vis);
	}
}

static bool rdo_png(rdo_png_params &params)
{
	const image& orig_img = params.m_orig_img;
	
	const uint32_t width = orig_img.get_width();
	const uint32_t height = orig_img.get_height();
	const uint32_t total_pixels = orig_img.get_total_pixels();
	const bool has_alpha = orig_img.has_alpha();
	const uint32_t num_comps = has_alpha ? 4 : 3;
	
	if (params.m_debug_images)
	{
		g_use_miniz = false;
		save_png("dbg_orig.png", orig_img);
		g_use_miniz = true;
	}
		
	uint8_vec filters(height);
	filters.set_all(PNG_AVG_FILTER);

	for (uint32_t i = 0; i < 288; i++)
		buminiz::g_defl_freq[0][i] = 0;

	for (uint32_t i = 0; i < 32; i++)
		buminiz::g_defl_freq[1][i] = 0;

	uint8_vec orig_avg_png_file;
	save_png(orig_avg_png_file, orig_img, 0, 0, -1, filters.data());

	histogram ht0(288), ht1(32);
	for (uint32_t i = 0; i < 288; i++)
		ht0[i] = maximum<uint32_t>(1U, (uint32_t)buminiz::g_defl_freq[0][i]);

	for (uint32_t i = 0; i < 32; i++)
		ht1[i] = maximum<uint32_t>(1U, (uint32_t)buminiz::g_defl_freq[1][i]);

	if (params.m_debug_images)
	{
		write_vec_to_file("dbg_orig_avg.png", orig_avg_png_file);
	}
		
	if (params.m_debug_images)
	{
		if (has_alpha)
		{
			save_png("dbg_orig_rgb.png", orig_img, cImageSaveIgnoreAlpha);
			save_png("dbg_orig_alpha.png", orig_img, cImageSaveGrayscale, 3);
		}
	}
	
	const float lambda = params.m_lambda;
	
	// TODO
	const float max_smooth_std_dev = 35.0f;
	const float smooth_max_mse_scale = 250.0f;

	const float max_ultra_smooth_std_dev = 5.0f;
	const float ultra_smooth_max_mse_scale = 1500.0f;

	assert(params.m_level < MAX_LEVELS);
	const rdo_png_level* pLevel = &g_levels[params.m_level];

	const int skip_filter0 = 2;
				
	const uint32_t MAX_M = 6;
	
	const uint32_t M = pLevel->m_M;
	const uint32_t num_match_order_a = pLevel->m_num_match_order_a;
	const match_order* pMatch_order_a = pLevel->m_pMatch_order_a;

	const uint32_t num_match_order_b = pLevel->m_num_match_order_b;
	const match_order* pMatch_order_b = pLevel->m_pMatch_order_b;
	
	image match_vis(width, height);

	vector2D<float> smooth_block_mse_scales(width, height);

	if (params.m_print_progress)
	{
		printf("Stage 1\n");
	}

	create_smooth_maps(
		smooth_block_mse_scales,
		orig_img,
		params);
			
	uint64_t comp_size = 0;

	const uint32_t num_encoder_passes = params.m_two_pass ? 2 : 1;
	
	image delta_img(width, height);
	image coded_img(width, height);

	for (uint32_t encoder_pass = 0; encoder_pass < num_encoder_passes; encoder_pass++)
	{
		if ((params.m_print_progress) && (num_encoder_passes > 1))
			printf("\n**** Pass %u\n", encoder_pass + 1);
		
		if (encoder_pass)
		{
			delta_img.set_all(g_black_color);
			coded_img.set_all(g_black_color);
		}
		
		huffman_encoding_table h0, h1;
		h0.init(ht0, 15);
		h1.init(ht1, 15);

		if (params.m_print_debug_output)
		{
			printf("Literal table:\n");
			for (uint32_t i = 0; i < 288; i++)
			{
				printf("%2u ", h0.get_code_sizes()[i]);
				if ((i & 15) == 15)
					printf("\n");
			}
			printf("\n");

			printf("Distance table:\n");
			for (uint32_t i = 0; i < 32; i++)
			{
				printf("%2u ", h1.get_code_sizes()[i]);
				if ((i & 15) == 15)
					printf("\n");
			}
			printf("\n");
		}

		uint32_t filter_hist[5];
		clear_obj(filter_hist);

		uint32_t match_len_hist[MAX_DELTA_COLORS + 1];
		clear_obj(match_len_hist);

		assert(num_match_order_a <= 256 && num_match_order_b <= 256);

		uint32_t type_hist_a[256];
		clear_obj(type_hist_a);

		uint32_t type_hist_b[256];
		clear_obj(type_hist_b);

		find_optimal_hash_map find_optimal_hashers[MAX_DELTA_COLORS];
		for (uint32_t i = 0; i < MAX_DELTA_COLORS; i++)
			find_optimal_hashers[i].reserve(4);

		uint32_t total_match_a = 0, total_match_b = 0;

		if (params.m_print_progress)
		{
			printf("Stage 2\n");
		}

		for (uint32_t y = 0; y < height; y++)
		{
			if (params.m_print_progress)
			{
				if ((y & 15) == 0)
				{
					printf("\b\b\b\b\b\b\b\b%3.2f%%", y * 100.0f / height);
					fflush(stdout);
				}
			}

			float best_scanline_t = 1e+9f;
			float best_scanline_err = 1e+9f;
			uint32_t best_filter = 0;
			std::vector<color_rgba> best_delta_pixels(width);
			std::vector<color_rgba> best_coded_pixels(width);

			for (uint32_t filter = pLevel->m_first_filter; filter <= pLevel->m_last_filter; filter++)
			{
				if ((int)filter == skip_filter0)
					continue;

				float total_squared_err = 0.0f;
				float total_bits = 0;

				if (pLevel->m_double_width)
				{
					uint32_t x = 0;
					while (x < width)
					{
						if ((x + M * 2) > width)
						{
							color_rgba best_delta_color;
							float best_bits, best_t, best_squared_err;
							uint32_t best_type;

							find_optimal1(best_delta_color, best_bits, best_squared_err, best_t, best_type,
								x, y,
								orig_img, coded_img, delta_img,
								lambda, h0, h1,
								smooth_block_mse_scales, filter, num_comps, pLevel, params);

							delta_img(x, y) = best_delta_color;
							coded_img(x, y) = png_unpredict(best_delta_color, x, y, coded_img, filter, num_comps);

							total_squared_err += compute_se(coded_img(x, y), orig_img(x, y), num_comps, params);
							total_bits += best_bits;

							match_len_hist[1]++;

							if (best_type == 0)
								match_vis(x, y).set(0, 255, 0, 255);
							else if (best_type == 1)
								match_vis(x, y).set(255, 255, 0, 255);
							else
								match_vis(x, y).set(255, 255, 255, 255);

							x++;
						}
						else
						{
							float best_t[3], best_se[3], best_bits[3];
							uint32_t best_idx[3];
							color_rgba best_delta_color[3][MAX_M * 2];

							for (uint32_t o = 0; o < 2; o++)
							{
								eval_matches(M,
									num_match_order_a, pMatch_order_a,
									x + o * M, y,
									best_t[o], best_se[o], best_bits[o], best_delta_color[o], best_idx[o],
									find_optimal_hashers,
									filter,
									lambda,
									orig_img,
									delta_img,
									coded_img,
									h0,
									h1,
									smooth_block_mse_scales, num_comps, pLevel, params);

								for (uint32_t k = 0; k < M; k++)
								{
									delta_img(x + o * M + k, y) = best_delta_color[o][k];
									coded_img(x + o * M + k, y) = png_unpredict(best_delta_color[o][k], x + o * M + k, y, coded_img, filter, num_comps);
								}
							}

							eval_matches(M * 2,
								num_match_order_b, pMatch_order_b,
								x, y,
								best_t[2], best_se[2], best_bits[2], best_delta_color[2], best_idx[2],
								find_optimal_hashers,
								filter,
								lambda,
								orig_img,
								delta_img,
								coded_img,
								h0,
								h1,
								smooth_block_mse_scales, num_comps, pLevel, params);

							float overall_mse_smooth_factor = 0;
							for (uint32_t i = 0; i < M * 2; i++)
								overall_mse_smooth_factor = maximum(overall_mse_smooth_factor, smooth_block_mse_scales(x + i, y));

							float best_se_a = best_se[0] + best_se[1];
							float best_mse_a = best_se_a * (1.0f / (float)(M * 2));
							float best_bits_a = best_bits[0] + best_bits[1];
							float best_t_a = best_mse_a * overall_mse_smooth_factor + best_bits_a * lambda;

							if (best_t_a < best_t[2])
							{
								total_match_a++;
								total_bits += best_bits_a;

								for (uint32_t o = 0; o < 2; o++)
								{
									for (uint32_t k = 0; k < M; k++)
									{
										delta_img(x + o * M + k, y) = best_delta_color[o][k];
										coded_img(x + o * M + k, y) = png_unpredict(best_delta_color[o][k], x + o * M + k, y, coded_img, filter, num_comps);

										total_squared_err += compute_se(coded_img(x + o * M + k, y), orig_img(x + o * M + k, y), num_comps, params);
									}

									const uint32_t n = pMatch_order_a[best_idx[o]].v[0];
									int x_ofs = 0;
									for (uint32_t i = 0; i < n; i++)
									{
										uint32_t l = pMatch_order_a[best_idx[o]].v[1 + i];

										match_len_hist[l]++;

										color_rgba c = get_match_len_color(l);
										for (uint32_t j = 0; j < l; j++)
											match_vis(x + o * M + x_ofs + j, y) = c;

										x_ofs += l;
									}

									assert(best_idx[o] < num_match_order_a);
									type_hist_a[best_idx[o]]++;
								}
							}
							else
							{
								total_match_b++;
								total_bits += best_bits[2];

								for (uint32_t k = 0; k < M * 2; k++)
								{
									delta_img(x + k, y) = best_delta_color[2][k];
									coded_img(x + k, y) = png_unpredict(best_delta_color[2][k], x + k, y, coded_img, filter, num_comps);

									total_squared_err += compute_se(coded_img(x + k, y), orig_img(x + k, y), num_comps, params);
								}

								const uint32_t n = pMatch_order_b[best_idx[2]].v[0];
								int x_ofs = 0;
								for (uint32_t i = 0; i < n; i++)
								{
									uint32_t l = pMatch_order_b[best_idx[2]].v[1 + i];

									match_len_hist[l]++;

									color_rgba c = get_match_len_color(l);
									for (uint32_t j = 0; j < l; j++)
										match_vis(x + x_ofs + j, y) = c;

									x_ofs += l;
								}

								assert(best_idx[2] < num_match_order_b);
								type_hist_b[best_idx[2]]++;
							}

							x += M * 2;
						}

						assert(x <= width);
					} // while (x < width)
				}
				else
				{
					uint32_t x = 0;
					while (x < width)
					{
						if ((x + M) > width)
						{
							color_rgba best_delta_color;
							float best_bits, best_t, best_squared_err;
							uint32_t best_type;

							find_optimal1(best_delta_color, best_bits, best_squared_err, best_t, best_type,
								x, y,
								orig_img, coded_img, delta_img,
								lambda, h0, h1,
								smooth_block_mse_scales, filter, num_comps, pLevel, params);

							delta_img(x, y) = best_delta_color;
							coded_img(x, y) = png_unpredict(best_delta_color, x, y, coded_img, filter, num_comps);

							total_squared_err += compute_se(coded_img(x, y), orig_img(x, y), num_comps, params);
							total_bits += best_bits;

							match_len_hist[1]++;

							if (best_type == 0)
								match_vis(x, y).set(0, 255, 0, 255);
							else if (best_type == 1)
								match_vis(x, y).set(255, 255, 0, 255);
							else
								match_vis(x, y).set(255, 255, 255, 255);

							x++;
						}
						else
						{
							float best_t, best_se, best_bits;
							uint32_t best_idx;
							color_rgba best_delta_color[MAX_M];

							eval_matches(M,
								num_match_order_a, pMatch_order_a,
								x, y,
								best_t, best_se, best_bits, best_delta_color, best_idx,
								find_optimal_hashers,
								filter,
								lambda,
								orig_img,
								delta_img,
								coded_img,
								h0,
								h1,
								smooth_block_mse_scales, num_comps, pLevel, params);

							for (uint32_t k = 0; k < M; k++)
							{
								delta_img(x + k, y) = best_delta_color[k];
								coded_img(x + k, y) = png_unpredict(best_delta_color[k], x + k, y, coded_img, filter, num_comps);

								total_squared_err += compute_se(coded_img(x + k, y), orig_img(x + k, y), num_comps, params);
							}

							total_match_a++;
							total_bits += best_bits;

							const uint32_t n = pMatch_order_a[best_idx].v[0];
							int x_ofs = 0;
							for (uint32_t i = 0; i < n; i++)
							{
								uint32_t l = pMatch_order_a[best_idx].v[1 + i];

								match_len_hist[l]++;

								color_rgba c = get_match_len_color(l);
								for (uint32_t j = 0; j < l; j++)
									match_vis(x + x_ofs + j, y) = c;

								x_ofs += l;
							}
							assert(x_ofs == M);

							assert(best_idx < num_match_order_a);
							type_hist_a[best_idx]++;

							x += M;
						}

						assert(x <= width);
					} // while (x < width)
				}

				float scanline_t = (total_squared_err / width) + total_bits * lambda;

				// TODO - what to default to?
				//if (scanline_t < best_scanline_t)
				if (total_squared_err < best_scanline_err)
				{
					best_scanline_t = scanline_t;
					best_scanline_err = total_squared_err;
					best_filter = filter;
					memcpy(best_delta_pixels.data(), &delta_img(0, y), width * sizeof(color_rgba));
					memcpy(best_coded_pixels.data(), &coded_img(0, y), width * sizeof(color_rgba));
				}

			} // filter

			memcpy(&delta_img(0, y), best_delta_pixels.data(), width * sizeof(color_rgba));
			memcpy(&coded_img(0, y), best_coded_pixels.data(), width * sizeof(color_rgba));
			filters[y] = (uint8_t)best_filter;
			filter_hist[best_filter]++;

		} //y
		
		if (params.m_print_progress)
		{
			printf("\b\b\b\b\b\b\b\b        \b\b\b\b\b\b\b\b\n");
			fflush(stdout);
		}

		if (params.m_print_debug_output)
		{
			printf("Total match_a: %u match_b: %u\n", total_match_a, total_match_b);
			printf("\n");

			printf("Filter hist:\n");
			for (uint32_t i = 1; i <= 4; i++)
				printf("%u %u\n", i, filter_hist[i]);
			printf("\n");

			printf("Match len hist:\n");
			for (uint32_t i = 1; i <= MAX_DELTA_COLORS; i++)
				printf("%u: %u\n", i, match_len_hist[i]);
			printf("\n");

			printf("Match order A hist:\n");
			for (uint32_t i = 0; i < num_match_order_a; i++)
				printf("%u: %u\n", i, type_hist_a[i]);
			printf("\n");

			printf("Match order B hist:\n");
			for (uint32_t i = 0; i < num_match_order_b; i++)
				printf("%u: %u\n", i, type_hist_b[i]);
			printf("\n");

			char buf[256];
			sprintf(buf, "dbg_match_vis_%u.png", encoder_pass);
			save_png(buf, match_vis);

			sprintf(buf, "dbg_delta_img_%u.png", encoder_pass);
			save_png(buf, delta_img);
		}

		if (encoder_pass == (num_encoder_passes - 1))
		{
			g_use_miniz = false;
			save_png(params.m_output_file_data, coded_img, 0, 0, -1, filters.data(), &comp_size);
			g_use_miniz = true;

			params.m_output_image = coded_img;
		}
		else
		{
			for (uint32_t i = 0; i < 288; i++)
				buminiz::g_defl_freq[0][i] = 0;

			for (uint32_t i = 0; i < 32; i++)
				buminiz::g_defl_freq[1][i] = 0;

			save_png("pass0_output_miniz.png", coded_img, 0, 0, -1, filters.data(), &comp_size);

			for (uint32_t i = 0; i < 288; i++)
				ht0[i] = maximum<uint32_t>(1U, (uint32_t)buminiz::g_defl_freq[0][i]);

			for (uint32_t i = 0; i < 32; i++)
				ht1[i] = maximum<uint32_t>(1U, (uint32_t)buminiz::g_defl_freq[1][i]);

			g_use_miniz = false;
			save_png("pass0_output.png", coded_img, 0, 0, -1, filters.data(), &comp_size);
			g_use_miniz = true;
		}

		if (has_alpha)
		{
			if (params.m_debug_images)
			{
				char buf[256];
				sprintf(buf, "dbg_coded_rgb_%u.png", encoder_pass);
				save_png(buf, coded_img, cImageSaveIgnoreAlpha, 0);
				
				sprintf(buf, "dbg_coded_alpha_%u.png", encoder_pass);
				save_png(buf, coded_img, cImageSaveGrayscale, 3);
			}
		}
				
		params.m_psnr = compute_image_metrics(coded_img, orig_img, num_comps, params.m_y_psnr, params.m_print_stats);
		if ((params.m_normal_map) || (params.m_print_normal_map_metrics))
			params.m_angular_rms_error = compute_normal_map_image_metrics(coded_img, orig_img, params.m_print_stats, params);
		
		params.m_bpp = (comp_size * 8.0f) / (float)total_pixels;

		if (params.m_print_stats)
		{
			printf("Compressed file size: %llu, Bitrate: %3.3f bits/pixel, RGB(A) Effectiveness: %3.3f PSNR per bits/pixel, Y: %3.3f PSNR per bits/pixel\n",
				(unsigned long long)comp_size,
				params.m_bpp,
				params.m_psnr / params.m_bpp,
				params.m_y_psnr / params.m_bpp);
		}

		if (params.m_debug_images)
		{
			image recovered_img(width, height);
			for (uint32_t y = 0; y < height; y++)
				for (uint32_t x = 0; x < width; x++)
					recovered_img(x, y) = png_unpredict(delta_img(x, y), x, y, recovered_img, filters[y], num_comps);

			char buf[256];
			sprintf(buf, "dbg_unpredicted_%u.png", encoder_pass);
			save_png(buf, recovered_img);
		}

	} // encoder_pass
	
	return true;
}

#define QOI_IMPLEMENTATION
#include "qoi.h"

#pragma pack(push, 1)
struct qoi_header
{
	char magic[4]; // magic bytes "qoif"
	uint32_t width; // image width in pixels (BE)
	uint32_t height; // image height in pixels (BE)
	uint8_t channels; // 3 = RGB, 4 = RGBA
	uint8_t colorspace; // 0 = sRGB with linear alpha 1 = all channels linear
};
#pragma pack(pop)

static void encode_qoi(const image& img, uint8_vec& data)
{
	color_rgba hash[64];
	clear_obj(hash);

	data.resize(0);

	qoi_header hdr;
	memcpy(hdr.magic, "qoif", 4);
	hdr.width = byteswap_32(img.get_width());
	hdr.height = byteswap_32(img.get_height());
	hdr.channels = img.has_alpha() ? 4 : 3;
	hdr.colorspace = 0;
	data.resize(sizeof(hdr));
	memcpy(data.data(), &hdr, sizeof(hdr));

	int prev_r = 0, prev_g = 0, prev_b = 0, prev_a = 255;
	uint32_t cur_run_len = 0;

	for (uint32_t y = 0; y < img.get_height(); y++)
	{
		for (uint32_t x = 0; x < img.get_width(); x++)
		{
			const color_rgba& c = img(x, y);

			if ((c.r == prev_r) && (c.g == prev_g) && (c.b == prev_b) && (c.a == prev_a))
			{
				cur_run_len++;
				if (cur_run_len == 62)
				{
					data.push_back(0xC0 | (cur_run_len - 1));
					cur_run_len = 0;
				}
				continue;
			}

			if (cur_run_len)
			{
				data.push_back((64 + 128) | (cur_run_len - 1));
				cur_run_len = 0;
			}

			uint32_t hash_idx = (c.r * 3 + c.g * 5 + c.b * 7 + c.a * 11) & 63;
			color_rgba& hash_color = hash[hash_idx];

			if (c == hash_color)
			{
				data.push_back(hash_idx);
			}
			else
			{
				hash[hash_idx] = c;

				int dr = ((int)c.r - prev_r + 2) & 255;
				int dg = ((int)c.g - prev_g + 2) & 255;
				int db = ((int)c.b - prev_b + 2) & 255;

				if (c.a == prev_a)
				{
					if ((dr <= 3) && (dg <= 3) && (db <= 3))
					{
						data.push_back(64 + (dr << 4) + (dg << 2) + db);
					}
					else
					{
						int g_diff = (int)c.g - prev_g;

						dg = (g_diff + 32) & 255;

						dr = (((int)c.r - prev_r) - g_diff + 8) & 255;
						db = (((int)c.b - prev_b) - g_diff + 8) & 255;

						if ((dg <= 63) && (dr <= 15) && (db <= 15))
						{
							data.push_back((uint8_t)(128 + dg));
							data.push_back((uint8_t)((dr << 4) | db));
						}
						else
						{
							data.push_back(254);
							data.push_back((uint8_t)c.r);
							data.push_back((uint8_t)c.g);
							data.push_back((uint8_t)c.b);
						}
					}
				}
				else
				{
					data.push_back(255);
					data.push_back((uint8_t)c.r);
					data.push_back((uint8_t)c.g);
					data.push_back((uint8_t)c.b);
					data.push_back((uint8_t)c.a);
				}
			}

			prev_r = c.r;
			prev_g = c.g;
			prev_b = c.b;
			prev_a = c.a;
		}
	}

	if (cur_run_len)
	{
		data.push_back((64 + 128) | (cur_run_len - 1));
		cur_run_len = 0;
	}

	for (uint32_t i = 0; i < 7; i++)
		data.push_back(0);
	data.push_back(1);
}

static bool encode_rdo_qoi(
	const image& orig_img,
	uint8_vec& data,
	const rdo_png_params& params,
	const vector2D<float>& smooth_block_mse_scales,
	float lambda)
{
	// This function wasn't designed to deal with lambda=0, so nudge it up.
	lambda = maximum(lambda, .0000125f);

	const bool has_alpha = orig_img.has_alpha();
	uint32_t num_comps = has_alpha ? 4 : 3;

	color_rgba hash[64];
	clear_obj(hash);

	data.resize(0);

	qoi_header hdr;
	memcpy(hdr.magic, "qoif", 4);
	hdr.width = byteswap_32(orig_img.get_width());
	hdr.height = byteswap_32(orig_img.get_height());
	hdr.channels = has_alpha ? 4 : 3;
	hdr.colorspace = 0;
	data.resize(sizeof(hdr));
	memcpy(data.data(), &hdr, sizeof(hdr));

	int prev_r = 0, prev_g = 0, prev_b = 0, prev_a = 255;
	uint32_t cur_run_len = 0;

	enum commands_t
	{
		cRUN,
		cIDX,
		cDELTA,
		cLUMA,
		cRGB,
		cRGBA,
	};

	uint32_t total_run = 0, total_rgb = 0, total_rgba = 0, total_index = 0, total_delta = 0, total_luma = 0, total_run_pixels = 0;

	for (uint32_t y = 0; y < orig_img.get_height(); y++)
	{
		if (params.m_print_progress)
		{
			if ((y & 15) == 0)
			{
				printf("\b\b\b\b\b\b\b\b%3.2f%%", y * 100.0f / orig_img.get_height());
				fflush(stdout);
			}
		}

		for (uint32_t x = 0; x < orig_img.get_width(); x++)
		{
			const color_rgba& c = orig_img(x, y);
			const float mse_scale = smooth_block_mse_scales(x, y);

			float best_mse = 0.0f;
			float best_bits = 40.0f;
			float best_t = best_mse + best_bits * lambda;
			int best_command = cRGBA;
			int best_index = 0, best_dr = 0, best_dg = 0, best_db = 0;

			{
				color_rgba trial_c(c.r, c.g, c.b, prev_a);
				if (!should_reject(trial_c, c, 4, params))
				{
					float mse = compute_se(trial_c, c, 4, params);
					float bits = 32.0f;
					float trial_t = mse_scale * mse + bits * lambda;
					if (trial_t < best_t)
					{
						best_mse = mse;
						best_bits = bits;
						best_t = trial_t;
						best_command = cRGB;
					}
				}
			}

			{
				color_rgba trial_c(prev_r, prev_g, prev_b, prev_a);
				if (!should_reject(trial_c, c, 4, params))
				{
					float mse = compute_se(trial_c, c, 4, params);
					float bits = cur_run_len ? 0 : 8.0f;
					float trial_t = mse_scale * mse + bits * lambda;
					if (trial_t < best_t)
					{
						best_mse = mse;
						best_bits = bits;
						best_t = trial_t;
						best_command = cRUN;

						if (best_mse == 0.0f)
						{
							cur_run_len++;
							if (cur_run_len == 62)
							{
								total_run_pixels += cur_run_len;

								data.push_back(0xC0 | (cur_run_len - 1));
								cur_run_len = 0;

								total_run++;
							}

							hash[(prev_r * 3 + prev_g * 5 + prev_b * 7 + prev_a * 11) & 63].set(prev_r, prev_g, prev_b, prev_a);

							continue;
						}
					}
				}
			}

			if (8.0f * lambda < best_t)
			{
				uint32_t hash_idx = (c.r * 3 + c.g * 5 + c.b * 7 + c.a * 11) & 63;
				
				// First try the INDEX command losslessly.
				if (c == hash[hash_idx])
				{
					float bits = 8.0f;
					float trial_t = bits * lambda;

					assert(trial_t < best_t);

					best_mse = 0.0f;
					best_bits = bits;
					best_t = trial_t;
					best_command = cIDX;
					best_index = hash_idx;
				}
				else
				{
					// Try a lossy INDEX command.
					for (uint32_t i = 0; i < 64; i++)
					{
						if (!should_reject(hash[i], c, 4, params))
						{
							float mse = compute_se(hash[i], c, 4, params);
							float bits = 8.0f;
							float trial_t = mse_scale * mse + bits * lambda;
							if (trial_t < best_t)
							{
								best_mse = mse;
								best_bits = bits;
								best_t = trial_t;
								best_command = cIDX;
								best_index = i;
							}
						}
					}
				}
			}

			if (8.0f * lambda < best_t)
			{
				bool delta_encodable_losslessly = false;

				// First try the DELTA command losslessly.
				if (c.a == prev_a)
				{
					int dr = ((int)c.r - prev_r + 2) & 255;
					int dg = ((int)c.g - prev_g + 2) & 255;
					int db = ((int)c.b - prev_b + 2) & 255;
					
					if ((dr <= 3) && (dg <= 3) && (db <= 3))
					{
						delta_encodable_losslessly = true;

						float bits = 8.0f;
						float trial_t = bits * lambda;

						assert(trial_t < best_t);
												
						best_mse = 0.0f;
						best_bits = bits;
						best_t = trial_t;
						best_command = cDELTA;
						best_dr = dr - 2;
						best_dg = dg - 2;
						best_db = db - 2;
					}
				}

				// Try a lossy DELTA command.
				if (!delta_encodable_losslessly)
				{
					for (uint32_t i = 0; i < 64; i++)
					{
						int dr = ((i >> 4) & 3) - 2;
						int dg = ((i >> 2) & 3) - 2;
						int db = (i & 3) - 2;

						color_rgba trial_c((prev_r + dr) & 255, (prev_g + dg) & 255, (prev_b + db) & 255, prev_a);

						if (!should_reject(trial_c, c, 4, params))
						{
							float mse = compute_se(trial_c, c, 4, params);
							float bits = 8.0f;
							float trial_t = mse_scale * mse + bits * lambda;

							if (trial_t < best_t)
							{
								best_mse = mse;
								best_bits = bits;
								best_t = trial_t;
								best_command = cDELTA;
								best_dr = dr;
								best_dg = dg;
								best_db = db;
							}
						}
					}
				}
			}

			if (16.0f * lambda < best_t)
			{
				bool luma_encodable_losslessly_in_rgb = false;

				// First try the LUMA command losslessly in RGB (may not be lossy in alpha).
				{
					int g_diff = (int)c.g - prev_g;

					int dg = (g_diff + 32) & 255;

					int dr = (((int)c.r - prev_r) - g_diff + 8) & 255;
					int db = (((int)c.b - prev_b) - g_diff + 8) & 255;

					if ((dg <= 63) && (dr <= 15) && (db <= 15))
					{
						luma_encodable_losslessly_in_rgb = true;

						color_rgba trial_c(c.r, c.g, c.b, prev_a);

						if (!should_reject(trial_c, c, 4, params))
						{
							float mse = compute_se(trial_c, c, 4, params);
							float bits = 16.0f;
							float trial_t = mse_scale * mse + bits * lambda;

							if (trial_t < best_t)
							{
								best_mse = mse;
								best_bits = bits;
								best_t = trial_t;
								best_command = cLUMA;
								best_dr = dr - 8;
								best_dg = dg - 32;
								best_db = db - 8;
							}
						}
					}
				}

				// If we can't use it losslessly, try it lossy.
				if ((!luma_encodable_losslessly_in_rgb) && (params.m_speed_mode != cFastestSpeed))
				{
					if (params.m_speed_mode == cNormalSpeed)
					{
						// Search all encodable LUMA commands.
						for (uint32_t i = 0; i < 16384; i++)
						{
							int dr = ((i >> 6) & 15) - 8;
							int dg = (i & 63) - 32;
							int db = ((i >> 10) & 15) - 8;

							color_rgba trial_c((prev_r + dg + dr) & 255, (prev_g + dg) & 255, (prev_b + dg + db) & 255, prev_a);

							if (!should_reject(trial_c, c, 4, params))
							{
								float mse = compute_se(trial_c, c, 4, params);
								float bits = 16.0f;
								float trial_t = mse_scale * mse + bits * lambda;

								if (trial_t < best_t)
								{
									best_mse = mse;
									best_bits = bits;
									best_t = trial_t;
									best_command = cLUMA;
									best_dr = dr;
									best_dg = dg;
									best_db = db;
								}
							}
						}
					}
					else
					{
						// TODO: This isn't very smart. What if the G delta is encodable but R and/or B aren't?
						const int g_deltas[] = { -24, -16, -14, -12, -10, -8, -6, -4, -3, -2, -1, 0, 1, 2, 3, 4, 6, 8, 10, 12, 14, 16, 24 };
						const int TOTAL_G_DELTAS = sizeof(g_deltas) / sizeof(g_deltas[0]);

						for (int kg = 0; kg < TOTAL_G_DELTAS; kg++)
						{
							const int dg = g_deltas[kg];
							for (uint32_t i = 0; i < 256; i++)
							{
								int dr = (i & 15) - 8;
								int db = ((i >> 4) & 15) - 8;

								color_rgba trial_c((prev_r + dg + dr) & 255, (prev_g + dg) & 255, (prev_b + dg + db) & 255, prev_a);

								if (!should_reject(trial_c, c, 4, params))
								{
									float mse = compute_se(trial_c, c, 4, params);
									float bits = 16.0f;
									float trial_t = mse_scale * mse + bits * lambda;

									if (trial_t < best_t)
									{
										best_mse = mse;
										best_bits = bits;
										best_t = trial_t;
										best_command = cLUMA;
										best_dr = dr;
										best_dg = dg;
										best_db = db;
									}
								}
							}
						}
					}
				}
			}

			switch (best_command)
			{
			case cRUN:
			{
				cur_run_len++;
				if (cur_run_len == 62)
				{
					total_run_pixels += cur_run_len;

					data.push_back(0xC0 | (cur_run_len - 1));
					cur_run_len = 0;

					total_run++;
				}

				hash[(prev_r * 3 + prev_g * 5 + prev_b * 7 + prev_a * 11) & 63].set(prev_r, prev_g, prev_b, prev_a);

				break;
			}
			case cRGB:
			{
				if (cur_run_len)
				{
					total_run_pixels += cur_run_len;

					data.push_back(0xC0 | (cur_run_len - 1));
					cur_run_len = 0;

					total_run++;
				}

				data.push_back(254);
				data.push_back((uint8_t)c.r);
				data.push_back((uint8_t)c.g);
				data.push_back((uint8_t)c.b);
				hash[(c.r * 3 + c.g * 5 + c.b * 7 + prev_a * 11) & 63].set(c.r, c.g, c.b, prev_a);
				prev_r = c.r;
				prev_g = c.g;
				prev_b = c.b;

				total_rgb++;

				break;
			}
			case cRGBA:
			{
				if (cur_run_len)
				{
					total_run_pixels += cur_run_len;

					data.push_back(0xC0 | (cur_run_len - 1));
					cur_run_len = 0;

					total_run++;
				}

				data.push_back(255);
				data.push_back((uint8_t)c.r);
				data.push_back((uint8_t)c.g);
				data.push_back((uint8_t)c.b);
				data.push_back((uint8_t)c.a);
				hash[(c.r * 3 + c.g * 5 + c.b * 7 + c.a * 11) & 63] = c;
				prev_r = c.r;
				prev_g = c.g;
				prev_b = c.b;
				prev_a = c.a;

				total_rgba++;

				break;
			}
			case cIDX:
			{
				if (cur_run_len)
				{
					total_run_pixels += cur_run_len;

					data.push_back(0xC0 | (cur_run_len - 1));
					cur_run_len = 0;

					total_run++;
				}

				data.push_back(best_index);

				prev_r = hash[best_index].r;
				prev_g = hash[best_index].g;
				prev_b = hash[best_index].b;
				prev_a = hash[best_index].a;

				total_index++;

				break;
			}
			case cDELTA:
			{
				if (cur_run_len)
				{
					total_run_pixels += cur_run_len;

					data.push_back(0xC0 | (cur_run_len - 1));
					cur_run_len = 0;

					total_run++;
				}

				assert(best_dr >= -2 && best_dr <= 1);
				assert(best_dg >= -2 && best_dg <= 1);
				assert(best_db >= -2 && best_db <= 1);

				data.push_back(64 + ((best_dr + 2) << 4) + ((best_dg + 2) << 2) + (best_db + 2));

				uint32_t decoded_r = (prev_r + best_dr) & 0xFF;
				uint32_t decoded_g = (prev_g + best_dg) & 0xFF;
				uint32_t decoded_b = (prev_b + best_db) & 0xFF;
				uint32_t decoded_a = prev_a;

				hash[(decoded_r * 3 + decoded_g * 5 + decoded_b * 7 + decoded_a * 11) & 63].set(decoded_r, decoded_g, decoded_b, decoded_a);

				prev_r = decoded_r;
				prev_g = decoded_g;
				prev_b = decoded_b;
				prev_a = decoded_a;

				total_delta++;

				break;
			}
			case cLUMA:
			{
				if (cur_run_len)
				{
					total_run_pixels += cur_run_len;

					data.push_back(0xC0 | (cur_run_len - 1));
					cur_run_len = 0;

					total_run++;
				}

				assert(best_dr >= -8 && best_dr <= 7);
				assert(best_dg >= -32 && best_dg <= 31);
				assert(best_db >= -8 && best_db <= 7);

				data.push_back((uint8_t)(128 + (best_dg + 32)));
				data.push_back((uint8_t)(((best_dr + 8) << 4) | (best_db + 8)));

				uint32_t decoded_r = (prev_r + best_dr + best_dg) & 0xFF;
				uint32_t decoded_g = (prev_g + best_dg) & 0xFF;
				uint32_t decoded_b = (prev_b + best_db + best_dg) & 0xFF;
				uint32_t decoded_a = prev_a;

				hash[(decoded_r * 3 + decoded_g * 5 + decoded_b * 7 + decoded_a * 11) & 63].set(decoded_r, decoded_g, decoded_b, decoded_a);

				prev_r = decoded_r;
				prev_g = decoded_g;
				prev_b = decoded_b;
				prev_a = decoded_a;

				total_luma++;

				break;
			}
			default:
			{
				assert(0);
				break;
			}
			}

		}
	}

	if (params.m_print_progress)
	{
		printf("\b\b\b\b\b\b\b\b        \b\b\b\b\b\b\b\b\n");
		fflush(stdout);
	}

	if (cur_run_len)
	{
		total_run_pixels += cur_run_len;

		data.push_back((64 + 128) | (cur_run_len - 1));
		cur_run_len = 0;

		total_run++;
	}

	for (uint32_t i = 0; i < 7; i++)
		data.push_back(0);
	data.push_back(1);

	if (params.m_print_stats)
	{
		printf("Totals: Run: %u, Run Pixels: %u %3.2f%%, RGB: %u %3.2f%%, RGBA: %u %3.2f%%, INDEX: %u %3.2f%%, DELTA: %u %3.2f%%, LUMA: %u %3.2f%%\n\n",
			total_run,
			total_run_pixels, (total_run_pixels * 100.0f) / orig_img.get_total_pixels(),
			total_rgb, (total_rgb * 100.0f) / orig_img.get_total_pixels(),
			total_rgba, (total_rgba * 100.0f) / orig_img.get_total_pixels(),
			total_index, (total_index * 100.0f) / orig_img.get_total_pixels(),
			total_delta, (total_delta * 100.0f) / orig_img.get_total_pixels(),
			total_luma, (total_luma * 100.0f) / orig_img.get_total_pixels());
	}

	return true;
}

static bool rdo_qoi(rdo_png_params& params)
{
	const image& orig_img = params.m_orig_img;

	const uint32_t width = orig_img.get_width();
	const uint32_t height = orig_img.get_height();
	const uint32_t total_pixels = orig_img.get_total_pixels();
	const bool has_alpha = orig_img.has_alpha();
	const uint32_t num_comps = has_alpha ? 4 : 3;
	
	vector2D<float> smooth_block_mse_scales(width, height);

	float lambda = params.m_lambda;

	if (params.m_debug_images)
	{
		g_use_miniz = false;
		save_png("dbg_orig.png", orig_img);
		g_use_miniz = true;
	}

	int ref_qoi_len = 0;
	qoi_desc ref_qoi_desc;
	ref_qoi_desc.width = orig_img.get_width();
	ref_qoi_desc.height = orig_img.get_height();
	ref_qoi_desc.channels = 4;
	ref_qoi_desc.colorspace = 0;
	void* pRef_qoi_data = qoi_encode(orig_img.get_ptr(), &ref_qoi_desc, &ref_qoi_len);
	if (params.m_debug_images)
		write_data_to_file("dbg_orig.qoi", pRef_qoi_data, ref_qoi_len);
	free(pRef_qoi_data);

	if (params.m_print_stats)
		printf("Lossless QOI encoded size: %i bytes, Bitrate: %3.3f bits/pixel\n", ref_qoi_len, (ref_qoi_len * 8.0f) / total_pixels);

	create_smooth_maps(
		smooth_block_mse_scales,
		orig_img,
		params);

	if (!encode_rdo_qoi(
		orig_img,
		params.m_output_file_data,
		params,
		smooth_block_mse_scales,
		lambda))
	{
		return false;
	}
			
	const uint32_t rdo_qoi_len = params.m_output_file_data.size();

	qoi_desc rdo_qoi_desc;
	void* pDecoded_RDO_image = qoi_decode((const void*)params.m_output_file_data.data(), (int)rdo_qoi_len, &rdo_qoi_desc, 4);
	if (!pDecoded_RDO_image)
	{
		fprintf(stderr, "qoi_decode() failed!\n");
		return false;
	}

	image decoded_image((uint8_t*)pDecoded_RDO_image, rdo_qoi_desc.width, rdo_qoi_desc.height, 4);
	free(pDecoded_RDO_image);
	pDecoded_RDO_image = nullptr;

	if (params.m_debug_images)
	{
		save_png("dbg_coded.png", decoded_image);
		save_png("dbg_coded_rgb.png", decoded_image, cImageSaveIgnoreAlpha);
		save_png("dbg_coded_alpha.png", decoded_image, cImageSaveGrayscale, 3);
	}
	
	params.m_output_image = decoded_image;

	params.m_psnr = compute_image_metrics(decoded_image, orig_img, 4, params.m_y_psnr, params.m_print_stats);
	if ((params.m_normal_map) || (params.m_print_normal_map_metrics))
		params.m_angular_rms_error = compute_normal_map_image_metrics(decoded_image, orig_img, params.m_print_stats, params);

	params.m_bpp = (rdo_qoi_len * 8.0f) / total_pixels;

	if (params.m_print_stats)
	{
		printf("Compressed file size: %u bytes, Bitrate: %3.3f bits/pixel, RGB(A) Effectiveness: %3.3f PSNR per bits/pixel, Y: %3.3f PSNR per bits/pixel\n",
			rdo_qoi_len,
			params.m_bpp,
			params.m_psnr / params.m_bpp,
			params.m_y_psnr / params.m_bpp);
	}
		
	return true;
}

#include "lz4.h"
#include "lz4hc.h"

#pragma pack(push, 1)
struct lz4i_header
{
	char sig[4]; // signature bytes "lz4i"
	uint32_t width; // image width in pixels (BE)
	uint32_t height; // image height in pixels (BE)
	uint8_t channels; // 3 = RGB, 4 = RGBA
	uint8_t colorspace; // 0 = sRGB with linear alpha 1 = all channels linear
};
#pragma pack(pop)

static inline bool check_for_rejection(const uint8_t* pTrial_buf, const uint8_t* pOrig_buf, uint32_t num_pixels, uint32_t num_comps, const rdo_png_params& params)
{
	uint32_t ofs = 0;

	color_rgba o(0, 255), t(0, 255);
	for (uint32_t i = 0; i < num_pixels; i++)
	{
		t.r = pTrial_buf[ofs];
		t.g = pTrial_buf[ofs + 1];
		t.b = pTrial_buf[ofs + 2];
		if (num_comps == 4)
			t.a = pTrial_buf[ofs + 3];

		o.r = pOrig_buf[ofs];
		o.g = pOrig_buf[ofs + 1];
		o.b = pOrig_buf[ofs + 2];
		if (num_comps == 4)
			o.a = pOrig_buf[ofs + 3];

		if (should_reject(t, o, num_comps, params))
			return true;
		
		ofs += num_comps;
	}

	return false;
}

static inline float compute_mse(const uint8_t* pTrial_buf, const uint8_t* pOrig_buf, uint32_t num_pixels, uint32_t num_comps, const rdo_png_params &params)
{
	float total_se = 0.0f;

	uint32_t ofs = 0;
	
	color_rgba o(0, 255), t(0, 255);
	for (uint32_t i = 0; i < num_pixels; i++)
	{
		t.r = pTrial_buf[ofs];
		t.g = pTrial_buf[ofs + 1];
		t.b = pTrial_buf[ofs + 2];
		if (num_comps == 4)
			t.a = pTrial_buf[ofs + 3];

		o.r = pOrig_buf[ofs];
		o.g = pOrig_buf[ofs + 1];
		o.b = pOrig_buf[ofs + 2];
		if (num_comps == 4)
			o.a = pOrig_buf[ofs + 3];
				
		total_se += compute_se(t, o, num_comps, params);
		
		ofs += num_comps;
	}
	
	return total_se / num_pixels;
}

const uint32_t RDO_LZ4_PIXEL_QUANT = 4;
const uint32_t RDO_LZ4_MIN_MATCH_LEN_IN_BYTES = 4;

static bool insert_lz4_match(
	const image &orig_img, image &coded_img,
	int xi, int yi, int width, int height, 
	uint32_t insert_len_in_bytes, uint32_t dst_insert_ofs,
	int lookahead_size_in_bytes, int lookahead_size_in_pixels,
	const uint8_t *pOrig_buf, 
	uint8_t *pBest_buf, float &best_t, float &best_bits, float &best_mse, uint32_t& best_trial_len, int &best_trial_dist,
	int match_dist_to_favor, bool &used_favored_match_dist,
	float lambda, uint32_t num_comps,
	const vector2D<float>& smooth_block_mse_scales,
	const rdo_png_params &params)
{
	bool found_match = false;
	
	const bool exhaustive_search = false;

	int SCANLINES_TO_CHECK = 4;
	int search_dist = 16;

	if (params.m_speed_mode == cNormalSpeed)
	{
		SCANLINES_TO_CHECK = 8;
		search_dist = 64;
	}
	else if (params.m_speed_mode == cFasterSpeed)
	{
		SCANLINES_TO_CHECK = 4;
		search_dist = 16;
	}
	else if (params.m_speed_mode == cFastestSpeed)
	{
		SCANLINES_TO_CHECK = 2;
		search_dist = 8;
	}
		
	uint8_t initial_buf[RDO_LZ4_PIXEL_QUANT * 4];
	memcpy(initial_buf, pBest_buf, lookahead_size_in_bytes);
	
	best_t = 1e+9f;
	best_bits = 0.0f;
	best_mse = 0.0f;
	best_trial_len = 0;
	best_trial_dist = 0;
	used_favored_match_dist = false;

	const uint32_t first_pixel_ofs = dst_insert_ofs / num_comps;
	const uint32_t first_pixel_byte_ofs = first_pixel_ofs * num_comps;
		
	const uint32_t total_pixels = ((dst_insert_ofs + insert_len_in_bytes - 1) / num_comps) - first_pixel_ofs + 1;

	float mse_scale = 0.0f;
	for (uint32_t i = 0; i < (uint32_t)minimum<uint32_t>(total_pixels, width - xi); i++)
		mse_scale = maximum(mse_scale, smooth_block_mse_scales(xi + first_pixel_ofs + i, yi));
		
	for (int yd = 0; yd < (int)SCANLINES_TO_CHECK; yd++)
	{
		const int y = (int)yi - yd;
		if (y < 0)
			break;

		int x_start, x_end;
		const int total_passes = ((yd == 1) && !exhaustive_search) ? 2 : 1;
		for (int pass = 0; pass < total_passes; pass++)
		{
			const int n = total_pixels;

			if (exhaustive_search)
			{
				x_end = yd ? ((int)width - n) : ((int)xi - n);
				x_start = 0;
			}
			else
			{
				if (!yd)
				{
					if ((int)xi < n)
						continue;

					x_start = maximum<int>((int)xi - search_dist * 2, 0);
					x_end = maximum<int>((int)xi - n, 0);
				}
				else if ((yd == 1) && (pass == 0))
				{
					if (width <= (uint32_t)search_dist * 2)
						continue;

					x_start = maximum<int>((int)width - search_dist, 0);
					x_end = width - n;
				}
				else
				{
					x_start = maximum<int>((int)xi - search_dist, 0);
					x_end = minimum<int>((int)xi + search_dist, (int)width - n);
				}
			}

			for (int xd = x_end; xd >= x_start; xd--)
			{
				assert((xd + n - 1) < (int)width);
				assert((yd != 0) || ((xd + n - 1) < (int)xi));

				const uint32_t max_match_len_in_pixels = minimum<uint32_t>(x_end - xd + 1, RDO_LZ4_PIXEL_QUANT);

				uint8_t trial_buf[RDO_LZ4_PIXEL_QUANT * 4];
				memcpy(trial_buf, initial_buf, lookahead_size_in_bytes);

				uint32_t trial_buf_ofs = dst_insert_ofs;
				const uint32_t end_ofs = dst_insert_ofs + insert_len_in_bytes;

				uint32_t src_pix_ofs = 0;
				uint32_t cur_comp = dst_insert_ofs % num_comps;
				while ((trial_buf_ofs < end_ofs) && (src_pix_ofs < max_match_len_in_pixels))
				{
					const color_rgba& c = coded_img(xd + src_pix_ofs, y);

					while (cur_comp < num_comps)
					{
						assert((trial_buf_ofs % num_comps) == (cur_comp % num_comps));

						trial_buf[trial_buf_ofs++] = c[cur_comp];
						if (trial_buf_ofs == end_ofs)
							break;

						cur_comp++;
					}
					cur_comp = 0;

					src_pix_ofs++;
				}
				assert(trial_buf_ofs <= RDO_LZ4_PIXEL_QUANT * num_comps);

				const uint32_t actual_insert_len_in_bytes = trial_buf_ofs - dst_insert_ofs;

				if (actual_insert_len_in_bytes != insert_len_in_bytes)
					continue;

				if (check_for_rejection(trial_buf + first_pixel_byte_ofs, pOrig_buf + first_pixel_byte_ofs, total_pixels, num_comps, params))
					continue;

				float trial_mse = compute_mse(trial_buf + first_pixel_byte_ofs, pOrig_buf + first_pixel_byte_ofs, total_pixels, num_comps, params);

				int cur_match_dist = (int)(xi * num_comps + dst_insert_ofs + yi * width * num_comps) - (int)(xd * num_comps + (dst_insert_ofs % num_comps) + y * width * num_comps);

				assert(cur_match_dist >= (int)num_comps);

				float trial_bits = 24.0f;
				if ((dst_insert_ofs == 0) && (match_dist_to_favor != -1))
				{
					if (cur_match_dist == match_dist_to_favor)
						trial_bits = 0;
				}
				
				float trial_t = mse_scale * trial_mse + trial_bits * lambda;

				if (trial_t < best_t)
				{
					best_t = trial_t;
					best_bits = trial_bits;
					best_mse = trial_mse;
					memcpy(pBest_buf, trial_buf, lookahead_size_in_bytes);
					best_trial_len = actual_insert_len_in_bytes;
					best_trial_dist = cur_match_dist;
					found_match = true;
					used_favored_match_dist = (trial_bits == 0.0f);
				}

			} // xd
		
		} // pass

	} // yd

	return found_match;
}

static bool encode_rdo_lz4i(
	const image& orig_img,
	uint8_vec& data,
	const rdo_png_params& params,
	const vector2D<float>& smooth_block_mse_scales,
	float lambda)
{
	const uint32_t width = orig_img.get_width();
	const uint32_t height = orig_img.get_height();
	const uint32_t total_pixels = orig_img.get_total_pixels();
	const bool has_alpha = orig_img.has_alpha();
	const uint32_t num_comps = has_alpha ? 4 : 3;
	const uint32_t total_bytes = total_pixels * num_comps;

	image coded_img(width, height);

	const uint32_t lookahead_size_in_bytes = RDO_LZ4_PIXEL_QUANT * num_comps;

	uint8_t initial_match_flags[RDO_LZ4_PIXEL_QUANT * 4];
	memset(initial_match_flags, 0, sizeof(initial_match_flags));

	uint32_t match_order_hist[NUM_LZ4_MATCH_ORDER_12];
	clear_obj(match_order_hist);

	int match_dist_to_favor = -1;

	basisu::vector<uint_vec> future_matches(total_bytes);
	int_vec match_distances(total_bytes);
	match_distances.set_all(-1);

	for (int yi = 0; yi < (int)height; yi++)
	{
		if ((yi & 31) == 0)
			printf("%u\n", yi);

		int xi = 0;

		while (xi < (int)width)
		{
			const uint32_t lookahead_size_in_pixels = minimum<uint32_t>(RDO_LZ4_PIXEL_QUANT, width - xi);
			const uint32_t lookahead_size_in_bytes = lookahead_size_in_pixels * num_comps;

			if (lookahead_size_in_pixels * num_comps < RDO_LZ4_MIN_MATCH_LEN_IN_BYTES)
			{
				coded_img(xi, yi) = orig_img(xi, yi);
				xi++;
				continue;
			}

			float mse_scale = 0.0f;
			for (uint32_t i = 0; i < (uint32_t)lookahead_size_in_pixels; i++)
				mse_scale = maximum(mse_scale, smooth_block_mse_scales(xi + i, yi));

			uint8_t orig_buf[RDO_LZ4_PIXEL_QUANT * 4];
			uint32_t orig_buf_ofs = 0;
			for (uint32_t i = 0; i < lookahead_size_in_pixels; i++)
			{
				const color_rgba& c = orig_img(xi + i, yi);
				orig_buf[orig_buf_ofs++] = c.r;
				orig_buf[orig_buf_ofs++] = c.g;
				orig_buf[orig_buf_ofs++] = c.b;
				if (num_comps == 4)
					orig_buf[orig_buf_ofs++] = c.a;
			}

			float best_mse = 0.0f;
			float best_bits = (float)(lookahead_size_in_bytes * 8) + (lookahead_size_in_bytes >= 15 ? 16 : 8);
			float best_t = best_bits * lambda;
			int best_match_dist_end = -1;
			uint32_t best_match_order = NUM_LZ4_MATCH_ORDER_12 - 1;
			uint8_t best_buf[RDO_LZ4_PIXEL_QUANT * 4];
			memcpy(best_buf, orig_buf, lookahead_size_in_bytes);
			int best_distances[MAX_DELTA_COLORS];
			memset(best_distances, 0xFF, sizeof(best_distances));

			for (uint32_t match_order_index = 0; match_order_index < NUM_LZ4_MATCH_ORDER_12; match_order_index++)
			{
				const match_order& order = g_lz4_match_order_12_bytes[match_order_index];

				uint32_t total_matches = 0, total_match_len = 0, total_coded_matches = 0;

				uint8_t best_parse_buf[RDO_LZ4_PIXEL_QUANT * 4];
				memcpy(best_parse_buf, orig_buf, lookahead_size_in_bytes);
				int trial_match_dist_end = -1;
				
				int trial_distances[MAX_DELTA_COLORS];
				memset(trial_distances, 0xFF, sizeof(trial_distances));

				uint32_t dst_ofs = 0;
				
				for (uint32_t l = 0; l < order.v[0]; l++)
				{
					uint32_t len = order.v[1 + l];
					if (len > lookahead_size_in_bytes)
						goto skip;
					if ((dst_ofs + len) > lookahead_size_in_bytes)
						goto skip;

					if (len > 1)
					{
						float best_trial_t, best_trial_bits, best_trial_mse;
						uint32_t best_trial_len;
						int best_trial_dist;
						bool used_favored_match_dist;

						bool found_match = insert_lz4_match(
							orig_img, coded_img,
							xi, yi, width, height,
							len, dst_ofs,
							lookahead_size_in_bytes, lookahead_size_in_pixels,
							orig_buf,
							best_parse_buf, best_trial_t, best_trial_bits, best_trial_mse, best_trial_len, best_trial_dist,
							(dst_ofs == 0) ? match_dist_to_favor : -1, used_favored_match_dist,
							lambda, num_comps,
							smooth_block_mse_scales,
							params);

						if (found_match)
						{
							trial_distances[l] = best_trial_dist;

							if ((dst_ofs + len) == lookahead_size_in_bytes)
								trial_match_dist_end = best_trial_dist;

							total_matches++;
							total_match_len += best_trial_len;

							if (!used_favored_match_dist)
								total_coded_matches++;
						}
					}

					dst_ofs += len;
				}
				assert(dst_ofs == lookahead_size_in_bytes);

				if (total_matches)
				{
					float trial_mse = compute_mse(best_parse_buf, orig_buf, lookahead_size_in_pixels, num_comps, params);
					float trial_bits = total_coded_matches * 24.0f + (float)(lookahead_size_in_bytes - total_match_len) * 8.0f;
					float trial_t = mse_scale * trial_mse + trial_bits * lambda;

					if (trial_t < best_t)
					{
						best_t = trial_t;
						best_bits = trial_bits;
						best_mse = trial_mse;
						memcpy(best_buf, best_parse_buf, lookahead_size_in_bytes);
						best_match_order = match_order_index;
						best_match_dist_end = trial_match_dist_end;
						memcpy(best_distances, trial_distances, sizeof(best_distances));
					}
				}

			skip:;
			}

			match_order_hist[best_match_order]++;

			uint32_t ofs = 0;
			for (uint32_t i = 0; i < lookahead_size_in_pixels; i++)
			{
				color_rgba& c = coded_img(xi + i, yi);

				c.r = best_buf[ofs++];
				c.g = best_buf[ofs++];
				c.b = best_buf[ofs++];
				if (num_comps == 4)
					c.a = best_buf[ofs++];
			}
																		
			{
				const match_order& best_order = g_lz4_match_order_12_bytes[best_match_order];

				const uint32_t cur_ofs = (xi + yi * width) * num_comps;

				uint32_t dst_ofs = 0;

				for (uint32_t l = 0; l < best_order.v[0]; l++)
				{
					uint32_t len = best_order.v[1 + l];
					if (len > lookahead_size_in_bytes)
						break;
					if ((dst_ofs + len) > lookahead_size_in_bytes)
						break;

					if (len > 1)
					{
						for (uint32_t j = 0; j < len; j++)
						{
							uint32_t ofs = cur_ofs + dst_ofs + j;
							match_distances[ofs] = best_distances[l];

							if (best_distances[l] != -1)
							{
								future_matches[ofs - best_distances[l]].push_back(ofs);
							}
						}
					}
					else
					{
						assert(best_distances[l] == -1);
					}

					dst_ofs += len;
				}

			}

			xi += lookahead_size_in_pixels;

			match_dist_to_favor = best_match_dist_end;

		} // xi
	} // yi

	if (params.m_print_debug_output)
	{
		printf("Match order usage histogram:\n");
		for (uint32_t i = 0; i < NUM_LZ4_MATCH_ORDER_12; i++)
		{
			printf("%u: %u\n", i, match_order_hist[i]);
		}
	}

	if (params.m_debug_images)
	{
		save_png("dbg_before_refine.png", coded_img);
	}

#if 1
	uint8_vec orig_bytes, coded_bytes;
	orig_bytes.reserve(total_bytes);
	coded_bytes.reserve(total_bytes);
	
	for (uint32_t y = 0; y < height; y++)
	{
		for (uint32_t x = 0; x < width; x++)
		{
			const color_rgba& c = coded_img(x, y);
			coded_bytes.push_back(c.r);
			coded_bytes.push_back(c.g);
			coded_bytes.push_back(c.b);
			if (num_comps == 4)
				coded_bytes.push_back(c.a);

			const color_rgba& o = orig_img(x, y);
			orig_bytes.push_back(o.r);
			orig_bytes.push_back(o.g);
			orig_bytes.push_back(o.b);
			if (num_comps == 4)
				orig_bytes.push_back(o.a);
		}
	}

	for (uint32_t i = 0; i < total_bytes; i++)
	{
		int match_distance = match_distances[i];
		if (match_distance == -1)
			continue;

		if ((match_distance == 0) || (match_distance > (int)i))
		{
			assert(0);
			return false;
		}

		int cur_byte = coded_bytes[i];
		int match_byte = coded_bytes[i - match_distance];

		if (cur_byte != match_byte)
		{
			assert(0);
			return false;
		}
	}

	uint8_vec byte_processed_flags(total_bytes);
	//image processed_img(width, height);

	for (uint32_t i = 0; i < total_bytes; i++)
	{
		int match_distance = match_distances[i];
		if (match_distance == -1)
			continue;

		if (byte_processed_flags[i])
			continue;

		uint_vec byte_indices;
		uint_vec offset_stack;

		offset_stack.push_back(i);
		while (offset_stack.size())
		{
			uint32_t ofs = offset_stack.back();
			offset_stack.pop_back();

			assert(!byte_processed_flags[ofs]);
						
			assert(byte_indices.find(ofs) == -1);
			byte_indices.push_back(ofs);

			if (match_distances[ofs] != -1)
			{
				if (byte_indices.find(ofs - match_distances[ofs]) == -1)
					offset_stack.push_back(ofs - match_distances[ofs]);
				
				assert(coded_bytes[ofs] == coded_bytes[ofs - match_distances[ofs]]);
			}

			for (uint32_t i = 0; i < future_matches[ofs].size(); i++)
			{
				uint32_t future_ofs = future_matches[ofs][i];
				
				assert(coded_bytes[ofs] == coded_bytes[future_ofs]);

				if (byte_indices.find(future_ofs) == -1)
					offset_stack.push_back(future_ofs);
			}
		}
				
		uint32_t total_val = 0;

		for (uint32_t i = 0; i < byte_indices.size(); i++)
		{
			uint32_t ofs = byte_indices[i];

			uint32_t pixel_index = ofs / num_comps;
			uint32_t comp_index = ofs % num_comps;
			uint32_t x = pixel_index % width;
			uint32_t y = pixel_index / width;

			int orig_val = orig_bytes[ofs];
			//int coded_val = coded_bytes[ofs];

			total_val += orig_val;
		}

		const uint8_t new_val = (uint8_t)((total_val + (byte_indices.size() / 2)) / byte_indices.size());
		
		for (uint32_t i = 0; i < byte_indices.size(); i++)
		{
			uint32_t ofs = byte_indices[i];

			uint32_t pixel_index = ofs / num_comps;
			uint32_t comp_index = ofs % num_comps;
			uint32_t x = pixel_index % width;
			uint32_t y = pixel_index / width;
						
			assert(!byte_processed_flags[ofs]);
			coded_img(x, y).m_comps[comp_index] = new_val;

			byte_processed_flags[ofs] = true;
		}

		//printf("%u, %u %u %u\n", byte_indices.size(), vote_add, vote_sub, vote_unchanged);
	}
#endif

	if (params.m_debug_images)
	{
		save_png("dbg_before_dither.png", coded_img);
	}

	data.resize(0);

	lz4i_header hdr;
	memcpy(hdr.sig, "lz4i", 4);
	hdr.width = byteswap_32(orig_img.get_width());
	hdr.height = byteswap_32(orig_img.get_height());
	hdr.channels = (uint8_t)num_comps;
	hdr.colorspace = 0;
	data.resize(sizeof(hdr));
	memcpy(data.data(), &hdr, sizeof(hdr));

	uint8_vec bytes_to_compress;
	bytes_to_compress.reserve(width * height * num_comps);
	for (uint32_t y = 0; y < height; y++)
	{
		for (uint32_t x = 0; x < width; x++)
		{
			const color_rgba& c = coded_img(x, y);

			bytes_to_compress.push_back(c.r);
			bytes_to_compress.push_back(c.g);
			bytes_to_compress.push_back(c.b);
			if (num_comps == 4)
				bytes_to_compress.push_back(c.a);
		} // x
	} // y

	const size_t data_ofs = data.size();
	const int comp_bound = LZ4_compressBound(bytes_to_compress.size());
	data.resize(data_ofs + comp_bound);

	int lz4_size = LZ4_compress_HC((const char*)bytes_to_compress.data(), (char*)(data.data() + data_ofs), bytes_to_compress.size(), comp_bound, LZ4HC_CLEVEL_MAX);
	if (!lz4_size)
	{
		fprintf(stderr, "LZ4_compress_HC() failed!\n");
		return false;
	}
	
	data.resize(data_ofs + lz4_size);

	return true;
}

static bool decode_lz4i(const uint8_t *pData, size_t data_size, image &dst_img)
{
	if ((data_size > INT_MAX) || (data_size < (sizeof(lz4i_header) + 1)))
		return false;

	const lz4i_header* pHeader = reinterpret_cast<const lz4i_header*>(pData);
	if (memcmp(pHeader->sig, "lz4i", 4) != 0)
		return false;

	uint32_t width = byteswap_32(pHeader->width);
	uint32_t height = byteswap_32(pHeader->height);

	const uint32_t MAX_DIM = 65536 * 8;
	if ((width < 1) || (width > MAX_DIM) || (height < 1) || (height > MAX_DIM))
		return false;

	uint32_t num_comps = pHeader->channels;

	if ((num_comps < 3) || (num_comps > 4))
		return false;
		
	dst_img.resize(width, height);

	if (num_comps == 3)
	{
		uint8_vec decomp_buf(width * height * 3);

		interval_timer tm;
		double min_time = 1e+9f;
		int res;
		for (uint32_t i = 0; i < 10; i++)
		{
			tm.start();

			res = LZ4_decompress_safe((char*)pData + sizeof(lz4i_header), (char*)decomp_buf.data(), (int)(data_size - sizeof(lz4i_header)), (int)decomp_buf.size());
			if (res <= 0)
				return false;
			
			min_time = minimum(min_time, tm.get_elapsed_secs());
		}
		
		if (res != decomp_buf.size())
			return false;

		printf("Decompression rate: %3.3f megapixels/sec\n", ((double)(width * height) / min_time) / (1024.0f*1024.0f));
				
		const uint8_t* pSrc = decomp_buf.data();
		uint8_t* pDst = (uint8_t*)dst_img.get_ptr();
		
		const uint32_t total_pixels = dst_img.get_total_pixels();
		for (uint32_t t = 0; t < total_pixels; t++)
		{
			pDst[0] = pSrc[0];
			pDst[1] = pSrc[1];
			pDst[2] = pSrc[2];
			pDst[3] = 0xFF;
			pSrc += 3;
			pDst += 4;
		}
	}
	else
	{
		int res = LZ4_decompress_safe((char*)pData + sizeof(lz4i_header), (char*)dst_img.get_ptr(), (int)(data_size - sizeof(lz4i_header)), width * height * 4);
		if (res <= 0)
			return false;

		if (res != width * height * 4)
			return false;
	}

	return true;
}

static bool rdo_lz4i(rdo_png_params& params)
{
	const image before_processed_orig_img(params.m_orig_img);

	const image& orig_img = params.m_orig_img;

	const uint32_t width = orig_img.get_width();
	const uint32_t height = orig_img.get_height();
	const uint32_t total_pixels = orig_img.get_total_pixels();
	const bool has_alpha = orig_img.has_alpha();
	const uint32_t num_comps = has_alpha ? 4 : 3;

	vector2D<float> smooth_block_mse_scales(width, height);

	float lambda = params.m_lambda;

	if (params.m_debug_images)
		save_png("dbg_orig.png", orig_img);

	uint8_vec rgb_image;
	for (uint32_t y = 0; y < height; y++)
	{
		for (uint32_t x = 0; x < width; x++)
		{
			const color_rgba& c = orig_img(x, y);
			rgb_image.push_back(c.r);
			rgb_image.push_back(c.g);
			rgb_image.push_back(c.b);
		}
	}

	const uint8_t* pOrig_image_bytes = has_alpha ? (const uint8_t *)orig_img.get_ptr() : rgb_image.get_ptr();
	const uint32_t orig_image_len = orig_img.get_total_pixels() * num_comps;

	uint8_vec orig_image_compressed(LZ4_compressBound(orig_image_len));
	int lz4i_lossless_size = LZ4_compress_HC((const char *)pOrig_image_bytes, (char *)orig_image_compressed.data(), orig_image_len, orig_image_compressed.size(), LZ4HC_CLEVEL_MAX);
	if (!lz4i_lossless_size)
	{
		fprintf(stderr, "LZ4_compress_HC() failed!\n");
		return false;
	}
	orig_image_compressed.resize(lz4i_lossless_size);

	if (params.m_print_stats)
		printf("Lossless LZ4I encoded size: %i bytes, Bitrate: %3.3f bits/pixel\n", lz4i_lossless_size + (uint32_t)sizeof(lz4i_header), ((lz4i_lossless_size + (uint32_t)sizeof(lz4i_header)) * 8.0f) / total_pixels);

	create_smooth_maps(
		smooth_block_mse_scales,
		orig_img,
		params);

#if 0
	for (uint32_t y = 0; y < height; y++)
	{
		for (uint32_t x = 0; x < width; x++)
		{
			color_rgba &c = params.m_orig_img(x, y);

			const int R = 8;

			int diff[3] = { c.r % R, c.g % R, c.b % R };

			// 0 6
			// 4 2
			const int s_matrix[4] = { 0, 6, 4, 2 };
			int threshold = s_matrix[(x & 1) + (y & 1) * 2];

			uint32_t cur_ofs = (x + y * width) * num_comps;
						
			for (int d = 0; d < 3; d++)
			{
				if (diff[d] > threshold)
					c[d] = clamp255(((c[d] / R) + 1) * R);
				else
					c[d] = clamp255((c[d] / R) * R);
			}
		}
	}
	
	if (params.m_debug_images)
		save_png("dbg_orig_after_dither.png", params.m_orig_img);
#endif

	if (!encode_rdo_lz4i(
		orig_img,
		params.m_output_file_data,
		params,
		smooth_block_mse_scales,
		lambda))
	{
		return false;
	}

	const uint32_t rdo_lz4i_len = (uint32_t)params.m_output_file_data.size();

	image decoded_image;
	if (!decode_lz4i(params.m_output_file_data.data(), params.m_output_file_data.size(), decoded_image))
		return false;

	if (params.m_debug_images)
	{
		save_png("dbg_coded.png", decoded_image);
		save_png("dbg_coded_rgb.png", decoded_image, cImageSaveIgnoreAlpha);
		save_png("dbg_coded_alpha.png", decoded_image, cImageSaveGrayscale, 3);
	}

	params.m_output_image = decoded_image;

	params.m_psnr = compute_image_metrics(decoded_image, before_processed_orig_img, 4, params.m_y_psnr, params.m_print_stats);
	if ((params.m_normal_map) || (params.m_print_normal_map_metrics))
		params.m_angular_rms_error = compute_normal_map_image_metrics(decoded_image, before_processed_orig_img, params.m_print_stats, params);

	params.m_bpp = (rdo_lz4i_len * 8.0f) / total_pixels;

	if (params.m_print_stats)
	{
		printf("Compressed file size: %u bytes, Bitrate: %3.3f bits/pixel, RGB(A) Effectiveness: %3.3f PSNR per bits/pixel, Y: %3.3f PSNR per bits/pixel\n",
			rdo_lz4i_len,
			params.m_bpp,
			params.m_psnr / params.m_bpp,
			params.m_y_psnr / params.m_bpp);
	}

	return true;
}

static void print_help()
{
	printf("rdopng " RDO_PNG_VERSION "\n\n");

	printf("Usage: rdopng [options] input_file.png/bmp/tga/jpg\n\n");

	printf("-lambda X: Set quality level, value range is [0-100000], higher=smaller files/lower quality, default is 300\n");
	printf("-level X: Set parsing level, valid X range is [0-29], default is 0 (fastest/lowest quality/least effective)\n");
	printf("-two_pass: Compress image in two passes for significantly higher compression\n");
	printf("-linear: Use linear RGB(A) metrics instead of the default perceptual sRGB/Oklab metrics\n");
	printf("-normal: Normal map mode (linear metrics, print normal map statistics, angular error and rejection metrics)\n");
	printf("-snorm: Normal map texels use SNORM GPU encoding vs. UNORM\n");

	printf("\n");
	printf("-quiet: Suppress all output to stdout\n");
	printf("-no_progress: Suppress all progress related output\n");
	printf("-output X: Set output filename to X\n");
	printf("-debug: Debug output and images\n");
	printf("-no_cache: Compute the Oklab lookup table at startup instead of caching the table to disk in the executable's directory\n");
	printf("-unpack: Unpack .LZ4I file and save as a .PNG file\n");
	printf("-lz4i: Encode a .LZ4I file instead of a .PNG file\n");

	printf("\nQOI specific options:\n");
	printf("-qoi: Encode a .QOI file instead of a .PNG file\n");
	printf("-unpack_qoi_to_png: Unpack coded .QOI file and save as a .PNG file\n");

	printf("\nQOI/LZ4I specific options:\n");
	printf("-uber: Best LZ4I/QOI compression, but slowest\n");
	printf("-better: Better LZ4I/QOI compression\n");
	printf("-fastest: Fastest LZ4I/QOI compression (default)\n");
		
	printf("\nColor distance and parsing options:\n");
	printf("-wr X, -wg X, -wb X, -wa X: Sets individual R,G,B, or A color distance weights to X, valid X range is [0,256], default is 1 (only used in -linear mode)\n");
	printf("-wlab L a b Alpha: Set Lab and alpha relative color distance weights, must specify 4 floats, defaults are 2 1.5 1 2\n");
	printf("-match_only: Only try LZ matches, don't try searching for cheaper to code literals\n");

	printf("\nTransparency options:\n");
	printf("-rt: On 32bpp images, don't allow fully opaque pixels to become transparent, and don't allow fully transparent pixels to become opaque\n");
	printf("-no_alpha_opacity: Alpha channel does NOT represent transparency, so don't favor the quality of RGB edges near alpha edges\n");

	printf("\nMatch rejection options:\n");
	printf("-no_reject: Disable all match rejection\n");
	printf("-rl X: Set Oklab L reject threshold to X, valid X range is [0,1.0], default is .05, higher values=more allowed lightness error\n");
	printf("-rlab X: Set Oklab ab reject distance threshold to X, valid X range is [0,1.0], default is .05, higher values=more allowed chroma/hue error\n");
	printf("-rrgb X: Set RGB reject threshold value to X (only used in -linear mode), valid X range is [0,256], default is 32, higher values=higher max RGB error\n");
	printf("-rr X, -rg X, -rb, X, -ra X: Set individual R,G,B, or A reject threshold value to X (only used in -linear mode), valid X range is [0,256], default is 32, higher values=higher max alpha error\n");

	printf("\nPerceptual options:\n");
	printf("-no_mse_scaling: Disable MSE scaling on smooth/ultra-smooth image regions\n");
	printf("-max_smooth_std_dev: Set smooth region maximum standard RGB(A) deviation, default is 35\n");
	printf("-smooth_max_mse_scale: Set smooth region max MSE scale multiplier, default is 250 (PNG) or 2500 (QOI)\n");
	printf("-max_ultra_smooth_std_dev: Set ultra-smooth region maximum standard RGB(A) deviaton, default is 5\n");
	printf("-ultra_smooth_max_mse_scale: Set ultra-smooth region max MSE scale multiplier, default is 1500 (PNG) or 2500 (QOI)\n");
}

#if 0
int qoi_test(const image& orig_img)
{
	uint8_vec qoi_data;
	encode_qoi(orig_img, qoi_data);

	write_vec_to_file("mine.qoi", qoi_data);

	printf("My encoded size: %u\n", (uint32_t)qoi_data.size());

	int qoi_len = 0;
	qoi_desc desc;
	desc.width = orig_img.get_width();
	desc.height = orig_img.get_height();
	desc.channels = 4;
	desc.colorspace = 0;
	void *p = qoi_encode(orig_img.get_ptr(), &desc, &qoi_len);
	write_data_to_file("image.qoi", p, qoi_len);
	free(p);

	printf("QOI encoded size: %i\n", qoi_len);
			
	void* pImage = qoi_decode((const void *)qoi_data.data(), (int)qoi_data.size(), &desc, 4);
	if (!pImage)
	{
		fprintf(stderr, "qoi_decode() failed!\n");
		return EXIT_FAILURE;
	}

	image decoded_image((uint8_t *)pImage, desc.width, desc.height, 4);
	free(pImage);
	pImage = nullptr;

	if (decoded_image != orig_img)
	{
		fprintf(stderr, "Decode validation failed!\n");
	}

	save_png("decoded.png", decoded_image);
	save_png("decoded_rgb.png", decoded_image, cImageSaveIgnoreAlpha);
	save_png("decoded_alpha.png", decoded_image, cImageSaveGrayscale, 3);

	printf("OK\n");
			
	return EXIT_SUCCESS;
}
#endif

static void normalize_image(image& img, const rdo_png_params &params)
{
	image orig_img(img);

	for (uint32_t y = 0; y < img.get_height(); y++)
	{
		for (uint32_t x = 0; x < img.get_width(); x++)
		{
			color_rgba& c = img(x, y);

			vec3F cf(decode_normal(c, params));
			
			cf.normalize_in_place();
						
			c = encode_normal_exhaustive(cf, c.a, params);

		} // x
	} // y

	if (params.m_print_stats)
	{
		printf("\nResults after normalizing normal map:\n");
		compute_normal_map_image_metrics(img, orig_img, true, params);
	}
}

enum comp_mode
{
	cModePNG,
	cModeQOI,
	cModeLZ4I
};

int main(int arg_c, const char** arg_v)
{
#ifdef _DEBUG
	printf("DEBUG\n");
#endif
							
	int status = EXIT_FAILURE;

#if BASISU_CATCH_EXCEPTIONS
	try
	{
		rdo_png_params rp;
		std::string input_filename, output_filename;

		rp.m_print_stats = true;
		rp.m_print_progress = true;

		bool quiet_mode = false;
		bool caching_enabled = true;
		comp_mode mode = cModePNG;
		bool normalize_first = false;
		bool unpack_qoi_to_png = false;
		bool unpack_flag = false;

		float max_smooth_std_dev = -1.0f, smooth_max_mse_scale = -1.0f, max_ultra_smooth_std_dev = -1.0f, ultra_smooth_max_mse_scale = -1.0f;

		if (arg_c <= 1)
		{
			print_help();
			return EXIT_FAILURE;
		}
																
		int arg_index = 1;
		while (arg_index < arg_c)
		{
			const char* pArg = arg_v[arg_index];
			const int num_remaining_args = arg_c - (arg_index + 1);
			int arg_count = 1;

#define REMAINING_ARGS_CHECK(n) if (num_remaining_args < (n)) { error_printf("Error: Expected %u values to follow %s!\n", n, pArg); return EXIT_FAILURE; }

			if (strcasecmp(pArg, "-debug") == 0)
			{
				rp.m_debug_images = true;
				rp.m_print_debug_output = true;
			}
			else if (strcasecmp(pArg, "-no_cache") == 0)
			{
				caching_enabled = false;
			}
			else if (strcasecmp(pArg, "-quiet") == 0)
			{
				quiet_mode = true;
			}
			else if (strcasecmp(pArg, "-no_progress") == 0)
			{
				rp.m_print_progress = false;
			}
			else if (strcasecmp(pArg, "-rt") == 0)
			{
				rp.m_transparent_reject_test = true;
			}
			else if (strcasecmp(pArg, "-qoi") == 0)
			{
				mode = cModeQOI;
			}
			else if (strcasecmp(pArg, "-lz4i") == 0)
			{
				mode = cModeLZ4I;
			}
			else if (strcasecmp(pArg, "-unpack") == 0)
			{
				unpack_flag = true;
			}
			else if (strcasecmp(pArg, "-unpack_qoi_to_png") == 0)
			{
				unpack_qoi_to_png = true;
			}
			else if (strcasecmp(pArg, "-level") == 0)
			{
				REMAINING_ARGS_CHECK(1);
				rp.m_level = clamp<int>(atoi(arg_v[arg_index + 1]), 0, MAX_LEVELS);
				arg_count++;
			}
			else if (strcasecmp(pArg, "-lambda") == 0)
			{
				REMAINING_ARGS_CHECK(1);
				rp.m_lambda = clamp<float>((float)atof(arg_v[arg_index + 1]), 0.0f, 250000.0f);
				arg_count++;
			}
			else if (strcasecmp(pArg, "-no_mse_scaling") == 0)
			{
				rp.m_no_mse_scaling = true;
			}
			else if (strcasecmp(pArg, "-max_smooth_std_dev") == 0)
			{
				REMAINING_ARGS_CHECK(1);
				max_smooth_std_dev = clamp<float>((float)atof(arg_v[arg_index + 1]), 0.000125f, 250000.0f);
				arg_count++;
			}
			else if (strcasecmp(pArg, "-smooth_max_mse_scale") == 0)
			{
				REMAINING_ARGS_CHECK(1);
				smooth_max_mse_scale = clamp<float>((float)atof(arg_v[arg_index + 1]), 0.000125f, 250000.0f);
				arg_count++;
			}
			else if (strcasecmp(pArg, "-max_ultra_smooth_std_dev") == 0)
			{
				REMAINING_ARGS_CHECK(1);
				max_ultra_smooth_std_dev = clamp<float>((float)atof(arg_v[arg_index + 1]), 0.000125f, 250000.0f);
				arg_count++;
			}
			else if (strcasecmp(pArg, "-ultra_smooth_max_mse_scale") == 0)
			{
				REMAINING_ARGS_CHECK(1);
				ultra_smooth_max_mse_scale = clamp<float>((float)atof(arg_v[arg_index + 1]), 0.000125f, 250000.0f);
				arg_count++;
			}
			else if (strcasecmp(pArg, "-output") == 0)
			{
				REMAINING_ARGS_CHECK(1);
				output_filename = arg_v[arg_index + 1];
				arg_count++;
			}
			else if (strcasecmp(pArg, "-no_reject") == 0)
			{
				rp.m_reject_thresholds[0] = 256;
				rp.m_reject_thresholds[1] = 256;
				rp.m_reject_thresholds[2] = 256;
				rp.m_reject_thresholds[3] = 256;
				rp.m_use_reject_thresholds = false;
			}
			else if (strcasecmp(pArg, "-rrgb") == 0)
			{
				REMAINING_ARGS_CHECK(1);
				rp.m_reject_thresholds[0] = clamp<int>(atoi(arg_v[arg_index + 1]), 0, 256);
				rp.m_reject_thresholds[1] = rp.m_reject_thresholds[0];
				rp.m_reject_thresholds[2] = rp.m_reject_thresholds[0];
				rp.m_use_reject_thresholds = true;
				arg_count++;
			}
			else if (strcasecmp(pArg, "-rl") == 0)
			{
				REMAINING_ARGS_CHECK(1);
				rp.m_reject_thresholds_lab[0] = clamp<float>((float)atof(arg_v[arg_index + 1]), 0.0f, 1.0f);
				rp.m_use_reject_thresholds = true;
				arg_count++;
			}
			else if (strcasecmp(pArg, "-rlab") == 0)
			{
				REMAINING_ARGS_CHECK(1);
				rp.m_reject_thresholds_lab[1] = clamp<float>((float)atof(arg_v[arg_index + 1]), 0, 1.0f);
				rp.m_use_reject_thresholds = true;
				arg_count++;
			}
			else if (strcasecmp(pArg, "-rr") == 0)
			{
				REMAINING_ARGS_CHECK(1);
				rp.m_reject_thresholds[0] = clamp<int>(atoi(arg_v[arg_index + 1]), 0, 256);
				rp.m_use_reject_thresholds = true;
				arg_count++;
			}
			else if (strcasecmp(pArg, "-rg") == 0)
			{
				REMAINING_ARGS_CHECK(1);
				rp.m_reject_thresholds[1] = clamp<int>(atoi(arg_v[arg_index + 1]), 0, 256);
				rp.m_use_reject_thresholds = true;
				arg_count++;
			}
			else if (strcasecmp(pArg, "-rb") == 0)
			{
				REMAINING_ARGS_CHECK(1);
				rp.m_reject_thresholds[2] = clamp<int>(atoi(arg_v[arg_index + 1]), 0, 256);
				rp.m_use_reject_thresholds = true;
				arg_count++;
			}
			else if (strcasecmp(pArg, "-ra") == 0)
			{
				REMAINING_ARGS_CHECK(1);
				rp.m_reject_thresholds[3] = clamp<int>(atoi(arg_v[arg_index + 1]), 0, 256);
				rp.m_use_reject_thresholds = true;
				arg_count++;
			}
			else if (strcasecmp(pArg, "-wr") == 0)
			{
				REMAINING_ARGS_CHECK(1);
				rp.m_chan_weights[0] = clamp<int>(atoi(arg_v[arg_index + 1]), 0, 256);
				rp.m_use_chan_weights = true;
				arg_count++;
			}
			else if (strcasecmp(pArg, "-wg") == 0)
			{
				REMAINING_ARGS_CHECK(1);
				rp.m_chan_weights[1] = clamp<int>(atoi(arg_v[arg_index + 1]), 0, 256);
				rp.m_use_chan_weights = true;
				arg_count++;
			}
			else if (strcasecmp(pArg, "-wb") == 0)
			{
				REMAINING_ARGS_CHECK(1);
				rp.m_chan_weights[2] = clamp<int>(atoi(arg_v[arg_index + 1]), 0, 256);
				rp.m_use_chan_weights = true;
				arg_count++;
			}
			else if (strcasecmp(pArg, "-wa") == 0)
			{
				REMAINING_ARGS_CHECK(1);
				rp.m_chan_weights[3] = clamp<int>(atoi(arg_v[arg_index + 1]), 0, 256);
				rp.m_use_chan_weights = true;
				arg_count++;
			}
			else if (strcasecmp(pArg, "-wlab") == 0)
			{
				REMAINING_ARGS_CHECK(4);
				float wl = clamp<float>((float)atof(arg_v[arg_index + 1]), 0, 100.0f);
				float wa = clamp<float>((float)atof(arg_v[arg_index + 2]), 0, 100.0f);
				float wb = clamp<float>((float)atof(arg_v[arg_index + 3]), 0, 100.0f);
				float walpha = clamp<float>((float)atof(arg_v[arg_index + 4]), 0, 100.0f);

				// We want wl,wa,wb to have a vector length of 1. In 32bpp mode, it's fine if its length is a bit higher than 1, the user will adjust the lambda accordingly.
				float l = sqrtf(wl * wl + wa * wa + wb * wb);
				if (l)
				{
					wl /= l;
					wa /= l;
					wb /= l;
				}
				rp.m_chan_weights_lab[0] = wl; // L
				rp.m_chan_weights_lab[1] = wa; // a
				rp.m_chan_weights_lab[2] = wb; // b
				rp.m_chan_weights_lab[3] = walpha; // alpha

				arg_count += 4;
			}
			else if (strcasecmp(pArg, "-linear") == 0)
			{
				rp.m_perceptual_error = false;
			}
			else if (strcasecmp(pArg, "-no_alpha_opacity") == 0)
			{
				rp.m_alpha_is_opacity = false;
			}
			else if (strcasecmp(pArg, "-match_only") == 0)
			{
				rp.m_match_only = true;
			}
			else if (strcasecmp(pArg, "-two_pass") == 0)
			{
				rp.m_two_pass = true;
			}
			else if (strcasecmp(pArg, "-uber") == 0)
			{
				rp.m_speed_mode = cNormalSpeed;
			}
			else if (strcasecmp(pArg, "-better") == 0)
			{
				rp.m_speed_mode = cFasterSpeed;
			}
			else if (strcasecmp(pArg, "-fastest") == 0)
			{
				rp.m_speed_mode = cFastestSpeed;
			}
			else if (strcasecmp(pArg, "-print_normal_map_metrics") == 0)
			{
				rp.m_print_normal_map_metrics = true;
			}
			else if (strcasecmp(pArg, "-normal_map") == 0)
			{
				rp.m_normal_map = true;
				rp.m_perceptual_error = false;
				rp.m_reject_thresholds[0] = 20;
				rp.m_reject_thresholds[1] = 20;
				rp.m_reject_thresholds[2] = 20;
			}
			else if (strcasecmp(pArg, "-normalize") == 0)
			{
				normalize_first = true;
			}
			else if (strcasecmp(pArg, "-snorm") == 0)
			{
				rp.m_snorm8 = true;
			}
			else if (pArg[0] == '-')
			{
				fprintf(stderr, "Unrecognized command line option: %s\n", pArg);
				return EXIT_FAILURE;
			}
			else
			{
				if (input_filename.size())
				{
					fprintf(stderr, "Too many input filenames\n");
					return EXIT_FAILURE;
				}
				input_filename = pArg;
			}

			arg_index += arg_count;
		}

		if (quiet_mode)
		{
			rp.m_print_stats = false;
			rp.m_print_progress = false;
			rp.m_print_debug_output = false;
		}

		if (!quiet_mode)
		{
			printf("rdopng " RDO_PNG_VERSION "\n");
		}

		init_srgb_to_linear();
		init_oklab_table(arg_v[0], quiet_mode, caching_enabled);
		init_acos_lookup();

		if (!input_filename.size())
		{
			fprintf(stderr, "No input filename specified\n");
			return EXIT_FAILURE;
		}

		if (!output_filename.size())
		{
			string_get_filename(input_filename.c_str(), output_filename);
			string_remove_extension(output_filename);
			if (!output_filename.size())
				output_filename = "out";
			
			if (unpack_flag)
				output_filename += ".png";
			else if (mode == cModeLZ4I)
				output_filename += "_rdo.lz4i";
			else if (mode == cModeQOI)
				output_filename += "_rdo.qoi";
			else
				output_filename += "_rdo.png";
		}

		if (unpack_flag)
		{
			uint8_vec file_data;
			if (!read_file_to_vec(input_filename.c_str(), file_data))
			{
				fprintf(stderr, "Failed reading file %s\n", input_filename.c_str());
				return EXIT_FAILURE;
			}

			if (!file_data.size())
			{
				fprintf(stderr, "File %s is empty\n", input_filename.c_str());
				return EXIT_FAILURE;
			}

			image img;
			if (!decode_lz4i(&file_data[0], file_data.size(), img))
			{
				fprintf(stderr, "Failed unpacking LZ4I file %s\n", input_filename.c_str());
				return EXIT_FAILURE;
			}

			if (!save_png(output_filename.c_str(), img))
			{
				fprintf(stderr, "Failed writing to file %s\n", output_filename.c_str());
				return EXIT_FAILURE;
			}
			
			printf("Wrote file %s, %ux%u, has_alpha: %u\n", output_filename.c_str(), img.get_width(), img.get_height(), img.has_alpha());

			status = EXIT_SUCCESS;
		}
		else
		{
			uint64_t input_filesize = 0;
			FILE* pFile = fopen(input_filename.c_str(), "rb");
			if (!pFile)
			{
				fprintf(stderr, "Failed loading file %s\n", input_filename.c_str());
				return EXIT_FAILURE;
			}
			fseek(pFile, 0, SEEK_END);
			input_filesize = ftell(pFile);
			fclose(pFile);

			if (!load_image(input_filename, rp.m_orig_img))
			{
				fprintf(stderr, "Failed loading file %s\n", input_filename.c_str());
				return EXIT_FAILURE;
			}

			if (!quiet_mode)
			{
				printf("Loaded file \"%s\", %ux%u, has alpha: %u, size: %llu, bpp: %3.3f\n",
					input_filename.c_str(), rp.m_orig_img.get_width(), rp.m_orig_img.get_height(), rp.m_orig_img.has_alpha(),
					(unsigned long long)input_filesize, (input_filesize * 8.0f) / rp.m_orig_img.get_total_pixels());
			}

			if (rp.m_debug_images)
			{
				save_png("dbg_loaded.png", rp.m_orig_img);
			}

			if (normalize_first)
			{
				normalize_image(rp.m_orig_img, rp);
			}

			if (mode == cModeLZ4I)
			{
				// LZ4-specific settings - more artifact suppression on smooth/ultra-smooth regions vs. PNG.
				rp.m_smooth_max_mse_scale = LZ4I_DEF_SMOOTH_MAX_MSE_SCALE;
				rp.m_ultra_smooth_max_mse_scale = LZ4I_DEF_ULTRA_SMOOTH_MAX_MSE_SCALE;
			}
			else if (mode == cModeQOI)
			{
				// QOI-specific settings - more artifact suppression on smooth/ultra-smooth regions vs. PNG.
				rp.m_smooth_max_mse_scale = QOI_DEF_SMOOTH_MAX_MSE_SCALE;
				rp.m_ultra_smooth_max_mse_scale = QOI_DEF_ULTRA_SMOOTH_MAX_MSE_SCALE;
			}

			if (max_smooth_std_dev != -1.0f)
				rp.m_max_smooth_std_dev = max_smooth_std_dev;

			if (smooth_max_mse_scale != -1.0f)
				rp.m_smooth_max_mse_scale = smooth_max_mse_scale;

			if (max_ultra_smooth_std_dev != -1.0f)
				rp.m_ultra_smooth_max_mse_scale = max_ultra_smooth_std_dev;

			if (ultra_smooth_max_mse_scale != -1.0f)
				rp.m_ultra_smooth_max_mse_scale = ultra_smooth_max_mse_scale;

			if (rp.m_print_debug_output)
			{
				printf("\nParameters:\n");
				rp.print();
				printf("\n");
			}

			interval_timer tm;
			tm.start();

			bool status = false;

			if (mode == cModeQOI)
				status = rdo_qoi(rp);
			else if (mode == cModeLZ4I)
				status = rdo_lz4i(rp);
			else
				status = rdo_png(rp);

			if (status)
			{
				if (!quiet_mode)
					printf("Encoded in %3.3f secs\n", tm.get_elapsed_secs());

				if (!write_vec_to_file(output_filename.c_str(), rp.m_output_file_data))
				{
					fprintf(stderr, "Failed writing to file \"%s\"\n", output_filename.c_str());
					return EXIT_FAILURE;
				}

				if (!quiet_mode)
				{
					printf("Wrote output file \"%s\"\n", output_filename.c_str());
				}

				if (unpack_qoi_to_png)
				{
					std::string png_filename(output_filename);
					string_remove_extension(png_filename);
					png_filename += ".png";

					if (!save_png(png_filename.c_str(), rp.m_output_image))
					{
						fprintf(stderr, "Failed writing to file \"%s\"\n", png_filename.c_str());
						return EXIT_FAILURE;
					}

					if (!quiet_mode)
					{
						printf("Wrote output file \"%s\"\n", png_filename.c_str());
					}
				}

				status = EXIT_SUCCESS;
			}
		}
	}
	catch (const std::exception &exc)
	{
		 fprintf(stderr, "FATAL ERROR: Caught exception \"%s\"\n", exc.what());
	}
	catch (...)
	{
		fprintf(stderr, "FATAL ERROR: Uncaught exception!\n");
	}
#else
	status = main_internal(argc, argv);
#endif

	return status;
}
