/*
 * Copyright (C) 2023 Linux Studio Plugins Project <https://lsp-plug.in/>
 *           (C) 2023 Vladimir Sadovnikov <sadko4u@gmail.com>
 *
 * This file is part of lsp-plugins-mb-limiter
 * Created on: 22 июн 2023 г.
 *
 * lsp-plugins-mb-limiter is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 *
 * lsp-plugins-mb-limiter is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with lsp-plugins-mb-limiter. If not, see <https://www.gnu.org/licenses/>.
 */

#ifndef PRIVATE_META_MB_LIMITER_H_
#define PRIVATE_META_MB_LIMITER_H_

#include <lsp-plug.in/dsp-units/misc/windows.h>
#include <lsp-plug.in/plug-fw/meta/types.h>
#include <lsp-plug.in/plug-fw/const.h>

namespace lsp
{
    //-------------------------------------------------------------------------
    // Plugin metadata
    namespace meta
    {
        typedef struct mb_limiter
        {
            static constexpr float  FREQ_MIN                = 10.0f;
            static constexpr float  FREQ_MAX                = 20000.0f;
            static constexpr float  FREQ_DFL                = 1000.0f;
            static constexpr float  FREQ_STEP               = 0.002f;

            static constexpr float  OUT_FREQ_MIN            = 0.0f;
            static constexpr float  OUT_FREQ_MAX            = MAX_SAMPLE_RATE;
            static constexpr float  OUT_FREQ_DFL            = 1000.0f;
            static constexpr float  OUT_FREQ_STEP           = 0.002f;

            static constexpr float  ZOOM_MIN                = GAIN_AMP_M_18_DB;
            static constexpr float  ZOOM_MAX                = GAIN_AMP_0_DB;
            static constexpr float  ZOOM_DFL                = GAIN_AMP_0_DB;
            static constexpr float  ZOOM_STEP               = 0.0125f;

            static constexpr float  LOOKAHEAD_MIN           = 0.1f;     // No lookahead [ms]
            static constexpr float  LOOKAHEAD_MAX           = 20.0f;    // Maximum Lookahead [ms]
            static constexpr float  LOOKAHEAD_DFL           = 5.0f;     // Default Lookahead [ms]
            static constexpr float  LOOKAHEAD_STEP          = 0.005f;   // Lookahead step

            static constexpr float  ATTACK_TIME_MIN         = 0.25f;
            static constexpr float  ATTACK_TIME_MAX         = 20.0f;
            static constexpr float  ATTACK_TIME_DFL         = 5.0f;
            static constexpr float  ATTACK_TIME_STEP        = 0.0025f;

            static constexpr float  RELEASE_TIME_MIN        = 0.25f;
            static constexpr float  RELEASE_TIME_MAX        = 20.0f;
            static constexpr float  RELEASE_TIME_DFL        = 5.0f;
            static constexpr float  RELEASE_TIME_STEP       = 0.0025f;

            static constexpr float  ALR_ATTACK_TIME_MIN     = 0.1f;
            static constexpr float  ALR_ATTACK_TIME_MAX     = 200.0f;
            static constexpr float  ALR_ATTACK_TIME_DFL     = 5.0f;
            static constexpr float  ALR_ATTACK_TIME_STEP    = 0.0025f;

            static constexpr float  ALR_RELEASE_TIME_MIN    = 10.0f;
            static constexpr float  ALR_RELEASE_TIME_MAX    = 1000.0f;
            static constexpr float  ALR_RELEASE_TIME_DFL    = 50.0f;
            static constexpr float  ALR_RELEASE_TIME_STEP   = 0.0025f;

            static constexpr float  THRESHOLD_MIN           = GAIN_AMP_M_48_DB;
            static constexpr float  THRESHOLD_MAX           = GAIN_AMP_0_DB;
            static constexpr float  THRESHOLD_DFL           = GAIN_AMP_0_DB;
            static constexpr float  THRESHOLD_STEP          = 0.01f;

            static constexpr float  MAKEUP_MIN              = GAIN_AMP_M_48_DB;
            static constexpr float  MAKEUP_MAX              = GAIN_AMP_P_48_DB;
            static constexpr float  MAKEUP_DFL              = GAIN_AMP_0_DB;
            static constexpr float  MAKEUP_STEP             = 0.01f;

            static constexpr float  KNEE_MIN                = GAIN_AMP_M_12_DB;
            static constexpr float  KNEE_MAX                = GAIN_AMP_P_12_DB;
            static constexpr float  KNEE_DFL                = GAIN_AMP_0_DB;
            static constexpr float  KNEE_STEP               = 0.01f;

            static constexpr float  LINKING_MIN             = 0;
            static constexpr float  LINKING_MAX             = 100.0f;
            static constexpr float  LINKING_DFL             = 100.0f;
            static constexpr float  LINKING_STEP            = 0.01f;

            static constexpr float  REACT_TIME_MIN          = 0.000;
            static constexpr float  REACT_TIME_MAX          = 1.000;
            static constexpr float  REACT_TIME_DFL          = 0.200;
            static constexpr float  REACT_TIME_STEP         = 0.001;

            static constexpr size_t FFT_RANK                = 13;
            static constexpr size_t FFT_ITEMS               = 1 << FFT_RANK;
            static constexpr size_t FFT_MESH_POINTS         = 640;
            static constexpr size_t FFT_WINDOW              = dspu::windows::HANN;
            static constexpr size_t BANDS_MAX               = 8;
            static constexpr size_t BANDS_DFL               = 4;
            static constexpr size_t REFRESH_RATE            = 20;
            static constexpr float  FREQ_BOOST_MIN          = 10.0f;
            static constexpr float  FREQ_BOOST_MAX          = 20000.0f;
            static constexpr size_t OVERSAMPLING_MAX        = 8;        // Maximum 8x oversampling
            static constexpr size_t FILTER_SLOPE            = 4;        // Filter slopeness

            enum oversampling_mode_t
            {
                OVS_NONE,

                OVS_HALF_2X2,
                OVS_HALF_2X3,
                OVS_HALF_3X2,
                OVS_HALF_3X3,
                OVS_HALF_4X2,
                OVS_HALF_4X3,
                OVS_HALF_6X2,
                OVS_HALF_6X3,
                OVS_HALF_8X2,
                OVS_HALF_8X3,

                OVS_FULL_2X2,
                OVS_FULL_2X3,
                OVS_FULL_3X2,
                OVS_FULL_3X3,
                OVS_FULL_4X2,
                OVS_FULL_4X3,
                OVS_FULL_6X2,
                OVS_FULL_6X3,
                OVS_FULL_8X2,
                OVS_FULL_8X3,

                OVS_DEFAULT     = OVS_NONE
            };

            enum limiter_mode_t
            {
                LOM_HERM_THIN,
                LOM_HERM_WIDE,
                LOM_HERM_TAIL,
                LOM_HERM_DUCK,

                LOM_EXP_THIN,
                LOM_EXP_WIDE,
                LOM_EXP_TAIL,
                LOM_EXP_DUCK,

                LOM_LINE_THIN,
                LOM_LINE_WIDE,
                LOM_LINE_TAIL,
                LOM_LINE_DUCK,

                LOM_DEFAULT     = LOM_HERM_THIN
            };

            enum dithering_t
            {
                DITHER_NONE,
                DITHER_7BIT,
                DITHER_8BIT,
                DITHER_11BIT,
                DITHER_12BIT,
                DITHER_15BIT,
                DITHER_16BIT,
                DITHER_23BIT,
                DITHER_24BIT,

                DITHER_DEFAULT  = DITHER_NONE
            };

            enum boost_t
            {
                FB_OFF,
                FB_BT_3DB,
                FB_MT_3DB,
                FB_BT_6DB,
                FB_MT_6DB,

                FB_DEFAULT              = FB_BT_3DB
            };
        } mb_limiter;

        // Plugin type metadata
        extern const plugin_t mb_limiter_mono;
        extern const plugin_t mb_limiter_stereo;
        extern const plugin_t sc_mb_limiter_mono;
        extern const plugin_t sc_mb_limiter_stereo;

    } /* namespace meta */
} /* namespace lsp */

#endif /* PRIVATE_META_MB_LIMITER_H_ */
