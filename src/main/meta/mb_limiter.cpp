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

#include <lsp-plug.in/plug-fw/meta/ports.h>
#include <lsp-plug.in/shared/meta/developers.h>
#include <private/meta/mb_limiter.h>

#define LSP_PLUGINS_MB_LIMITER_VERSION_MAJOR       1
#define LSP_PLUGINS_MB_LIMITER_VERSION_MINOR       0
#define LSP_PLUGINS_MB_LIMITER_VERSION_MICRO       0

#define LSP_PLUGINS_MB_LIMITER_VERSION  \
    LSP_MODULE_VERSION( \
        LSP_PLUGINS_MB_LIMITER_VERSION_MAJOR, \
        LSP_PLUGINS_MB_LIMITER_VERSION_MINOR, \
        LSP_PLUGINS_MB_LIMITER_VERSION_MICRO  \
    )

namespace lsp
{
    namespace meta
    {
        //-------------------------------------------------------------------------
        // Plugin metadata
        static const port_item_t limiter_split_modes[] =
        {
            { "Classic",        "mb_limiter.classic"    },
            { "Modern",         "mb_limiter.modern"     },
            { NULL, NULL }
        };

        static port_item_t limiter_oper_modes[] =
        {
            { "Herm Thin",      "limiter.herm_thin"     },
            { "Herm Wide",      "limiter.herm_wide"     },
            { "Herm Tail",      "limiter.herm_tail"     },
            { "Herm Duck",      "limiter.herm_duck"     },

            { "Exp Thin",       "limiter.exp_thin"      },
            { "Exp Wide",       "limiter.exp_wide"      },
            { "Exp Tail",       "limiter.exp_tail"      },
            { "Exp Duck",       "limiter.exp_duck"      },

            { "Line Thin",      "limiter.line_thin"     },
            { "Line Wide",      "limiter.line_wide"     },
            { "Line Tail",      "limiter.line_tail"     },
            { "Line Duck",      "limiter.line_duck"     },

            { NULL, NULL }
        };

        static port_item_t limiter_ovs_modes[] =
        {
            { "None",           "oversampler.none"      },

            { "Half x2(2L)",    "oversampler.half.2x2"  },
            { "Half x2(3L)",    "oversampler.half.2x3"  },
            { "Half x3(2L)",    "oversampler.half.3x2"  },
            { "Half x3(3L)",    "oversampler.half.3x3"  },
            { "Half x4(2L)",    "oversampler.half.4x2"  },
            { "Half x4(3L)",    "oversampler.half.4x3"  },
            { "Half x6(2L)",    "oversampler.half.6x2"  },
            { "Half x6(3L)",    "oversampler.half.6x3"  },
            { "Half x8(2L)",    "oversampler.half.8x2"  },
            { "Half x8(3L)",    "oversampler.half.8x3"  },

            { "Full x2(2L)",    "oversampler.full.2x2"  },
            { "Full x2(3L)",    "oversampler.full.2x3"  },
            { "Full x3(2L)",    "oversampler.full.3x2"  },
            { "Full x3(3L)",    "oversampler.full.3x3"  },
            { "Full x4(2L)",    "oversampler.full.4x2"  },
            { "Full x4(3L)",    "oversampler.full.4x3"  },
            { "Full x6(2L)",    "oversampler.full.6x2"  },
            { "Full x6(3L)",    "oversampler.full.6x3"  },
            { "Full x8(2L)",    "oversampler.full.8x2"  },
            { "Full x8(3L)",    "oversampler.full.8x3"  },

            { NULL, NULL }
        };

        static port_item_t limiter_dither_modes[] =
        {
            { "None",           "dither.none"           },
            { "7bit",           "dither.bits.7"         },
            { "8bit",           "dither.bits.8"         },
            { "11bit",          "dither.bits.11"        },
            { "12bit",          "dither.bits.12"        },
            { "15bit",          "dither.bits.15"        },
            { "16bit",          "dither.bits.16"        },
            { "23bit",          "dither.bits.23"        },
            { "24bit",          "dither.bits.24"        },
            { NULL, NULL }
        };

