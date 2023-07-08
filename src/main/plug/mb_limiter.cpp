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
#include <lsp-plug.in/dsp-units/misc/envelope.h>
#include <lsp-plug.in/dsp-units/util/Oversampler.h>
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
            bEnvUpdate          = true;
            fInGain             = GAIN_AMP_0_DB;
            fOutGain            = GAIN_AMP_0_DB;
            fZoom               = 1.0f;
            nEnvBoost           = -1;
            nRealSampleRate     = 0;
            nLookahead          = 0;

            vChannels           = NULL;
            vTmpBuf             = NULL;
            vEnvBuf             = NULL;
            vFreqs              = NULL;
            vIndexes            = NULL;
            vTr                 = NULL;
            vTrTmp              = NULL;
            vFc                 = NULL;
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
            for (size_t i=0; i<meta::mb_limiter::BANDS_MAX; ++i)
                vPlan[i]            = 0;

            pBypass             = NULL;
            pInGain             = NULL;
            pOutGain            = NULL;
            pLookahead          = NULL;
            pOversampling       = NULL;
            pDithering          = NULL;
            pEnvBoost           = NULL;
            pZoom               = NULL;
            pExtSc              = NULL;
            pReactivity         = NULL;
            pShift              = NULL;

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

            size_t an_id            = 0;
            size_t szof_channel     = align_size(sizeof(channel_t), OPTIMAL_ALIGN);
            size_t szof_indexes     = meta::mb_limiter::FFT_MESH_POINTS * sizeof(uint32_t);
            size_t szof_fft_graph   = meta::mb_limiter::FFT_MESH_POINTS * sizeof(float);
            size_t szof_buf         = BUFFER_SIZE * sizeof(float);
            size_t szof_ovs_buf     = szof_buf * meta::mb_limiter::OVERSAMPLING_MAX;
            size_t to_alloc         =
                szof_channel * nChannels +      // vChannels
                szof_ovs_buf +                  // vTmpBuf
                szof_ovs_buf +                  // vEnvBuf
                szof_fft_graph +                // vFreqs
                szof_indexes +                  // vIndexes
                szof_fft_graph * 2 +            // vTr
                szof_fft_graph * 2 +            // vTrTmp
                szof_fft_graph * 2 +            // vFc
                nChannels * (
                    szof_buf +                  // vData
                    szof_ovs_buf +              // vInBuf
                    szof_ovs_buf +              // vScBuf
                    szof_ovs_buf +              // vDataBuf
                    szof_fft_graph +            // vTrOut
                    meta::mb_limiter::BANDS_MAX * (
                        szof_fft_graph +        // vTrOut
                        szof_ovs_buf            // vVcaBuf
                    )
                );

            // Initialize analyzer
            if (!sAnalyzer.init(nChannels * 2, meta::mb_limiter::FFT_RANK,
                MAX_SAMPLE_RATE, meta::mb_limiter::REFRESH_RATE))
                return;
            sAnalyzer.set_rank(meta::mb_limiter::FFT_RANK);
            sAnalyzer.set_activity(false);
            sAnalyzer.set_envelope(dspu::envelope::WHITE_NOISE);
            sAnalyzer.set_window(meta::mb_limiter::FFT_WINDOW);
            sAnalyzer.set_rate(meta::mb_limiter::REFRESH_RATE);

            // Allocate data
            uint8_t *ptr            = alloc_aligned<uint8_t>(pData, to_alloc);
            if (ptr == NULL)
                return;

            // Allocate objects
            vChannels               = reinterpret_cast<channel_t *>(ptr);
            ptr                    += szof_channel * nChannels;
            vTmpBuf                 = reinterpret_cast<float *>(ptr);
            ptr                    += szof_ovs_buf;
            vEnvBuf                 = reinterpret_cast<float *>(ptr);
            ptr                    += szof_ovs_buf;
            vFreqs                  = reinterpret_cast<float *>(ptr);
            ptr                    += szof_fft_graph;
            vIndexes                = reinterpret_cast<uint32_t *>(ptr);
            ptr                    += szof_indexes;
            vTr                     = reinterpret_cast<float *>(ptr);
            ptr                    += szof_fft_graph * 2;
            vTrTmp                  = reinterpret_cast<float *>(ptr);
            ptr                    += szof_fft_graph * 2;
            vFc                     = reinterpret_cast<float *>(ptr);
            ptr                    += szof_fft_graph * 2;

            // Initialize objects
            float lk_latency        =
                floorf(dspu::samples_to_millis(MAX_SAMPLE_RATE, meta::mb_limiter::OVERSAMPLING_MAX)) +
                meta::mb_limiter::LOOKAHEAD_MAX + 1.0f;

            for (size_t i=0; i<nChannels; ++i)
            {
                channel_t *c    = &vChannels[i];

                // Initialize channel
                c->sBypass.construct();
                c->sDither.construct();
                c->sOver.construct();
                c->sScOver.construct();
                c->sScBoost.construct();
                c->sDataDelayMB.construct();
                c->sDataDelaySB.construct();
                c->sDryDelay.construct();

                if (!c->sScBoost.init(NULL))
                    return;
                // Initialize oversamplers
                if (!c->sOver.init())
                    return;
                if (!c->sScOver.init())
                    return;

                // Initialize
                if (!c->sDataDelayMB.init(dspu::millis_to_samples(MAX_SAMPLE_RATE * meta::mb_limiter::OVERSAMPLING_MAX, lk_latency) + BUFFER_SIZE))
                    return;
                if (!c->sDataDelaySB.init(dspu::millis_to_samples(MAX_SAMPLE_RATE * meta::mb_limiter::OVERSAMPLING_MAX, lk_latency) + BUFFER_SIZE))
                    return;
                if (!c->sDryDelay.init(dspu::millis_to_samples(MAX_SAMPLE_RATE, lk_latency*2 + c->sOver.max_latency())))
                    return;

                // Init bands
                for (size_t j=0; j<meta::mb_limiter::BANDS_MAX; ++j)
                {
                    band_t *b   = &c->vBands[j];

                    // limiter_t
                    b->sLimit.construct();

                    // Initialize limiter with latency compensation gap
                    if (!b->sLimit.init(MAX_SAMPLE_RATE * meta::mb_limiter::OVERSAMPLING_MAX, lk_latency))
                        return;

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
                    b->sEq.construct();
                    b->sPassFilter.construct();
                    b->sRejFilter.construct();
                    b->sAllFilter.construct();

                    // Initialize filters and equalizers
                    if (!b->sEq.init(2, 0))
                        return;
                    if (!b->sPassFilter.init(NULL))
                        return;
                    if (!b->sRejFilter.init(NULL))
                        return;
                    if (!b->sAllFilter.init(NULL))
                        return;
                    b->sEq.set_mode(dspu::EQM_IIR);

                    b->bSync            = false;
                    b->bMute            = false;
                    b->bSolo            = false;
                    b->fPreamp          = GAIN_AMP_0_DB;
                    b->fFreqStart       = 0.0f;
                    b->fFreqEnd         = 0.0f;
                    b->fMakeup          = GAIN_AMP_0_DB;
                    b->fReductionLevel  = GAIN_AMP_0_DB;

                    b->vTrOut           = reinterpret_cast<float *>(ptr);
                    ptr                += szof_fft_graph;
                    b->vVcaBuf          = reinterpret_cast<float *>(ptr);
                    ptr                += szof_ovs_buf;

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
                c->vData            = reinterpret_cast<float *>(ptr);
                ptr                += szof_buf;
                c->vInBuf           = reinterpret_cast<float *>(ptr);
                ptr                += szof_ovs_buf;
                c->vScBuf           = reinterpret_cast<float *>(ptr);
                ptr                += szof_ovs_buf;
                c->vDataBuf         = reinterpret_cast<float *>(ptr);
                ptr                += szof_ovs_buf;
                c->vTrOut           = reinterpret_cast<float *>(ptr);
                ptr                += szof_fft_graph;
                c->nAnInChannel     = an_id++;
                c->nAnOutChannel    = an_id++;

                for (size_t i=0; i<meta::mb_limiter::BANDS_MAX; ++i)
                    c->vPlan[i]         = 0;

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
            pZoom               = TRACE_PORT(ports[port_id++]);
            TRACE_PORT(ports[port_id++]); // Skip band filter curve control port
            pReactivity         = TRACE_PORT(ports[port_id++]);
            pShift              = TRACE_PORT(ports[port_id++]);
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

            // Destroy processors
            sAnalyzer.destroy();

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
                    c->sScBoost.destroy();
                    c->sDataDelayMB.destroy();
                    c->sDataDelaySB.destroy();
                    c->sDryDelay.destroy();

                    // Destroy bands
                    for (size_t j=0; j<meta::mb_limiter::BANDS_MAX; ++j)
                    {
                        band_t *b   = &c->vBands[j];

                        b->sLimit.destroy();
                        b->sEq.destroy();
                        b->sPassFilter.destroy();
                        b->sRejFilter.destroy();
                        b->sAllFilter.destroy();
                    }

                    // Destroy main limiter
                    limiter_t *l    = &c->sLimiter;
                    l->sLimit.destroy();
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
            // Update analyzer's sample rate
            sAnalyzer.set_sample_rate(sr);

            // Update channels
            for (size_t i=0; i<nChannels; ++i)
            {
                channel_t *c = &vChannels[i];

                c->sBypass.init(sr);
                c->sOver.set_sample_rate(sr);
                c->sScBoost.set_sample_rate(sr);

                // Update bands
                for (size_t j=0; j<meta::mb_limiter::BANDS_MAX; ++j)
                {
                    band_t *b  = &c->vBands[j];

                    b->sEq.set_sample_rate(sr);
                    b->sPassFilter.set_sample_rate(sr);
                    b->sRejFilter.set_sample_rate(sr);
                    b->sAllFilter.set_sample_rate(sr);
                }
            }

            // Force to rebuild plan
            nPlanSize        = 0;
        }

        size_t mb_limiter::decode_real_sample_rate(size_t mode)
        {
            switch (mode)
            {
                case meta::mb_limiter::OVS_HALF_2X2:
                case meta::mb_limiter::OVS_HALF_2X3:
                case meta::mb_limiter::OVS_FULL_2X2:
                case meta::mb_limiter::OVS_FULL_2X3:
                    return fSampleRate * 2;

                case meta::mb_limiter::OVS_HALF_3X2:
                case meta::mb_limiter::OVS_HALF_3X3:
                case meta::mb_limiter::OVS_FULL_3X2:
                case meta::mb_limiter::OVS_FULL_3X3:
                    return fSampleRate * 3;

                case meta::mb_limiter::OVS_HALF_4X2:
                case meta::mb_limiter::OVS_HALF_4X3:
                case meta::mb_limiter::OVS_FULL_4X2:
                case meta::mb_limiter::OVS_FULL_4X3:
                    return fSampleRate * 4;

                case meta::mb_limiter::OVS_HALF_6X2:
                case meta::mb_limiter::OVS_HALF_6X3:
                case meta::mb_limiter::OVS_FULL_6X2:
                case meta::mb_limiter::OVS_FULL_6X3:
                    return fSampleRate * 6;

                case meta::mb_limiter::OVS_HALF_8X2:
                case meta::mb_limiter::OVS_HALF_8X3:
                case meta::mb_limiter::OVS_FULL_8X2:
                case meta::mb_limiter::OVS_FULL_8X3:
                    return fSampleRate * 8;

                default:
                    break;
            }

            return fSampleRate;
        }

        dspu::limiter_mode_t mb_limiter::decode_limiter_mode(ssize_t mode)
        {
            switch (mode)
            {
                case meta::mb_limiter::LOM_HERM_THIN:
                    return dspu::LM_HERM_THIN;
                case meta::mb_limiter::LOM_HERM_WIDE:
                    return dspu::LM_HERM_WIDE;
                case meta::mb_limiter::LOM_HERM_TAIL:
                    return dspu::LM_HERM_TAIL;
                case meta::mb_limiter::LOM_HERM_DUCK:
                    return dspu::LM_HERM_DUCK;

                case meta::mb_limiter::LOM_EXP_THIN:
                    return dspu::LM_EXP_THIN;
                case meta::mb_limiter::LOM_EXP_WIDE:
                    return dspu::LM_EXP_WIDE;
                case meta::mb_limiter::LOM_EXP_TAIL:
                    return dspu::LM_EXP_TAIL;
                case meta::mb_limiter::LOM_EXP_DUCK:
                    return dspu::LM_EXP_DUCK;

                case meta::mb_limiter::LOM_LINE_THIN:
                    return dspu::LM_LINE_THIN;
                case meta::mb_limiter::LOM_LINE_WIDE:
                    return dspu::LM_LINE_WIDE;
                case meta::mb_limiter::LOM_LINE_TAIL:
                    return dspu::LM_LINE_TAIL;
                case meta::mb_limiter::LOM_LINE_DUCK:
                    return dspu::LM_LINE_DUCK;

                default:
                    break;
            }
            return dspu::LM_HERM_THIN;
        }

        dspu::over_mode_t mb_limiter::decode_oversampling_mode(size_t mode)
        {
            #define L_KEY(x) \
                case meta::mb_limiter::OVS_HALF_ ## x: \
                case meta::mb_limiter::OVS_FULL_ ## x: \
                    return dspu::OM_LANCZOS_ ## x;

            switch (mode)
            {
                L_KEY(2X2)
                L_KEY(2X3)
                L_KEY(3X2)
                L_KEY(3X3)
                L_KEY(4X2)
                L_KEY(4X3)
                L_KEY(6X2)
                L_KEY(6X3)
                L_KEY(8X2)
                L_KEY(8X3)

                case meta::mb_limiter::OVS_NONE:
                default:
                    return dspu::OM_NONE;
            }
            #undef L_KEY
            return dspu::OM_NONE;
        }

        bool mb_limiter::decode_filtering(size_t mode)
        {
            return (mode >= meta::mb_limiter::OVS_FULL_2X2) && (mode <= meta::mb_limiter::OVS_FULL_8X3);
        }

        size_t mb_limiter::decode_dithering(size_t mode)
        {
            switch (mode)
            {
                case meta::mb_limiter::DITHER_7BIT:     return 7;
                case meta::mb_limiter::DITHER_8BIT:     return 8;
                case meta::mb_limiter::DITHER_11BIT:    return 11;
                case meta::mb_limiter::DITHER_12BIT:    return 12;
                case meta::mb_limiter::DITHER_15BIT:    return 15;
                case meta::mb_limiter::DITHER_16BIT:    return 16;
                case meta::mb_limiter::DITHER_23BIT:    return 23;
                case meta::mb_limiter::DITHER_24BIT:    return 24;
                case meta::mb_limiter::DITHER_NONE:
                default:
                    return 0;
            }
            return 0;
        }

        void mb_limiter::update_settings()
        {
            dspu::filter_params_t fp;

            // Determine number of channels
            bool rebuild_bands          = nPlanSize <= 0;
            int active_channels         = 0;
            size_t env_boost            = pEnvBoost->value();

            // Check that real sample rate has changed
            size_t ovs_mode             = pOversampling->value();
            dspu::over_mode_t over_mode = decode_oversampling_mode(ovs_mode);
            bool over_filtering         = decode_filtering(ovs_mode);
            float real_srate            = decode_real_sample_rate(ovs_mode);
            size_t dither_bits          = decode_dithering(pDithering->value());
            if (real_srate != nRealSampleRate)
            {
                for (size_t i=0; i<nChannels; ++i)
                {
                    vChannels[i].sDataDelayMB.clear();
                    vChannels[i].sDataDelaySB.clear();
                }
                nRealSampleRate     = real_srate;
                rebuild_bands       = true;
            }

            // Store gain
            fInGain             = pInGain->value();
            fOutGain            = pOutGain->value();
            fZoom               = pZoom->value();

            // Update frequency split bands
            for (size_t i=0; i<meta::mb_limiter::BANDS_MAX-1; ++i)
            {
                split_t *s      = &vSplits[i];

                bool enabled    = s->pEnabled->value() >= 0.5f;
                if (enabled != s->bEnabled)
                {
                    s->bEnabled     = enabled;
                    rebuild_bands   = true;
                }

                float freq      = s->pFreq->value();
                if ((enabled) && (freq != s->fFreq))
                {
                    s->fFreq        = freq;
                    rebuild_bands   = true;
                }
            }

            // Rebuild compression plan
            if (rebuild_bands)
            {
                nPlanSize               = 1;
                vPlan[0]                = 0;    // 0th band is always present

                for (size_t i=0; i<meta::mb_limiter::BANDS_MAX-1; ++i)
                {
                    if (vSplits[i].bEnabled)
                        vPlan[nPlanSize++]    = i+1;
                }

                // Do simple sort of PLAN items by frequency
                if (nPlanSize > 1)
                {
                    // Sort plan in frequency-descending order
                    for (size_t si=1; si < nPlanSize-1; ++si)
                        for (size_t sj=si+1; sj < nPlanSize; ++sj)
                        {
                            size_t p1 = vPlan[si] - 1, p2 = vPlan[sj] - 1;
                            if (vSplits[p1].fFreq > vSplits[p2].fFreq)
                                lsp::swap(vPlan[si], vPlan[sj]);
                        }
                }
            }

            // Configure channels (first pass)
            for (size_t i=0; i<nChannels; ++i)
            {
                channel_t *c    = &vChannels[i];

                // Update bypass settings
                c->sBypass.set_bypass(pBypass->value());

                // Update analyzer settings
                c->bFftIn       = c->pFftInEnable->value() >= 0.5f;
                c->bFftOut      = c->pFftOutEnable->value() >= 0.5f;

                sAnalyzer.enable_channel(c->nAnInChannel, c->bFftIn);
                sAnalyzer.enable_channel(c->nAnOutChannel, c->bFftOut);

                if (sAnalyzer.channel_active(c->nAnInChannel))
                    active_channels ++;
                if (sAnalyzer.channel_active(c->nAnOutChannel))
                    active_channels ++;

                // Update envelope boost filters
                if ((env_boost != nEnvBoost) || (bEnvUpdate))
                {
                    fp.fFreq        = meta::mb_limiter::FREQ_BOOST_MIN;
                    fp.fFreq2       = 0.0f;
                    fp.fGain        = 1.0f;
                    fp.fQuality     = 0.0f;

                    switch (env_boost)
                    {
                        case meta::mb_limiter::FB_BT_3DB:
                            fp.nType        = dspu::FLT_BT_RLC_ENVELOPE;
                            fp.nSlope       = 1;
                            break;
                        case meta::mb_limiter::FB_MT_3DB:
                            fp.nType        = dspu::FLT_MT_RLC_ENVELOPE;
                            fp.nSlope       = 1;
                            break;
                        case meta::mb_limiter::FB_BT_6DB:
                            fp.nType        = dspu::FLT_BT_RLC_ENVELOPE;
                            fp.nSlope       = 2;
                            break;
                        case meta::mb_limiter::FB_MT_6DB:
                            fp.nType        = dspu::FLT_MT_RLC_ENVELOPE;
                            fp.nSlope       = 2;
                            break;
                        case meta::mb_limiter::FB_OFF:
                        default:
                            fp.nType        = dspu::FLT_NONE;
                            fp.nSlope       = 1;
                            break;
                    }

                    c->sScBoost.update(fSampleRate, &fp);
                }
            }

            // Update analyzer parameters
            sAnalyzer.set_reactivity(pReactivity->value());
            if (pShift != NULL)
                sAnalyzer.set_shift(pShift->value() * 100.0f);
            sAnalyzer.set_activity(active_channels > 0);

            // Update analyzer
            if (sAnalyzer.needs_reconfiguration())
            {
                sAnalyzer.reconfigure();
                sAnalyzer.get_frequencies(vFreqs, vIndexes, SPEC_FREQ_MIN, SPEC_FREQ_MAX, meta::mb_limiter::FFT_MESH_POINTS);
            }

            // Estimate lookahead buffer size
            float lookahead = pLookahead->value();
            nLookahead      = dspu::millis_to_samples(nRealSampleRate, lookahead);

//            size_t latency = 0;
            bool has_solo  = false;

            // Configure channels (second pass)
            for (size_t i=0; i<nChannels; ++i)
            {
                channel_t *c    = &vChannels[i];

                // Update lookahead delay settings for multiband and single band limiter
                c->sDataDelayMB.set_delay(nLookahead);
                c->sDataDelaySB.set_delay(nLookahead);

                // Update compressor bands
                for (size_t j=0; j<meta::mb_limiter::BANDS_MAX; ++j)
                {
                    band_t *b       = &c->vBands[j];

                    dspu::limiter_mode_t limiter_mode = decode_limiter_mode(b->pMode->value());
                    bool enabled    = b->pEnable->value() >= 0.5f;
                    bool boost      = b->pBoost->value() >= 0.5f;
                    if (enabled && (j > 0))
                        enabled         = vSplits[j-1].bEnabled;

                    b->fPreamp      = b->pPreamp->value();
                    b->bMute        = (b->pMute->value() >= 0.5f);
                    b->bSolo        = (enabled) && (b->pSolo->value() >= 0.5f);
                    b->fMakeup      = b->pMakeup->value();

                    if (b->bSolo)
                        has_solo            = true;

                    // Update settings for oversamplers
                    c->sOver.set_mode(over_mode);
                    c->sOver.set_filtering(over_filtering);
                    if (c->sOver.modified())
                        c->sOver.update_settings();

                    c->sScOver.set_mode(over_mode);
                    c->sScOver.set_filtering(false);
                    if (c->sScOver.modified())
                        c->sScOver.update_settings();

                    // Configure dither noise
                    c->sDither.set_bits(dither_bits);

                    // Update settings for limiter
                    b->sLimit.set_mode(limiter_mode);
                    b->sLimit.set_sample_rate(nRealSampleRate);
                    b->sLimit.set_lookahead(lookahead);
                    b->sLimit.set_threshold(b->pThresh->value(), !boost);
                    b->sLimit.set_attack(b->pAttack->value());
                    b->sLimit.set_release(b->pRelease->value());
                    b->sLimit.set_knee(b->pAlrKnee->value());
                    b->sLimit.set_alr(b->pAlrOn->value() >= 0.5f);
                    b->sLimit.set_alr_attack(b->pAlrAttack->value());
                    b->sLimit.set_alr_release(b->pAlrRelease->value());
                }
            }

            // Configure channels (third pass)
            for (size_t i=0; i<nChannels; ++i)
            {
                channel_t *c    = &vChannels[i];

                // Check muting option
                for (size_t j=0; j<meta::mb_limiter::BANDS_MAX; ++j)
                {
                    band_t *b       = &c->vBands[j];
                    if ((!b->bMute) && (has_solo))
                        b->bMute        = !b->bSolo;
                    if (rebuild_bands)
                        b->bEnabled     = false;
                }

                c->sScBoost.set_sample_rate(real_srate);

                // Rebuild compression plan
                if (rebuild_bands)
                {
                    // Configure equalizers
                    lsp_trace("Configure bands according to new plan");

                    // Disable all bands first
                    for (size_t j=0; j<nPlanSize; ++j)
                    {
                        size_t band_id  = vPlan[j];

                        band_t *b       = &c->vBands[band_id];
                        b->bEnabled     = true;
                        b->fFreqStart   = (band_id > 0) ? vSplits[band_id-1].fFreq : 0.0f;
                        c->vPlan[j]     = b;
                    }

                    // Process only enabled bands
                    for (size_t j=0; j < nPlanSize; ++j)
                    {
                        band_t *b       = c->vPlan[j];

                        b->sEq.set_sample_rate(nRealSampleRate);

                        // Check that band is enabled
                        b->bSync        = true;
                        b->fFreqEnd     = (j < (nPlanSize - 1)) ? c->vPlan[j+1]->fFreqStart : fSampleRate >> 1;
//                        lsp_trace("[%d]: %f - %f", int(j), b->fFreqStart, b->fFreqEnd);
                        b->pFreqEnd->set_value(b->fFreqEnd);

                        // Configure lo-pass sidechain filter
                        fp.nType        = (j < (nPlanSize - 1)) ? dspu::FLT_BT_LRX_LOPASS : dspu::FLT_NONE;
                        fp.fFreq        = b->fFreqEnd;
                        fp.fFreq2       = fp.fFreq;
                        fp.fQuality     = 0.0f;
                        fp.fGain        = 1.0f;
                        fp.fQuality     = 0.0f;
                        fp.nSlope       = meta::mb_limiter::FILTER_SLOPE;

                        b->sEq.set_params(0, &fp);

                        // Configure hi-pass sidechain filter
                        fp.nType        = (j > 0) ? dspu::FLT_BT_LRX_HIPASS : dspu::FLT_NONE;
                        fp.fFreq        = b->fFreqStart;
                        fp.fFreq2       = fp.fFreq;
                        fp.fQuality     = 0.0f;
                        fp.fGain        = 1.0f;
                        fp.fQuality     = 0.0f;
                        fp.nSlope       = meta::mb_limiter::FILTER_SLOPE;

                        b->sEq.set_params(1, &fp);

                        // Update transfer function for equalizer
                        b->sEq.freq_chart(vTr, vFreqs, meta::mb_limiter::FFT_MESH_POINTS);
                        dsp::pcomplex_mod(b->vTrOut, vTr, meta::mb_limiter::FFT_MESH_POINTS);

                        // Update filter parameters
                        fp.fGain        = 1.0f;
                        fp.nSlope       = meta::mb_limiter::FILTER_SLOPE;
                        fp.fQuality     = 0.0f;
                        fp.fFreq        = b->fFreqEnd;
                        fp.fFreq2       = b->fFreqEnd;

                        // We're going from low frequencies to high frequencies
                        if (j >= (nPlanSize - 1))
                        {
                            fp.nType    = dspu::FLT_NONE;
                            b->sPassFilter.update(fSampleRate, &fp);
                            b->sRejFilter.update(fSampleRate, &fp);
                            b->sAllFilter.update(fSampleRate, &fp);
                        }
                        else
                        {
                            fp.nType    = dspu::FLT_BT_LRX_LOPASS;
                            b->sPassFilter.update(fSampleRate, &fp);
                            fp.nType    = dspu::FLT_BT_LRX_HIPASS;
                            b->sRejFilter.update(fSampleRate, &fp);
                            fp.nType    = (j == 0) ? dspu::FLT_NONE : dspu::FLT_BT_LRX_ALLPASS;
                            b->sAllFilter.update(fSampleRate, &fp);
                        }

                        b->sPassFilter.set_sample_rate(nRealSampleRate);
                        b->sRejFilter.set_sample_rate(nRealSampleRate);
                        b->sAllFilter.set_sample_rate(nRealSampleRate);
                    }
                } // nPlanSize

                // TODO: Calculate latency
//                for (size_t j=0; j<c->nPlanSize; ++j)
//                {
//                    comp_band_t *b  = c->vPlan[j];
//                    latency         = lsp_max(latency, b->nLookahead);
//                }
            }

            nEnvBoost           = env_boost;
            bEnvUpdate          = false;
        }

        void mb_limiter::compute_multiband_vca_gain(channel_t *c, size_t samples)
        {
            size_t ovs_samples = samples * vChannels[0].sScOver.get_oversampling();

            // Estimate the VCA gain for each band
            for (size_t j=0; j<nPlanSize; ++j)
            {
                band_t *b       = c->vPlan[j];

                // Prepare sidechain signal with band equalizers
                b->sEq.process(vTmpBuf, c->vScBuf, ovs_samples);

                if (b->bEnabled)
                {
                    b->sLimit.process(b->vVcaBuf, vTmpBuf, ovs_samples);
                    dsp::mul_k2(b->vVcaBuf, b->fMakeup, ovs_samples);

                    // Output curve level
                    float reduction     = dsp::min(b->vVcaBuf, ovs_samples);
                    b->fReductionLevel  = lsp_min(b->fReductionLevel, reduction);

                    // Check muting option
                    if (b->bMute)
                        dsp::fill(b->vVcaBuf, GAIN_AMP_M_60_DB, ovs_samples);
                }
                else
                {
                    dsp::fill(b->vVcaBuf, (b->bMute) ? GAIN_AMP_M_60_DB : GAIN_AMP_0_DB, ovs_samples);
                    b->fReductionLevel       = lsp_min(b->fReductionLevel, GAIN_AMP_0_DB);
                }
            }
        }

        void mb_limiter::apply_multiband_vca_gain(channel_t *c, size_t samples)
        {
            size_t ovs_samples = samples * vChannels[0].sScOver.get_oversampling();

            // Here, we apply VCA to input signal dependent on the input
            // Apply delay to compensate lookahead feature
            c->sDataDelayMB.process(vTmpBuf, c->vInBuf, ovs_samples);

            // Originally, there is no signal
            dsp::fill_zero(c->vDataBuf, ovs_samples);           // Clear the channel data buffer

            for (size_t j=0; j<nPlanSize; ++j)
            {
                band_t *b       = c->vPlan[j];

                b->sAllFilter.process(c->vDataBuf, c->vDataBuf, ovs_samples);   // Process the signal with all-pass
                b->sPassFilter.process(vEnvBuf, vTmpBuf, ovs_samples);          // Filter frequencies from input
                dsp::fmadd3(c->vDataBuf, vEnvBuf, b->vVcaBuf, ovs_samples);     // Apply VCA gain to band and add to output data buffer
                b->sRejFilter.process(vTmpBuf, vTmpBuf, ovs_samples);           // Filter frequencies from input
            }
        }

        void mb_limiter::process(size_t samples)
        {
            // Bind input signal
            for (size_t i=0; i<nChannels; ++i)
            {
                channel_t *c        = &vChannels[i];

                c->vIn              = c->pIn->buffer<float>();
                c->vOut             = c->pOut->buffer<float>();
                c->vSc              = (c->pSc != NULL) ? c->pSc->buffer<float>() : c->vIn;

                for (size_t i=0; i<meta::mb_limiter::BANDS_MAX; ++i)
                {
                    band_t *b           = &c->vBands[i];
                    b->fReductionLevel  = GAIN_AMP_P_96_DB;
                }
            }

            // Do main processing
            for (size_t offset=0; offset < samples;)
            {
                // Compute number of samples to process
                size_t count        = lsp_min(samples - offset, BUFFER_SIZE);

                oversample_data(count);
                for (size_t i=0; i<nChannels; ++i)
                    compute_multiband_vca_gain(&vChannels[i], count);
                for (size_t i=0; i<nChannels; ++i)
                    apply_multiband_vca_gain(&vChannels[i], count);
                downsample_data(count);
                perform_fft_analysis(count);

                // Output audio
                for (size_t i=0; i<nChannels; ++i)
                {
                    channel_t *c        = &vChannels[i];
                    dsp::copy(c->vOut, c->vData, count);
                }

                // Update pointers
                for (size_t i=0; i<nChannels; ++i)
                {
                    channel_t *c        = &vChannels[i];
                    c->vIn             += count;
                    c->vOut            += count;
                    c->vSc             += count;
                }
                offset += samples;
            }

            // Output FFT graphs to the UI
            output_meters();
            output_fft_curves();

            // Request for redraw
            if (pWrapper != NULL)
                pWrapper->query_display_draw();
        }

        void mb_limiter::oversample_data(size_t samples)
        {
            // Apply input gain if needed
            size_t ovs_samples = samples * vChannels[0].sScOver.get_oversampling();

            for (size_t i=0; i<nChannels; ++i)
            {
                channel_t *c        = &vChannels[i];
                if (fInGain != GAIN_AMP_0_DB)
                {
                    dsp::mul_k3(c->vData, c->vIn, fInGain, samples);
                    c->sOver.upsample(c->vInBuf, c->vData, samples);
                }
                else
                    c->sOver.upsample(c->vInBuf, c->vIn, samples);

                // Process sidechain signal
                if ((c->vSc != NULL) && (bExtSc))
                    c->sScOver.upsample(c->vScBuf, c->vSc, samples);
                else
                    dsp::copy(c->vScBuf, c->vInBuf, ovs_samples);
            }
        }

        void mb_limiter::downsample_data(size_t samples)
        {
            for (size_t i=0; i<nChannels; ++i)
            {
                channel_t *c        = &vChannels[i];

                c->sOver.downsample(c->vData, c->vDataBuf, samples);                // Downsample
                c->sDither.process(c->vData, c->vData, samples);                    // Apply dithering
            }
        }

        void mb_limiter::perform_fft_analysis(size_t samples)
        {
            // Prepare processing
            const float *bufs[4] = { NULL, NULL, NULL, NULL };
            for (size_t i=0; i<nChannels; ++i)
            {
                channel_t *c            = &vChannels[i];
                bufs[c->nAnInChannel]   = c->vIn;
                bufs[c->nAnOutChannel]  = c->vData;
            }

            // Perform processing
            sAnalyzer.process(bufs, samples);
        }

        void mb_limiter::output_meters()
        {
            for (size_t i=0; i<nChannels; ++i)
            {
                channel_t *c        = &vChannels[i];

                for (size_t j=0; j<meta::mb_limiter::BANDS_MAX; ++j)
                {
                    band_t *b           = &c->vBands[j];

                    float reduction     = (b->bEnabled) ? b->fReductionLevel : GAIN_AMP_0_DB;
                    b->pReductionMeter->set_value(reduction);
                }
            }
        }

        void mb_limiter::output_fft_curves()
        {
            // Output filter curve for each band
            for (size_t j=0; j<meta::mb_limiter::BANDS_MAX; ++j)
            {
                band_t *b           = &vChannels[0].vBands[j];

                // FFT spectrogram
                plug::mesh_t *mesh  = NULL;

                // FFT curve
                if (b->bSync)
                {
                    mesh                = (b->pBandGraph != NULL) ? b->pBandGraph->buffer<plug::mesh_t>() : NULL;
                    if ((mesh != NULL) && (mesh->isEmpty()))
                    {
                        // Add extra points
                        mesh->pvData[0][0] = SPEC_FREQ_MIN*0.5f;
                        mesh->pvData[0][meta::mb_limiter::FFT_MESH_POINTS+1] = SPEC_FREQ_MAX * 2.0f;
                        mesh->pvData[1][0] = 0.0f;
                        mesh->pvData[1][meta::mb_limiter::FFT_MESH_POINTS+1] = 0.0f;

                        // Fill mesh
                        dsp::copy(&mesh->pvData[0][1], vFreqs, meta::mb_limiter::FFT_MESH_POINTS);
                        dsp::mul_k3(&mesh->pvData[1][1], b->vTrOut, b->fPreamp, meta::mb_limiter::FFT_MESH_POINTS);
                        mesh->data(2, meta::mb_limiter::FFT_MESH_POINTS + 2);

                        // Mark mesh as synchronized
                        b->bSync            = false;
                    }
                }
            }

            // Output FFT curves for each channel
            for (size_t i=0; i<nChannels; ++i)
            {
                channel_t *c     = &vChannels[i];

                // Calculate transfer function
                dsp::pcomplex_fill_ri(vTrTmp, 1.0f, 0.0f, meta::mb_limiter::FFT_MESH_POINTS);
                dsp::fill_zero(vTr, meta::mb_limiter::FFT_MESH_POINTS*2);

                // Calculate transfer function
                for (size_t j=0; j<nPlanSize; ++j)
                {
                    band_t *b       = c->vPlan[j];

                    // Apply all-pass characteristics
                    b->sAllFilter.freq_chart(vFc, vFreqs, meta::mb_limiter::FFT_MESH_POINTS);
                    dsp::pcomplex_mul2(vTr, vFc, meta::mb_limiter::FFT_MESH_POINTS);

                    // Apply lo-pass filter characteristics
                    b->sPassFilter.freq_chart(vFc, vFreqs, meta::mb_limiter::FFT_MESH_POINTS);
                    dsp::pcomplex_mul2(vFc, vTrTmp, meta::mb_limiter::FFT_MESH_POINTS);
                    dsp::fmadd_k3(vTr, vFc, b->fReductionLevel, meta::mb_limiter::FFT_MESH_POINTS*2);

                    // Apply hi-pass filter characteristics
                    b->sRejFilter.freq_chart(vFc, vFreqs, meta::mb_limiter::FFT_MESH_POINTS);
                    dsp::pcomplex_mul2(vTrTmp, vFc, meta::mb_limiter::FFT_MESH_POINTS);
                }
                dsp::pcomplex_mod(c->vTrOut, vTr, meta::mb_limiter::FFT_MESH_POINTS);

                // Output FFT curve for input
                plug::mesh_t *mesh            = (c->pFftIn != NULL) ? c->pFftIn->buffer<plug::mesh_t>() : NULL;
                if ((mesh != NULL) && (mesh->isEmpty()))
                {
                    if ((c->bFftIn) && (sAnalyzer.channel_active(c->nAnInChannel)))
                    {
                        // Add extra points
                        mesh->pvData[0][0] = SPEC_FREQ_MIN*0.5f;
                        mesh->pvData[0][meta::mb_limiter::FFT_MESH_POINTS+1] = SPEC_FREQ_MAX * 2.0f;
                        mesh->pvData[1][0] = 0.0f;
                        mesh->pvData[1][meta::mb_limiter::FFT_MESH_POINTS+1] = 0.0f;

                        // Copy frequency points
                        dsp::copy(&mesh->pvData[0][1], vFreqs, meta::mb_limiter::FFT_MESH_POINTS);
                        sAnalyzer.get_spectrum(c->nAnInChannel, &mesh->pvData[1][1], vIndexes, meta::mb_limiter::FFT_MESH_POINTS);

                        // Mark mesh containing data
                        mesh->data(2, meta::mb_limiter::FFT_MESH_POINTS + 2);
                    }
                    else
                        mesh->data(2, 0);
                }

                // Output FFT curve for output
                mesh            = (c->pFftOut != NULL) ? c->pFftOut->buffer<plug::mesh_t>() : NULL;
                if ((mesh != NULL) && (mesh->isEmpty()))
                {
                    if ((c->bFftOut) && (sAnalyzer.channel_active(c->nAnOutChannel)))
                    {
                        // Copy frequency points
                        dsp::copy(mesh->pvData[0], vFreqs, meta::mb_limiter::FFT_MESH_POINTS);
                        sAnalyzer.get_spectrum(c->nAnOutChannel, mesh->pvData[1], vIndexes, meta::mb_limiter::FFT_MESH_POINTS);

                        // Mark mesh containing data
                        mesh->data(2, meta::mb_limiter::FFT_MESH_POINTS);
                    }
                    else
                        mesh->data(2, 0);
                }

                // Output Channel curve
                mesh            = (c->pFilterGraph != NULL) ? c->pFilterGraph->buffer<plug::mesh_t>() : NULL;
                if ((mesh != NULL) && (mesh->isEmpty()))
                {
                    // Calculate amplitude (modulo)
                    dsp::copy(mesh->pvData[0], vFreqs, meta::mb_limiter::FFT_MESH_POINTS);
                    dsp::copy(mesh->pvData[1], c->vTrOut, meta::mb_limiter::FFT_MESH_POINTS);
                    mesh->data(2, meta::mb_limiter::FFT_MESH_POINTS);
                }
            } // for channel
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


