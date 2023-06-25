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

#include <lsp-plug.in/common/alloc.h>
#include <lsp-plug.in/common/debug.h>
#include <lsp-plug.in/dsp/dsp.h>
#include <lsp-plug.in/dsp-units/units.h>
#include <lsp-plug.in/plug-fw/meta/func.h>

#include <private/plugins/mb_limiter.h>

/* The size of temporary buffer for audio processing */
#define BUFFER_SIZE         0x1000U

namespace lsp
{
//    static plug::IPort *TRACE_PORT(plug::IPort *p)
//    {
//        lsp_trace("  port id=%s", (p)->metadata()->id);
//        return p;
//    }

    namespace plugins
    {
        //---------------------------------------------------------------------
        // Plugin factory
        static const meta::plugin_t *plugins[] =
        {
            &meta::mb_limiter_mono,
            &meta::mb_limiter_stereo,
            &meta::sc_mb_limiter_mono,
            &meta::sc_mb_limiter_stereo,
        };

        static plug::Module *plugin_factory(const meta::plugin_t *meta)
        {
            return new mb_limiter(meta);
        }

        static plug::Factory factory(plugin_factory, plugins, 2);

        //---------------------------------------------------------------------
        // Implementation
        mb_limiter::mb_limiter(const meta::plugin_t *meta):
            Module(meta)
        {
            nChannels           = 1;
            bSidechain          = false;

            if ((!strcmp(meta->uid, meta::mb_limiter_stereo.uid)) ||
                (!strcmp(meta->uid, meta::sc_mb_limiter_stereo.uid)))
                nChannels           = 2;

            if ((!strcmp(meta->uid, meta::sc_mb_limiter_mono.uid)) ||
                (!strcmp(meta->uid, meta::sc_mb_limiter_stereo.uid)))
                bSidechain      = true;
        }

        mb_limiter::~mb_limiter()
        {
            destroy();
        }

        void mb_limiter::init(plug::IWrapper *wrapper, plug::IPort **ports)
        {
            // Call parent class for initialization
            Module::init(wrapper, ports);

            // TODO
        }

        void mb_limiter::destroy()
        {
            Module::destroy();

            // TODO
        }

        void mb_limiter::update_sample_rate(long sr)
        {
            // TODO
        }

        void mb_limiter::update_settings()
        {
            // TODO
        }

        void mb_limiter::process(size_t samples)
        {
            // TODO
        }

        void mb_limiter::dump(dspu::IStateDumper *v) const
        {
            // TODO
        }

        bool mb_limiter::inline_display(plug::ICanvas *cv, size_t width, size_t height)
        {
            // TODO
            return false;
        }

    } /* namespace plugins */
} /* namespace lsp */