        static const port_item_t limiter_sc_boost[] =
        {
            { "None",           "sidechain.boost.none" },
            { "Pink BT",        "sidechain.boost.pink_bt" },
            { "Pink MT",        "sidechain.boost.pink_mt" },
            { "Brown BT",       "sidechain.boost.brown_bt" },
            { "Brown MT",       "sidechain.boost.brown_mt" },
            { NULL, NULL }
        };

        #define MBL_COMMON \
            BYPASS, \
            IN_GAIN, \
            OUT_GAIN, \
            LOG_CONTROL("lk", "Lookahead", U_MSEC, mb_limiter::LOOKAHEAD), \
            COMBO("mode", "Operating mode", 1, limiter_split_modes), \
            COMBO("ovs", "Oversampling", mb_limiter::OVS_DEFAULT, limiter_ovs_modes), \
            COMBO("dither", "Dithering", mb_limiter::DITHER_DEFAULT, limiter_dither_modes), \
            COMBO("envb", "Envelope boost", mb_limiter::FB_DEFAULT, limiter_sc_boost), \
            LOG_CONTROL("zoom", "Graph zoom", U_GAIN_AMP, mb_limiter::ZOOM), \
            SWITCH("flt", "Band filter curves", 1.0f), \
            LOG_CONTROL("react", "FFT reactivity", U_MSEC, mb_limiter::REACT_TIME), \
            AMP_GAIN100("shift", "Shift gain", 1.0f)

        #define MBL_SC_COMMON \
            MBL_COMMON, \
            SWITCH("extsc", "External sidechain", 0.0f)

        #define MBL_SPLIT(id, label, enable, freq) \
            SWITCH("se" id, "Limiter band enable" label, enable), \
            LOG_CONTROL_DFL("sf" id, "Band split frequency" label, U_HZ, mb_limiter::FREQ, freq)

        #define MBL_LIMITER(id, label) \
            SWITCH("on" id, "Limiter enabled" label, 1.0f), \
            SWITCH("alr" id, "Automatic level regulation" label, 1.0f), \
            LOG_CONTROL("aat" id, "Automatic level regulation attack time" label, U_MSEC, mb_limiter::ALR_ATTACK_TIME), \
            LOG_CONTROL("art" id, "Automatic level regulation release time" label, U_MSEC, mb_limiter::ALR_RELEASE_TIME), \
            LOG_CONTROL("akn" id, "Automatic level regulation knee" label, U_GAIN_AMP, mb_limiter::KNEE), \
            COMBO("lm" id, "Operating mode" label, mb_limiter::LOM_DEFAULT, limiter_oper_modes), \
            LOG_CONTROL("th" id, "Threshold" label, U_GAIN_AMP, mb_limiter::THRESHOLD), \
            SWITCH("gb" id, "Gain boost" label, 1.0f), \
            LOG_CONTROL("at" id, "Attack time" label, U_MSEC, mb_limiter::ATTACK_TIME), \
            LOG_CONTROL("rt" id, "Release time" label, U_MSEC, mb_limiter::RELEASE_TIME)

        #define MBL_LIMITER_METERS(id, label) \
            METER_OUT_GAIN("rlm" id, "Reduction level meter" label, GAIN_AMP_0_DB)

        #define MBL_MAIN_LIMITER_MONO \
            MBL_LIMITER("", " Main"), \
            MBL_LIMITER_METERS("", " Main")

        #define MBL_MAIN_LIMITER_STEREO \
            MBL_LIMITER("", " Main"), \
            LOG_CONTROL("slink", "Stereo linking Main", U_PERCENT, mb_limiter::LINKING), \
            MBL_LIMITER_METERS("_l", " Main Left"), \
            MBL_LIMITER_METERS("_r", " Main Right")

