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
    static plug::IPort *TRACE_PORT(plug::IPort *p)
    {
        lsp_trace("  port id=%s", (p)->metadata()->id);
        return p;
    }

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

        static plug::Factory factory(plugin_factory, plugins, 4);

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

            bExtSc              = false;
            fInGain             = GAIN_AMP_0_DB;
            fOutGain            = GAIN_AMP_0_DB;
            nOversampling       = 0;
            vChannels           = NULL;
            pIDisplay           = NULL;

            for (size_t i=0; i<(meta::mb_limiter::BANDS_MAX-1); ++i)
            {
                split_t *s          = &vSplits[i];

                s->bEnabled         = false;
                s->fFreq            = 0.0f;

                s->pEnabled         = NULL;
                s->pFreq            = NULL;
            }

            nPlanSize           = 0;

            pBypass             = NULL;
            pInGain             = NULL;
            pOutGain            = NULL;
            pLookahead          = NULL;
            pOversampling       = NULL;
            pDithering          = NULL;
            pEnvBoost           = NULL;
            pExtSc              = NULL;

            pData               = NULL;
        }

        mb_limiter::~mb_limiter()
        {
            destroy();
        }

        void mb_limiter::init(plug::IWrapper *wrapper, plug::IPort **ports)
        {
            // Call parent class for initialization
            Module::init(wrapper, ports);

            size_t szof_channel     = sizeof(channel_t);

            size_t to_alloc         = szof_channel * nChannels;

            // Allocate data
            uint8_t *ptr            = alloc_aligned<uint8_t>(pData, to_alloc);
            if (ptr == NULL)
                return;

            // Allocate objects
            vChannels               = reinterpret_cast<channel_t *>(ptr);
            ptr                    += szof_channel * nChannels;

            // Initialize objects
            for (size_t i=0; i<nChannels; ++i)
            {
                channel_t *c    = &vChannels[i];

                // Initialize channel
                c->sBypass.construct();
                c->sDither.construct();
                c->sOver.construct();
                c->sScOver.construct();
                c->sScEQ.construct();
                c->sDryDelay.construct();

                // Destroy bands
                for (size_t j=0; j<meta::mb_limiter::BANDS_MAX; ++j)
                {
                    band_t *b   = &c->vBands[j];

                    // limiter_t
                    b->sLimit.construct();
                    b->sBlink.construct();

                    b->bEnabled         = false;
                    b->fStereoLink      = 0.0f;

                    b->pEnable          = NULL;
                    b->pAlrOn           = NULL;
                    b->pAlrAttack       = NULL;
                    b->pAlrRelease      = NULL;
                    b->pAlrKnee         = NULL;

                    b->pMode            = NULL;
                    b->pThresh          = NULL;
                    b->pBoost           = NULL;
                    b->pAttack          = NULL;
                    b->pRelease         = NULL;
                    b->pStereoLink      = NULL;
                    b->pReductionMeter  = NULL;

                    // band_t
                    b->sEQ.construct();
                    b->sPassFilter.construct();
                    b->sRejFilter.construct();
                    b->sAllFilter.construct();

                    b->sEQ.init(2, 0);
                    b->sPassFilter.init(NULL);
                    b->sRejFilter.init(NULL);
                    b->sAllFilter.init(NULL);

                    b->bSync            = false;
                    b->bMute            = false;
                    b->bSolo            = false;
                    b->fFreqStart       = 0.0f;
                    b->fFreqEnd         = 0.0f;

                    b->vTr              = NULL;
                    b->vVCA             = NULL;

                    b->nFilterID        = 0;

                    b->pFreqEnd         = NULL;
                    b->pSolo            = NULL;
                    b->pMute            = NULL;
                    b->pPreamp          = NULL;
                    b->pMakeup          = NULL;
                    b->pBandGraph       = NULL;
                }

                // Init main limiter
                limiter_t *l    = &c->sLimiter;

                l->sLimit.construct();
                l->sBlink.construct();

                l->bEnabled         = false;
                l->fStereoLink      = 0.0f;

                l->pEnable          = NULL;
                l->pAlrOn           = NULL;
                l->pAlrAttack       = NULL;
                l->pAlrRelease      = NULL;
                l->pAlrKnee         = NULL;

                l->pMode            = NULL;
                l->pThresh          = NULL;
                l->pBoost           = NULL;
                l->pAttack          = NULL;
                l->pRelease         = NULL;
                l->pStereoLink      = NULL;
                l->pReductionMeter  = NULL;

                // Initialize fields
                c->vIn              = NULL;
                c->vSc              = NULL;
                c->vOut             = NULL;

                c->pIn              = NULL;
                c->pOut             = NULL;
                c->pSc              = NULL;
                c->pFftInEnable     = NULL;
                c->pFftOutEnable    = NULL;
                c->pInMeter         = NULL;
                c->pOutMeter        = NULL;
                c->pFftIn           = NULL;
                c->pFftOut          = NULL;
                c->pFilterGraph     = NULL;
            }

            // Bind ports
            size_t port_id      = 0;
            lsp_trace("Binding audio channels");
            for (size_t i=0; i<nChannels; ++i)
                vChannels[i].pIn    = TRACE_PORT(ports[port_id++]);
            for (size_t i=0; i<nChannels; ++i)
                vChannels[i].pOut   = TRACE_PORT(ports[port_id++]);
            for (size_t i=0; i<nChannels; ++i)
                vChannels[i].pSc    = (bSidechain) ? TRACE_PORT(ports[port_id++]) : vChannels[i].pIn;

            // Bind common ports
            lsp_trace("Binding common ports");
            pBypass             = TRACE_PORT(ports[port_id++]);
            pInGain             = TRACE_PORT(ports[port_id++]);
            pOutGain            = TRACE_PORT(ports[port_id++]);
            pLookahead          = TRACE_PORT(ports[port_id++]);
            pOversampling       = TRACE_PORT(ports[port_id++]);
            pDithering          = TRACE_PORT(ports[port_id++]);
            pEnvBoost           = TRACE_PORT(ports[port_id++]);
            pExtSc              = (bSidechain) ? TRACE_PORT(ports[port_id++]) : NULL;

            // Bind metering ports
            lsp_trace("Binding metering ports");
            for (size_t i=0; i<nChannels; ++i)
            {
                channel_t *c        = &vChannels[i];

                c->pFftInEnable     = TRACE_PORT(ports[port_id++]);
                c->pFftOutEnable    = TRACE_PORT(ports[port_id++]);
                c->pInMeter         = TRACE_PORT(ports[port_id++]);
                c->pOutMeter        = TRACE_PORT(ports[port_id++]);
                c->pFftIn           = TRACE_PORT(ports[port_id++]);
                c->pFftOut          = TRACE_PORT(ports[port_id++]);
                c->pFilterGraph     = TRACE_PORT(ports[port_id++]);
            }

            // Bind main limiter ports
            lsp_trace("Binding main limiter ports");
            for (size_t i=0; i<nChannels; ++i)
            {
                channel_t *c        = &vChannels[i];
                limiter_t *l        = &c->sLimiter;

                if (i > 0)
                {
                    channel_t *sc       = &vChannels[0];
                    limiter_t *sl       = &sc->sLimiter;

                    l->pEnable          = sl->pEnable;
                    l->pAlrOn           = sl->pAlrOn;
                    l->pAlrAttack       = sl->pAlrAttack;
                    l->pAlrRelease      = sl->pAlrRelease;
                    l->pAlrKnee         = sl->pAlrKnee;

                    l->pMode            = sl->pMode;
                    l->pThresh          = sl->pThresh;
                    l->pBoost           = sl->pBoost;
                    l->pAttack          = sl->pAttack;
                    l->pRelease         = sl->pRelease;
                    l->pStereoLink      = NULL;
                }
                else
                {
                    l->pEnable          = TRACE_PORT(ports[port_id++]);
                    l->pAlrOn           = TRACE_PORT(ports[port_id++]);
                    l->pAlrAttack       = TRACE_PORT(ports[port_id++]);
                    l->pAlrRelease      = TRACE_PORT(ports[port_id++]);
                    l->pAlrKnee         = TRACE_PORT(ports[port_id++]);

                    l->pMode            = TRACE_PORT(ports[port_id++]);
                    l->pThresh          = TRACE_PORT(ports[port_id++]);
                    l->pBoost           = TRACE_PORT(ports[port_id++]);
                    l->pAttack          = TRACE_PORT(ports[port_id++]);
                    l->pRelease         = TRACE_PORT(ports[port_id++]);
                    l->pStereoLink      = (nChannels > 1) ? TRACE_PORT(ports[port_id++]) : NULL;
                }

                l->pReductionMeter  = TRACE_PORT(ports[port_id++]);
            }

            // Bind split ports
            lsp_trace("Binding split ports");
            for (size_t i=0; i<meta::mb_limiter::BANDS_MAX-1; ++i)
            {
                split_t *s          = &vSplits[i];

                s->pEnabled         = TRACE_PORT(ports[port_id++]);
                s->pFreq            = TRACE_PORT(ports[port_id++]);
            }

            // Bind band-related ports
            lsp_trace("Binding band-related ports");
            for (size_t i=0; i<meta::mb_limiter::BANDS_MAX; ++i)
            {
                for (size_t j=0; j<nChannels; ++j)
                {
                    channel_t *c        = &vChannels[j];
                    band_t *b           = &c->vBands[i];

                    if (j > 0)
                    {
                        channel_t *sc       = &vChannels[0];
                        band_t *sb          = &sc->vBands[i];

                        b->pFreqEnd         = sb->pFreqEnd;
                        b->pSolo            = sb->pSolo;
                        b->pMute            = sb->pMute;
                        b->pPreamp          = sb->pPreamp;
                        b->pMakeup          = sb->pMakeup;
                        b->pBandGraph       = NULL;

                        b->pEnable          = sb->pEnable;
                        b->pAlrOn           = sb->pAlrOn;
                        b->pAlrAttack       = sb->pAlrAttack;
                        b->pAlrRelease      = sb->pAlrRelease;
                        b->pAlrKnee         = sb->pAlrKnee;

                        b->pMode            = sb->pMode;
                        b->pThresh          = sb->pThresh;
                        b->pBoost           = sb->pBoost;
                        b->pAttack          = sb->pAttack;
                        b->pRelease         = sb->pRelease;
                        b->pStereoLink      = NULL;
                    }
                    else
                    {
                        b->pFreqEnd         = TRACE_PORT(ports[port_id++]);
                        b->pSolo            = TRACE_PORT(ports[port_id++]);
                        b->pMute            = TRACE_PORT(ports[port_id++]);
                        b->pPreamp          = TRACE_PORT(ports[port_id++]);
                        b->pMakeup          = TRACE_PORT(ports[port_id++]);
                        b->pBandGraph       = TRACE_PORT(ports[port_id++]);

                        b->pEnable          = TRACE_PORT(ports[port_id++]);
                        b->pAlrOn           = TRACE_PORT(ports[port_id++]);
                        b->pAlrAttack       = TRACE_PORT(ports[port_id++]);
                        b->pAlrRelease      = TRACE_PORT(ports[port_id++]);
                        b->pAlrKnee         = TRACE_PORT(ports[port_id++]);

                        b->pMode            = TRACE_PORT(ports[port_id++]);
                        b->pThresh          = TRACE_PORT(ports[port_id++]);
                        b->pBoost           = TRACE_PORT(ports[port_id++]);
                        b->pAttack          = TRACE_PORT(ports[port_id++]);
                        b->pRelease         = TRACE_PORT(ports[port_id++]);
                        b->pStereoLink      = (nChannels > 1) ? TRACE_PORT(ports[port_id++]) : NULL;
                    }

                    b->pReductionMeter  = TRACE_PORT(ports[port_id++]);
                }
            }
        }

        void mb_limiter::destroy()
        {
            Module::destroy();

            // Destroy channels
            if (vChannels != NULL)
            {
                for (size_t i=0; i<nChannels; ++i)
                {
                    channel_t *c    = &vChannels[i];

                    c->sBypass.destroy();
                    c->sDither.destroy();
                    c->sOver.destroy();
                    c->sScOver.destroy();
                    c->sScEQ.destroy();
                    c->sDryDelay.destroy();

                    // Destroy bands
                    for (size_t j=0; j<meta::mb_limiter::BANDS_MAX; ++j)
                    {
                        band_t *b   = &c->vBands[j];

                        b->sLimit.destroy();
                        b->sBlink.destroy();
                        b->sEQ.destroy();
                        b->sPassFilter.destroy();
                        b->sRejFilter.destroy();
                        b->sAllFilter.destroy();
                    }

                    // Destroy main limiter
                    limiter_t *l    = &c->sLimiter;
                    l->sLimit.destroy();
                    l->sBlink.destroy();
                }

                vChannels       = NULL;
            }

            // Destroy data
            if (pData != NULL)
            {
                free_aligned(pData);
                pData           = NULL;
            }
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


