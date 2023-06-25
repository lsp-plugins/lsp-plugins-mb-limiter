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

        static const port_t mb_limiter_mono_ports[] =
        {
            // Input and output audio ports
            PORTS_MONO_PLUGIN,
            BYPASS,


            PORTS_END
        };

        static const port_t mb_limiter_stereo_ports[] =
        {
            // Input and output audio ports
            PORTS_STEREO_PLUGIN,
            BYPASS,

            PORTS_END
        };

        static const port_t sc_mb_limiter_mono_ports[] =
        {
            // Input and output audio ports
            PORTS_MONO_PLUGIN,
            PORTS_MONO_SIDECHAIN,
            BYPASS,

            PORTS_END
        };

        static const port_t sc_mb_limiter_stereo_ports[] =
        {
            // Input and output audio ports
            PORTS_STEREO_PLUGIN,
            PORTS_STEREO_SIDECHAIN,
            BYPASS,

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
            E_DUMP_STATE,
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
            E_DUMP_STATE,
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
            "mb_limiter_mono",
            LSP_LV2_URI("mb_limiter_mono"),
            LSP_LV2UI_URI("mb_limiter_mono"),
            "mblM",
            LSP_LADSPA_MB_LIMITER_BASE + 2,
            LSP_LADSPA_URI("mb_limiter_mono"),
            LSP_CLAP_URI("mb_limiter_mono"),
            LSP_PLUGINS_MB_LIMITER_VERSION,
            plugin_classes,
            clap_features_mono,
            E_DUMP_STATE,
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
            "mb_limiter_stereo",
            LSP_LV2_URI("mb_limiter_stereo"),
            LSP_LV2UI_URI("mb_limiter_stereo"),
            "mblS",
            LSP_LADSPA_MB_LIMITER_BASE + 3,
            LSP_LADSPA_URI("mb_limiter_stereo"),
            LSP_CLAP_URI("mb_limiter_stereo"),
            LSP_PLUGINS_MB_LIMITER_VERSION,
            plugin_classes,
            clap_features_stereo,
            E_DUMP_STATE,
            sc_mb_limiter_stereo_ports,
            "dynamics/limiter/multiband/limiter.xml",
            NULL,
            stereo_plugin_port_groups,
            &mb_limiter_bundle
        };

    } /* namespace meta */
} /* namespace lsp */