        #define MBL_BAND_COMMON(id, label) \
            METER("bfe" id, "Frequency range end" label, U_HZ, mb_limiter::OUT_FREQ), \
            SWITCH("bs" id, "Solo band" label, 0.0f), \
            SWITCH("bm" id, "Mute band" label, 0.0f), \
            AMP_GAIN100("bpa" id, "Band preamp" label, GAIN_AMP_0_DB), \
            AMP_GAIN100("bmk" id, "Band makeup" label, GAIN_AMP_0_DB), \
            MESH("bfc" id, "Band filter chart" label, 2, mb_limiter::FFT_MESH_POINTS + 2), \
            MBL_LIMITER(id, label)

        #define MBL_BAND_MONO(id, label) \
            MBL_BAND_COMMON(id, label), \
            MBL_LIMITER_METERS(id, label)

        #define MBL_BAND_STEREO(id, label) \
            MBL_BAND_COMMON(id, label), \
            LOG_CONTROL("bsl" id, "Band stereo linking" label, U_PERCENT, mb_limiter::LINKING), \
            MBL_LIMITER_METERS(id "l", label " Left"), \
            MBL_LIMITER_METERS(id "r", label " Right")

        #define MBL_METERS(id, label) \
            SWITCH("ife" id, "Input FFT enable" label, 1.0f), \
            SWITCH("ofe" id, "Output FFT enable" label, 1.0f), \
            METER_OUT_GAIN("ilm" id, "Input level meter" label, GAIN_AMP_0_DB), \
            METER_OUT_GAIN("olm" id, "Output level meter" label, GAIN_AMP_0_DB), \
            MESH("ifg" id, "Input FFT graph" label, 2, mb_limiter::FFT_MESH_POINTS + 2), \
            MESH("ofg" id, "Output FFT graph" label, 2, mb_limiter::FFT_MESH_POINTS), \
            MESH("ag" id, "Amplification graph" label, 2, mb_limiter::FFT_MESH_POINTS + 2)

        #define MBL_METERS_MONO \
            MBL_METERS("", "")

        #define MBL_METERS_STEREO \
            MBL_METERS("_l", " Left"), \
            MBL_METERS("_r", " Right")

        static const port_t mb_limiter_mono_ports[] =
        {
            // Input and output audio ports
            PORTS_MONO_PLUGIN,
            MBL_COMMON,
            MBL_METERS_MONO,
            MBL_MAIN_LIMITER_MONO,

            MBL_SPLIT("_1", " 1", 0.0f, 40.0f),
            MBL_SPLIT("_2", " 2", 1.0f, 100.0f),
            MBL_SPLIT("_3", " 3", 0.0f, 252.0f),
            MBL_SPLIT("_4", " 4", 1.0f, 632.0f),
            MBL_SPLIT("_5", " 5", 0.0f, 1587.0f),
            MBL_SPLIT("_6", " 6", 1.0f, 3984.0f),
            MBL_SPLIT("_7", " 7", 0.0f, 10000.0f),

            MBL_BAND_MONO("_1", " 1"),
            MBL_BAND_MONO("_2", " 2"),
            MBL_BAND_MONO("_3", " 3"),
            MBL_BAND_MONO("_4", " 4"),
            MBL_BAND_MONO("_5", " 5"),
            MBL_BAND_MONO("_6", " 6"),
            MBL_BAND_MONO("_7", " 7"),
            MBL_BAND_MONO("_8", " 8"),

            PORTS_END
        };

        static const port_t mb_limiter_stereo_ports[] =
        {
            // Input and output audio ports
            PORTS_STEREO_PLUGIN,
            MBL_COMMON,
            MBL_METERS_STEREO,
            MBL_MAIN_LIMITER_STEREO,

            MBL_SPLIT("_1", " 1", 0.0f, 40.0f),
            MBL_SPLIT("_2", " 2", 1.0f, 100.0f),
            MBL_SPLIT("_3", " 3", 0.0f, 252.0f),
            MBL_SPLIT("_4", " 4", 1.0f, 632.0f),
            MBL_SPLIT("_5", " 5", 0.0f, 1587.0f),
            MBL_SPLIT("_6", " 6", 1.0f, 3984.0f),
            MBL_SPLIT("_7", " 7", 0.0f, 10000.0f),

            MBL_BAND_STEREO("_1", " 1"),
            MBL_BAND_STEREO("_2", " 2"),
            MBL_BAND_STEREO("_3", " 3"),
            MBL_BAND_STEREO("_4", " 4"),
            MBL_BAND_STEREO("_5", " 5"),
            MBL_BAND_STEREO("_6", " 6"),
            MBL_BAND_STEREO("_7", " 7"),
            MBL_BAND_STEREO("_8", " 8"),

            PORTS_END
        };

        static const port_t sc_mb_limiter_mono_ports[] =
        {
            // Input and output audio ports
            PORTS_MONO_PLUGIN,
            PORTS_MONO_SIDECHAIN,
            MBL_SC_COMMON,
            MBL_METERS_MONO,
            MBL_MAIN_LIMITER_MONO,

            MBL_SPLIT("_1", " 1", 0.0f, 40.0f),
            MBL_SPLIT("_2", " 2", 1.0f, 100.0f),
            MBL_SPLIT("_3", " 3", 0.0f, 252.0f),
            MBL_SPLIT("_4", " 4", 1.0f, 632.0f),
            MBL_SPLIT("_5", " 5", 0.0f, 1587.0f),
            MBL_SPLIT("_6", " 6", 1.0f, 3984.0f),
            MBL_SPLIT("_7", " 7", 0.0f, 10000.0f),

            MBL_BAND_MONO("_1", " 1"),
            MBL_BAND_MONO("_2", " 2"),
            MBL_BAND_MONO("_3", " 3"),
            MBL_BAND_MONO("_4", " 4"),
            MBL_BAND_MONO("_5", " 5"),
            MBL_BAND_MONO("_6", " 6"),
            MBL_BAND_MONO("_7", " 7"),
            MBL_BAND_MONO("_8", " 8"),

            PORTS_END
        };

        static const port_t sc_mb_limiter_stereo_ports[] =
        {
            // Input and output audio ports
            PORTS_STEREO_PLUGIN,
            PORTS_STEREO_SIDECHAIN,
            MBL_SC_COMMON,
            MBL_METERS_STEREO,
            MBL_MAIN_LIMITER_STEREO,

            MBL_SPLIT("_1", " 1", 0.0f, 40.0f),
            MBL_SPLIT("_2", " 2", 1.0f, 100.0f),
            MBL_SPLIT("_3", " 3", 0.0f, 252.0f),
            MBL_SPLIT("_4", " 4", 1.0f, 632.0f),
            MBL_SPLIT("_5", " 5", 0.0f, 1587.0f),
            MBL_SPLIT("_6", " 6", 1.0f, 3984.0f),
            MBL_SPLIT("_7", " 7", 0.0f, 10000.0f),

            MBL_BAND_STEREO("_1", " 1"),
            MBL_BAND_STEREO("_2", " 2"),
            MBL_BAND_STEREO("_3", " 3"),
            MBL_BAND_STEREO("_4", " 4"),
            MBL_BAND_STEREO("_5", " 5"),
            MBL_BAND_STEREO("_6", " 6"),
            MBL_BAND_STEREO("_7", " 7"),
            MBL_BAND_STEREO("_8", " 8"),

            PORTS_END
        };

        static const int plugin_classes[]       = { C_LIMITER, -1 };
        static const int clap_features_mono[]   = { CF_AUDIO_EFFECT, CF_LIMITER, CF_MONO, -1 };
        static const int clap_features_stereo[] = { CF_AUDIO_EFFECT, CF_LIMITER, CF_STEREO, -1 };

        const meta::bundle_t mb_limiter_bundle =
        {
            "mb_limiter",
            "Multiband Limiter",
            B_DYNAMICS,
            "", // TODO: provide ID of the video on YouTube
            "" // TODO: write plugin description, should be the same to the english version in 'bundles.json'
        };

        const plugin_t mb_limiter_mono =
        {
            "Multi-band Begrenzer Mono",
            "Multiband Limiter Mono",
            "MBL1M",
            &developers::v_sadovnikov,
            "mb_limiter_mono",
            LSP_LV2_URI("mb_limiter_mono"),
            LSP_LV2UI_URI("mb_limiter_mono"),
            "mblm",
            LSP_LADSPA_MB_LIMITER_BASE + 0,
            LSP_LADSPA_URI("mb_limiter_mono"),
            LSP_CLAP_URI("mb_limiter_mono"),
            LSP_PLUGINS_MB_LIMITER_VERSION,
            plugin_classes,
            clap_features_mono,
            E_DUMP_STATE | E_INLINE_DISPLAY,
            mb_limiter_mono_ports,
            "dynamics/limiter/multiband/limiter.xml",
            NULL,
            mono_plugin_port_groups,
            &mb_limiter_bundle
        };

        const plugin_t mb_limiter_stereo =
        {
            "Multi-band Begrenzer Stereo",
            "Multiband Limiter Stereo",
            "MBL1S",
            &developers::v_sadovnikov,
            "mb_limiter_stereo",
            LSP_LV2_URI("mb_limiter_stereo"),
            LSP_LV2UI_URI("mb_limiter_stereo"),
            "mbls",
            LSP_LADSPA_MB_LIMITER_BASE + 1,
            LSP_LADSPA_URI("mb_limiter_stereo"),
            LSP_CLAP_URI("mb_limiter_stereo"),
            LSP_PLUGINS_MB_LIMITER_VERSION,
            plugin_classes,
            clap_features_stereo,
            E_DUMP_STATE | E_INLINE_DISPLAY,
            mb_limiter_stereo_ports,
            "dynamics/limiter/multiband/limiter.xml",
            NULL,
            stereo_plugin_port_groups,
            &mb_limiter_bundle
        };

        const plugin_t sc_mb_limiter_mono =
        {
            "Sidechain Multi-band Begrenzer Mono",
            "Sidechain Multiband Limiter Mono",
            "SCMBL1M",
            &developers::v_sadovnikov,
            "sc_mb_limiter_mono",
            LSP_LV2_URI("sc_mb_limiter_mono"),
            LSP_LV2UI_URI("sc_mb_limiter_mono"),
            "mblM",
            LSP_LADSPA_MB_LIMITER_BASE + 2,
            LSP_LADSPA_URI("sc_mb_limiter_mono"),
            LSP_CLAP_URI("sc_mb_limiter_mono"),
            LSP_PLUGINS_MB_LIMITER_VERSION,
            plugin_classes,
            clap_features_mono,
            E_DUMP_STATE | E_INLINE_DISPLAY,
            sc_mb_limiter_mono_ports,
            "dynamics/limiter/multiband/limiter.xml",
            NULL,
            mono_plugin_port_groups,
            &mb_limiter_bundle
        };

        const plugin_t sc_mb_limiter_stereo =
        {
            "Sidechain Multi-band Begrenzer Stereo",
            "Sidechain Multiband Limiter Stereo",
            "SCMBL1S",
            &developers::v_sadovnikov,
            "sc_mb_limiter_stereo",
            LSP_LV2_URI("sc_mb_limiter_stereo"),
            LSP_LV2UI_URI("sc_mb_limiter_stereo"),
            "mblS",
            LSP_LADSPA_MB_LIMITER_BASE + 3,
            LSP_LADSPA_URI("sc_mb_limiter_stereo"),
            LSP_CLAP_URI("sc_mb_limiter_stereo"),
            LSP_PLUGINS_MB_LIMITER_VERSION,
            plugin_classes,
            clap_features_stereo,
            E_DUMP_STATE | E_INLINE_DISPLAY,
            sc_mb_limiter_stereo_ports,
            "dynamics/limiter/multiband/limiter.xml",
            NULL,
            stereo_plugin_port_groups,
            &mb_limiter_bundle
        };

    } /* namespace meta */
} /* namespace lsp */



