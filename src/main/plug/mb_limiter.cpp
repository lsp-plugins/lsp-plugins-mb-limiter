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
#include <lsp-plug.in/common/bits.h>
#include <lsp-plug.in/common/debug.h>
#include <lsp-plug.in/dsp/dsp.h>
#include <lsp-plug.in/dsp-units/misc/envelope.h>
#include <lsp-plug.in/dsp-units/util/Oversampler.h>
#include <lsp-plug.in/dsp-units/units.h>
#include <lsp-plug.in/plug-fw/meta/func.h>
#include <lsp-plug.in/shared/id_colors.h>

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
            nMode               = XOVER_CLASSIC;
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
            pMode               = NULL;
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
            do_destroy();
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
                    szof_ovs_buf +              // vVcaBuf
                    meta::mb_limiter::BANDS_MAX * (
                        szof_fft_graph +        // vTrOut
                        szof_ovs_buf +          // vDataBuf
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
            lsp_guard_assert( const uint8_t *tail = &ptr[to_alloc]; );

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
                c->sFFTXOver.construct();
                c->sFFTScXOver.construct();
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

                c->sDither.init();

                // Initialize
                if (!c->sDataDelayMB.init(dspu::millis_to_samples(MAX_SAMPLE_RATE * meta::mb_limiter::OVERSAMPLING_MAX, lk_latency) + BUFFER_SIZE))
                    return;
                if (!c->sDataDelaySB.init(dspu::millis_to_samples(MAX_SAMPLE_RATE * meta::mb_limiter::OVERSAMPLING_MAX, lk_latency) + BUFFER_SIZE))
                    return;

                // Init main limiter
                limiter_t *l    = &c->sLimiter;

                l->sLimit.construct();

                // Initialize limiter with latency compensation gap
                if (!l->sLimit.init(MAX_SAMPLE_RATE * meta::mb_limiter::OVERSAMPLING_MAX, lk_latency))
                    return;

                l->bEnabled         = false;
                l->fStereoLink      = 0.0f;
                l->fInLevel         = GAIN_AMP_M_INF_DB;
                l->fReductionLevel  = GAIN_AMP_0_DB;
                l->vVcaBuf          = reinterpret_cast<float *>(ptr);
                ptr                += szof_ovs_buf;

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
                l->pInMeter         = NULL;
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

                // Init bands
                for (size_t j=0; j<meta::mb_limiter::BANDS_MAX; ++j)
                {
                    band_t *b   = &c->vBands[j];

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

                    b->vDataBuf         = reinterpret_cast<float *>(ptr);
                    ptr                += szof_ovs_buf;
                    b->vTrOut           = reinterpret_cast<float *>(ptr);
                    ptr                += szof_fft_graph;

                    b->pFreqEnd         = NULL;
                    b->pSolo            = NULL;
                    b->pMute            = NULL;
                    b->pPreamp          = NULL;
                    b->pMakeup          = NULL;
                    b->pBandGraph       = NULL;

                    // limiter_t
                    limiter_t *l        = &b->sLimiter;
                    l->sLimit.construct();

                    // Initialize limiter with latency compensation gap
                    if (!l->sLimit.init(MAX_SAMPLE_RATE * meta::mb_limiter::OVERSAMPLING_MAX, lk_latency))
                        return;

                    l->bEnabled         = false;
                    l->fStereoLink      = 0.0f;
                    l->fInLevel         = GAIN_AMP_M_INF_DB;
                    l->fReductionLevel  = GAIN_AMP_0_DB;
                    l->vVcaBuf          = reinterpret_cast<float *>(ptr);
                    ptr                += szof_ovs_buf;

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
                    l->pInMeter         = NULL;
                    l->pStereoLink      = NULL;
                    l->pReductionMeter  = NULL;
                }
            }

            lsp_assert( ptr <= tail );

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
            pMode               = TRACE_PORT(ports[port_id++]);
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
                    l->pInMeter         = NULL;
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
                    l->pInMeter         = TRACE_PORT(ports[port_id++]);
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
                        limiter_t *sl       = &sb->sLimiter;
                        limiter_t *l        = &b->sLimiter;

                        b->pFreqEnd         = sb->pFreqEnd;
                        b->pSolo            = sb->pSolo;
                        b->pMute            = sb->pMute;
                        b->pPreamp          = sb->pPreamp;
                        b->pMakeup          = sb->pMakeup;
                        b->pBandGraph       = NULL;

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
                        l->pInMeter         = NULL;
                        l->pStereoLink      = NULL;
                    }
                    else
                    {
                        limiter_t *l        = &b->sLimiter;

                        b->pFreqEnd         = TRACE_PORT(ports[port_id++]);
                        b->pSolo            = TRACE_PORT(ports[port_id++]);
                        b->pMute            = TRACE_PORT(ports[port_id++]);
                        b->pPreamp          = TRACE_PORT(ports[port_id++]);
                        b->pMakeup          = TRACE_PORT(ports[port_id++]);
                        b->pBandGraph       = TRACE_PORT(ports[port_id++]);

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
                        l->pInMeter         = TRACE_PORT(ports[port_id++]);
                        l->pStereoLink      = (nChannels > 1) ? TRACE_PORT(ports[port_id++]) : NULL;
                    }

                    b->sLimiter.pReductionMeter     = TRACE_PORT(ports[port_id++]);
                }
            }
        }

        void mb_limiter::destroy()
        {
            plug::Module::destroy();
            do_destroy();
        }

        void mb_limiter::do_destroy()
        {
            // Destroy processors
            sAnalyzer.destroy();

            // Destroy channels
            if (vChannels != NULL)
            {
                for (size_t i=0; i<nChannels; ++i)
                {
                    channel_t *c    = &vChannels[i];

                    c->sBypass.destroy();
                    c->sFFTXOver.destroy();
                    c->sFFTScXOver.destroy();
                    c->sDither.destroy();
                    c->sOver.destroy();
                    c->sScOver.destroy();
                    c->sScBoost.destroy();
                    c->sDataDelayMB.destroy();
                    c->sDataDelaySB.destroy();
                    c->sDryDelay.destroy();
                    c->sLimiter.sLimit.destroy();

                    // Destroy bands
                    for (size_t j=0; j<meta::mb_limiter::BANDS_MAX; ++j)
                    {
                        band_t *b   = &c->vBands[j];

                        b->sLimiter.sLimit.destroy();
                        b->sEq.destroy();
                        b->sPassFilter.destroy();
                        b->sRejFilter.destroy();
                        b->sAllFilter.destroy();
                    }
                }

                vChannels       = NULL;
            }

            // Destroy inline display buffers
            if (pIDisplay != NULL)
            {
                pIDisplay->destroy();
                pIDisplay   = NULL;
            }

            // Destroy data
            if (pData != NULL)
            {
                free_aligned(pData);
                pData           = NULL;
            }
        }

        size_t mb_limiter::select_fft_rank(size_t sample_rate)
        {
            const size_t k = (sample_rate + meta::mb_limiter::FFT_XOVER_FREQ_MIN/2) / meta::mb_limiter::FFT_XOVER_FREQ_MIN;
            const size_t n = int_log2(k);
            return meta::mb_limiter::FFT_XOVER_RANK_MIN + n;
        }

        void mb_limiter::update_sample_rate(long sr)
        {
            size_t fft_rank     = select_fft_rank(sr * meta::mb_limiter::OVERSAMPLING_MAX);
            size_t bins         = 1 << fft_rank;
            float lk_latency        =
                floorf(dspu::samples_to_millis(MAX_SAMPLE_RATE, meta::mb_limiter::OVERSAMPLING_MAX)) +
                meta::mb_limiter::LOOKAHEAD_MAX + 1.0f;

            // Update analyzer's sample rate
            sAnalyzer.set_sample_rate(sr);

            // Update channels
            for (size_t i=0; i<nChannels; ++i)
            {
                channel_t *c = &vChannels[i];

                size_t max_lat      = dspu::millis_to_samples(MAX_SAMPLE_RATE, lk_latency*2 + c->sOver.max_latency()) + bins;

                c->sBypass.init(sr);
                c->sOver.set_sample_rate(sr);
                c->sScBoost.set_sample_rate(sr);
                c->sDryDelay.init(max_lat);

                // Need to re-initialize FFT crossovers?
                if (fft_rank != c->sFFTXOver.rank())
                {
                    c->sFFTXOver.init(fft_rank, meta::mb_limiter::BANDS_MAX);
                    c->sFFTScXOver.init(fft_rank, meta::mb_limiter::BANDS_MAX);
                    for (size_t j=0; j<meta::mb_limiter::BANDS_MAX; ++j)
                    {
                        c->sFFTXOver.set_handler(j, process_band, this, c);
                        c->sFFTScXOver.set_handler(j, process_sc_band, this, c);
                    }
                    c->sFFTXOver.set_phase(float(i) / float(nChannels));
                    c->sFFTScXOver.set_phase(float(i + 0.5f) / float(nChannels));
                }

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

            // Force to rebuild plan and envelope boost
            nPlanSize       = 0;
            bEnvUpdate      = true;
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

            // Determine work mode: classic, modern or linear phase
            xover_mode_t xover          = xover_mode_t(pMode->value());
            size_t fft_rank             = select_fft_rank(nRealSampleRate);
            if ((xover != nMode) || ((xover == XOVER_LINEAR_PHASE) && (fft_rank != vChannels[0].sFFTXOver.rank())))
            {
                nMode               = xover;
                rebuild_bands       = true;
                for (size_t i=0; i<nChannels; ++i)
                {
                    channel_t *c        = &vChannels[i];
                    c->sDryDelay.clear();
                    c->sFFTXOver.clear();
                    c->sFFTScXOver.clear();
                }
            }

            // Store gain
            bExtSc              = (pExtSc != NULL) ? (pExtSc->value() >= 0.5f) : false;
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
                // Put enabled bands to plan
                nPlanSize               = 0;
                vPlan[nPlanSize++]      = 0;

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

                // Update plan for channels and basic band parameters (enabled, start and end frequency)
                for (size_t i=0; i<nChannels; ++i)
                {
                    channel_t *c    = &vChannels[i];

                    for (size_t j=0; j<meta::mb_limiter::BANDS_MAX; ++j)
                        c->vBands[j].bEnabled   = (j > 0) ? vSplits[j-1].bEnabled : true;

                    for (size_t j=0; j<nPlanSize; ++j)
                    {
                        size_t band_id          = vPlan[j];
                        band_t *b               = &c->vBands[band_id];
                        c->vPlan[j]             = b;
                        b->fFreqStart           = (band_id > 0) ? vSplits[band_id-1].fFreq : 0.0f;
                    }
                    for (size_t j=0; j<nPlanSize; ++j)
                    {
                        band_t *b               = c->vPlan[j];
                        b->fFreqEnd             = (j < (nPlanSize - 1)) ? c->vPlan[j+1]->fFreqStart : fSampleRate >> 1;
                    }
                }
            }

            // Configure channels (first pass)
            for (size_t i=0; i<nChannels; ++i)
            {
                channel_t *c    = &vChannels[i];

                // Update bypass settings
                c->sBypass.set_bypass(pBypass->value());
                c->sFFTXOver.set_rank(fft_rank);
                c->sFFTScXOver.set_rank(fft_rank);
                c->sFFTXOver.set_sample_rate(nRealSampleRate);
                c->sFFTScXOver.set_sample_rate(nRealSampleRate);

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
                    fp.fQuality     = 0.0f;

                    switch (env_boost)
                    {
                        case meta::mb_limiter::FB_BT_3DB:
                            fp.nType        = dspu::FLT_BT_RLC_ENVELOPE;
                            fp.fGain        = GAIN_AMP_M_18_DB;
                            fp.nSlope       = 1;
                            break;
                        case meta::mb_limiter::FB_MT_3DB:
                            fp.nType        = dspu::FLT_MT_RLC_ENVELOPE;
                            fp.fGain        = GAIN_AMP_M_18_DB;
                            fp.nSlope       = 1;
                            break;
                        case meta::mb_limiter::FB_BT_6DB:
                            fp.nType        = dspu::FLT_BT_RLC_ENVELOPE;
                            fp.fGain        = GAIN_AMP_M_36_DB;
                            fp.nSlope       = 2;
                            break;
                        case meta::mb_limiter::FB_MT_6DB:
                            fp.nType        = dspu::FLT_MT_RLC_ENVELOPE;
                            fp.fGain        = GAIN_AMP_M_36_DB;
                            fp.nSlope       = 2;
                            break;
                        case meta::mb_limiter::FB_OFF:
                        default:
                            fp.nType        = dspu::FLT_NONE;
                            fp.fGain        = GAIN_AMP_0_DB;
                            fp.nSlope       = 1;
                            break;
                    }

                    c->sScBoost.update(nRealSampleRate, &fp);
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

            bool has_solo  = false;

            // Configure channels (second pass)
            for (size_t i=0; i<nChannels; ++i)
            {
                channel_t *c    = &vChannels[i];

                // Update lookahead delay settings for multiband and single band limiter
                c->sDataDelayMB.set_delay(nLookahead);
                c->sDataDelaySB.set_delay(nLookahead);

                // Configure dither noise
                c->sDither.set_bits(dither_bits);

                // Update settings for the post-limiter
                limiter_t *l    = &c->sLimiter;

                dspu::limiter_mode_t limiter_mode = decode_limiter_mode(l->pMode->value());
                float thresh    = l->pThresh->value();
                bool boost      = l->pBoost->value() >= 0.5f;

                l->bEnabled     = l->pEnable->value() >= 0.5f;
                l->fStereoLink  = (l->pStereoLink != NULL) ? l->pStereoLink->value() * 0.01f : 0.0f;
                if ((boost) && (i == 0) && (l->bEnabled))
                    fOutGain       /= thresh;

                l->sLimit.set_mode(limiter_mode);
                l->sLimit.set_sample_rate(nRealSampleRate);
                l->sLimit.set_lookahead(lookahead);
                l->sLimit.set_threshold(thresh, !boost);
                l->sLimit.set_attack(l->pAttack->value());
                l->sLimit.set_release(l->pRelease->value());
                l->sLimit.set_knee(l->pAlrKnee->value());
                l->sLimit.set_alr(l->pAlrOn->value() >= 0.5f);
                l->sLimit.set_alr_attack(l->pAlrAttack->value());
                l->sLimit.set_alr_release(l->pAlrRelease->value());

                // Update compressor bands
                for (size_t j=0; j<meta::mb_limiter::BANDS_MAX; ++j)
                {
                    band_t *b       = &c->vBands[j];
                    limiter_t *l    = &b->sLimiter;

                    limiter_mode    = decode_limiter_mode(l->pMode->value());
                    bool enabled    = l->pEnable->value() >= 0.5f;
                    thresh          = l->pThresh->value();
                    boost           = l->pBoost->value() >= 0.5f;
                    if (enabled && (j > 0))
                        enabled         = vSplits[j-1].bEnabled;

                    b->fPreamp      = b->pPreamp->value();
                    b->bMute        = b->pMute->value() >= 0.5f;
                    b->bSolo        = (b->bEnabled) ? (b->pSolo->value() >= 0.5f) : false;
                    b->fMakeup      = b->pMakeup->value();

                    l->bEnabled     = enabled;
                    l->fStereoLink  = (l->pStereoLink != NULL) ? l->pStereoLink->value() * 0.01f : 0.0f;

                    if (boost)
                        b->fMakeup     /= thresh;

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

                    // Update settings for limiter
                    l->sLimit.set_mode(limiter_mode);
                    l->sLimit.set_sample_rate(nRealSampleRate);
                    l->sLimit.set_lookahead(lookahead);
                    l->sLimit.set_threshold(thresh, !boost);
                    l->sLimit.set_attack(l->pAttack->value());
                    l->sLimit.set_release(l->pRelease->value());
                    l->sLimit.set_knee(l->pAlrKnee->value());
                    l->sLimit.set_alr(l->pAlrOn->value() >= 0.5f);
                    l->sLimit.set_alr_attack(l->pAlrAttack->value());
                    l->sLimit.set_alr_release(l->pAlrRelease->value());
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
                }

                c->sScBoost.set_sample_rate(real_srate);

                // Rebuild compression plan
                if (rebuild_bands)
                {
                    // Configure equalizers
                    lsp_trace("Configure bands according to new plan");

                    // Process only enabled bands
                    for (size_t j=0; j < nPlanSize; ++j)
                    {
                        band_t *b       = c->vPlan[j];
                        size_t band     = b - c->vBands;

                        b->sEq.set_sample_rate(nRealSampleRate);

                        // Check that band is enabled
                        b->bSync        = true;
//                        lsp_trace("[%d]: %f - %f", int(j), b->fFreqStart, b->fFreqEnd);
                        b->pFreqEnd->set_value(b->fFreqEnd);

                        if (nMode == XOVER_CLASSIC)
                        {
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
                        else // nMode == XOVER_LINEAR_PHASE
                        {
                            if (j > 0)
                            {
                                c->sFFTXOver.enable_hpf(band, true);
                                c->sFFTXOver.set_hpf_frequency(band, b->fFreqStart);
                                c->sFFTXOver.set_hpf_slope(band, -96.0f);

                                c->sFFTScXOver.enable_hpf(band, true);
                                c->sFFTScXOver.set_hpf_frequency(band, b->fFreqStart);
                                c->sFFTScXOver.set_hpf_slope(band, -96.0f);
                            }
                            else
                            {
                                c->sFFTXOver.disable_hpf(band);
                                c->sFFTScXOver.disable_hpf(band);
                            }

                            if (j < (nPlanSize-1))
                            {
                                c->sFFTXOver.enable_lpf(band, true);
                                c->sFFTXOver.set_lpf_frequency(band, b->fFreqEnd);
                                c->sFFTXOver.set_lpf_slope(band, -96.0f);
                                c->sFFTScXOver.enable_lpf(band, true);
                                c->sFFTScXOver.set_lpf_frequency(band, b->fFreqEnd);
                                c->sFFTScXOver.set_lpf_slope(band, -96.0f);
                            }
                            else
                            {
                                c->sFFTXOver.disable_lpf(band);
                                c->sFFTScXOver.disable_lpf(band);
                            }

                            // Update transfer function
                            c->sFFTScXOver.freq_chart(band, vTr, vFreqs, meta::mb_limiter::FFT_MESH_POINTS);
                            dsp::copy(b->vTrOut, vTr, meta::mb_limiter::FFT_MESH_POINTS);
                        }
                    } // nPlanSize

                    // Enable/disable bands
                    for (size_t j=0; j < meta::mb_limiter::BANDS_MAX; ++j)
                    {
                        bool band_on = (j > 0) ? vSplits[j-1].bEnabled : true;
                        c->sFFTXOver.enable_band(j, band_on);
                        c->sFFTScXOver.enable_band(j, band_on);
                    }
                }
            }

            nEnvBoost               = env_boost;
            bEnvUpdate              = false;

            // Report latency
            size_t t_over           = vChannels[0].sOver.get_oversampling();
            size_t latency          = (nLookahead * 2) / t_over + vChannels[0].sOver.latency();
            size_t xover_latency    = (nMode == XOVER_LINEAR_PHASE) ? vChannels[0].sFFTXOver.latency()/t_over : 0;
            set_latency(latency + xover_latency);

            for (size_t i=0; i<nChannels; ++i)
            {
                channel_t *c            = &vChannels[i];
                c->sDryDelay.set_delay(latency + xover_latency);
            }
        }

        void mb_limiter::process_band(void *object, void *subject, size_t band, const float *data, size_t sample, size_t count)
        {
            channel_t *c            = static_cast<channel_t *>(subject);
            band_t *b               = &c->vBands[band];

            // Store data to band's buffer
            dsp::copy(&b->vDataBuf[sample], data, count);
        }

        void mb_limiter::process_sc_band(void *object, void *subject, size_t band, const float *data, size_t sample, size_t count)
        {
            channel_t *c            = static_cast<channel_t *>(subject);
            band_t *b               = &c->vBands[band];

            // Store data to band's buffer
            dsp::mul_k3(&b->sLimiter.vVcaBuf[sample], data, b->fPreamp, count);
        }

        void mb_limiter::compute_multiband_vca_gain(channel_t *c, size_t samples)
        {
            // Split single sidechain band into multiple
            if (nMode == XOVER_CLASSIC)
            {
                for (size_t j=0; j<nPlanSize; ++j)
                {
                    band_t *b       = c->vPlan[j];
                    b->sEq.process(b->sLimiter.vVcaBuf, c->vScBuf, samples);
                    dsp::mul_k2(b->sLimiter.vVcaBuf, b->fPreamp, samples);
                }
            }
            else // nMode == XOVER_LINEAR_PHASE
                c->sFFTScXOver.process(c->vScBuf, samples);

            // Estimate the VCA gain for each band
            for (size_t j=0; j<nPlanSize; ++j)
            {
                band_t *b       = c->vPlan[j];

                // Pass sidechain signal through the limiter
                b->sLimiter.fInLevel    = lsp_max(b->sLimiter.fInLevel, dsp::abs_max(b->sLimiter.vVcaBuf, samples));
                if (b->sLimiter.bEnabled)
                    b->sLimiter.sLimit.process(b->sLimiter.vVcaBuf, b->sLimiter.vVcaBuf, samples);
                else
                    dsp::fill(b->sLimiter.vVcaBuf, (b->bMute) ? GAIN_AMP_M_INF_DB : GAIN_AMP_0_DB, samples);
            }
        }

        void mb_limiter::process_multiband_stereo_link(size_t samples)
        {
            for (size_t i=0; i<nPlanSize; ++i)
            {
                band_t *left = vChannels[0].vPlan[i];
                band_t *right= vChannels[1].vPlan[i];
                perform_stereo_link(
                    left->sLimiter.vVcaBuf,
                    right->sLimiter.vVcaBuf,
                    left->sLimiter.fStereoLink,
                    samples);
            }
        }

        void mb_limiter::apply_multiband_vca_gain(channel_t *c, size_t samples)
        {
            // Post-process VCA gain
            for (size_t i=0; i<nPlanSize; ++i)
            {
                band_t *b       = c->vPlan[i];

                // Compute gain reduction level
                float reduction     = dsp::min(b->sLimiter.vVcaBuf, samples);
                b->sLimiter.fReductionLevel  = lsp_min(b->sLimiter.fReductionLevel, reduction);

                // Check muting option
                if (b->bMute)
                    dsp::fill_zero(b->sLimiter.vVcaBuf, samples);
                else
                    dsp::mul_k2(b->sLimiter.vVcaBuf, b->fMakeup, samples);
            }

            // Here, we apply VCA to input signal dependent on the input
            // Apply delay to compensate lookahead feature
            c->sDataDelayMB.process(vTmpBuf, c->vInBuf, samples);

            // Originally, there is no signal
            dsp::fill_zero(c->vDataBuf, samples);           // Clear the channel data buffer

            if (nMode == XOVER_CLASSIC)
            {
                for (size_t j=0; j<nPlanSize; ++j)
                {
                    band_t *b       = c->vPlan[j];

                    // Do the crossover stuff:
                    // Process the signal with all-pass
                    b->sAllFilter.process(c->vDataBuf, c->vDataBuf, samples);
                    // Filter frequencies from input
                    b->sPassFilter.process(vEnvBuf, vTmpBuf, samples);
                    // Apply VCA gain to band and add to output data buffer
                    dsp::fmadd3(c->vDataBuf, vEnvBuf, b->sLimiter.vVcaBuf, samples);
                    // Filter frequencies from input
                    b->sRejFilter.process(vTmpBuf, vTmpBuf, samples);
                }
            }
            else // nMode == XOVER_LINEAR_PHASE
            {
                c->sFFTXOver.process(vTmpBuf, samples);
                for (size_t j=0; j<nPlanSize; ++j)
                {
                    band_t *b       = c->vPlan[j];
                    // Apply VCA gain to band and add to output data buffer
                    dsp::fmadd3(c->vDataBuf, b->vDataBuf, b->sLimiter.vVcaBuf, samples);
                }
            }
        }

        void mb_limiter::perform_stereo_link(float *cl, float *cr, float link, size_t samples)
        {
            for (size_t i=0; i<samples; ++i)
            {
                float gl = cl[i];
                float gr = cr[i];

                if (gl < gr)
                    cr[i] = gr + (gl - gr) * link;
                else
                    cl[i] = gl + (gr - gl) * link;
            }
        }

        void mb_limiter::process_single_band(size_t samples)
        {
            // Process the VCA signal for each channel
            for (size_t i=0; i<nChannels; ++i)
            {
                channel_t *c = &vChannels[i];

                c->sLimiter.fInLevel    = lsp_max(c->sLimiter.fInLevel, dsp::abs_max(c->vDataBuf, samples));
                if (c->sLimiter.bEnabled)
                    c->sLimiter.sLimit.process(c->sLimiter.vVcaBuf, c->vDataBuf, samples);
                else
                    dsp::fill(c->sLimiter.vVcaBuf, GAIN_AMP_0_DB, samples);
            }

            // Do stereo linking
            if (nChannels > 1)
            {
                limiter_t *left     = &vChannels[0].sLimiter;
                limiter_t *right    = &vChannels[1].sLimiter;

                perform_stereo_link(
                    left->vVcaBuf,
                    right->vVcaBuf,
                    left->fStereoLink,
                    samples);
            }

            // Apply changes to the signal
            for (size_t i=0; i<nChannels; ++i)
            {
                channel_t *c = &vChannels[i];

                // Compute gain reduction level
                float reduction             = dsp::min(c->sLimiter.vVcaBuf, samples);
                c->sLimiter.fReductionLevel = lsp_min(c->sLimiter.fReductionLevel, reduction);

                // Apply lookahead and gain reduction to the input signal
                c->sDataDelaySB.process(c->vDataBuf, c->vDataBuf, samples);
                dsp::fmmul_k3(c->vDataBuf, c->sLimiter.vVcaBuf, fOutGain, samples);
            }
        }

        void mb_limiter::output_audio(size_t samples)
        {
            for (size_t i=0; i<nChannels; ++i)
            {
                channel_t *c        = &vChannels[i];

                c->sDryDelay.process(c->vInBuf, c->vIn, samples);
                c->sBypass.process(c->vOut, c->vInBuf, c->vData, samples);
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

                c->sLimiter.fInLevel        = GAIN_AMP_M_INF_DB;
                c->sLimiter.fReductionLevel = GAIN_AMP_P_96_DB;

                for (size_t i=0; i<meta::mb_limiter::BANDS_MAX; ++i)
                {
                    band_t *b           = &c->vBands[i];
                    b->sLimiter.fInLevel        = GAIN_AMP_M_INF_DB;
                    b->sLimiter.fReductionLevel = GAIN_AMP_P_96_DB;
                }
            }

            // Do main processing
            for (size_t offset=0; offset < samples;)
            {
                // Compute number of samples to process
                size_t count        = lsp_min(samples - offset, BUFFER_SIZE);
                size_t ovs_count    = samples * vChannels[0].sScOver.get_oversampling();

                // Perform multiband processing
                oversample_data(count);
                for (size_t i=0; i<nChannels; ++i)
                    compute_multiband_vca_gain(&vChannels[i], ovs_count);
                if (nChannels > 1)
                    process_multiband_stereo_link(ovs_count);
                for (size_t i=0; i<nChannels; ++i)
                    apply_multiband_vca_gain(&vChannels[i], ovs_count);

                // Perform single-band processing
                process_single_band(ovs_count);

                // Post-process data
                downsample_data(count);

                // Output audio
                output_audio(count);
                perform_analysis(count);

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

                // Apply sidechain boosting
                c->sScBoost.process(c->vScBuf, c->vScBuf, ovs_samples);
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

        void mb_limiter::perform_analysis(size_t samples)
        {
            // Prepare processing
            const float *bufs[4] = { NULL, NULL, NULL, NULL };
            for (size_t i=0; i<nChannels; ++i)
            {
                channel_t *c            = &vChannels[i];
                bufs[c->nAnInChannel]   = c->vInBuf;
                bufs[c->nAnOutChannel]  = c->vData;

                c->pOutMeter->set_value(dsp::abs_max(c->vData, samples));
                c->pInMeter->set_value(dsp::abs_max(c->vInBuf, samples) * fInGain);
            }

            // Perform processing
            sAnalyzer.process(bufs, samples);
        }

        void mb_limiter::output_meters()
        {
            for (size_t i=0; i<nChannels; ++i)
            {
                channel_t *c        = &vChannels[i];

                // Report gain reduction for master limiter
                float reduction     = (c->sLimiter.bEnabled) ? c->sLimiter.fReductionLevel : GAIN_AMP_0_DB;
                c->sLimiter.pReductionMeter->set_value(reduction);

                for (size_t j=0; j<meta::mb_limiter::BANDS_MAX; ++j)
                {
                    band_t *b           = &c->vBands[j];

                    reduction           = ((b->bEnabled) && (b->sLimiter.bEnabled)) ? b->sLimiter.fReductionLevel : GAIN_AMP_0_DB;
                    b->sLimiter.pReductionMeter->set_value(reduction);
                }
            }

            // Output input gain meters
            if (nChannels > 1)
            {
                limiter_t *left     = &vChannels[0].sLimiter;
                limiter_t *right    = &vChannels[1].sLimiter;
                float in_gain       = (left->bEnabled) ? lsp_max(left->fInLevel, right->fInLevel) : GAIN_AMP_M_INF_DB;
                left->pInMeter->set_value(in_gain);

                for (size_t j=0; j<meta::mb_limiter::BANDS_MAX; ++j)
                {
                    band_t *b           = &vChannels[0].vBands[j];
                    left                = &vChannels[0].vBands[j].sLimiter;
                    right               = &vChannels[1].vBands[j].sLimiter;
                    in_gain             = ((b->bEnabled) && (left->bEnabled)) ? lsp_max(left->fInLevel, right->fInLevel) : GAIN_AMP_M_INF_DB;
                    left->pInMeter->set_value(in_gain);
                }
            }
            else
            {
                limiter_t *mid      = &vChannels[0].sLimiter;
                float in_gain       = (mid->bEnabled) ? mid->fInLevel : GAIN_AMP_M_INF_DB;
                mid->pInMeter->set_value(in_gain);

                for (size_t j=0; j<meta::mb_limiter::BANDS_MAX; ++j)
                {
                    band_t *b           = &vChannels[0].vBands[j];
                    mid                 = &vChannels[0].vBands[j].sLimiter;
                    in_gain             = ((b->bEnabled) && (mid->bEnabled)) ? mid->fInLevel : GAIN_AMP_M_INF_DB;
                    mid->pInMeter->set_value(in_gain);
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
                for (size_t j=0; j<nPlanSize; ++j)
                {
                    band_t *b       = c->vPlan[j];
                    if (j == 0)
                        dsp::mul_k3(vTr, b->vTrOut, b->sLimiter.fReductionLevel * b->fMakeup, meta::mb_limiter::FFT_MESH_POINTS);
                    else
                        dsp::fmadd_k3(vTr, b->vTrOut, b->sLimiter.fReductionLevel * b->fMakeup, meta::mb_limiter::FFT_MESH_POINTS);
                }
                dsp::copy(c->vTrOut, vTr, meta::mb_limiter::FFT_MESH_POINTS);

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
                        dsp::mul_k2(&mesh->pvData[1][1], fInGain, meta::mb_limiter::FFT_MESH_POINTS);

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

        void mb_limiter::ui_activated()
        {
            // Force meshes with the UI to synchronized
            for (size_t i=0; i<nChannels; ++i)
            {
                channel_t *c        = &vChannels[i];

                for (size_t j=0; j<meta::mb_limiter::BANDS_MAX; ++j)
                {
                    band_t *b           = &c->vBands[j];
                    b->bSync            = true;
                }
            }
        }

        bool mb_limiter::inline_display(plug::ICanvas *cv, size_t width, size_t height)
        {
            // Check proportions
            if (height > (M_RGOLD_RATIO * width))
                height  = M_RGOLD_RATIO * width;

            // Init canvas
            if (!cv->init(width, height))
                return false;
            width   = cv->width();
            height  = cv->height();

            // Clear background
            bool bypassing = vChannels[0].sBypass.bypassing();
            cv->set_color_rgb((bypassing) ? CV_DISABLED : CV_BACKGROUND);
            cv->paint();

            // Draw axis
            cv->set_line_width(1.0);

            // "-72 db / (:zoom ** 3)" max="24 db * :zoom"

            float miny  = logf(GAIN_AMP_M_72_DB / dsp::ipowf(fZoom, 3));
            float maxy  = logf(GAIN_AMP_P_48_DB * fZoom * fZoom);

            float zx    = 1.0f/SPEC_FREQ_MIN;
            float zy    = dsp::ipowf(fZoom, 3)/GAIN_AMP_M_72_DB;
            float dx    = width/(logf(SPEC_FREQ_MAX)-logf(SPEC_FREQ_MIN));
            float dy    = height/(miny-maxy);

            // Draw vertical lines
            cv->set_color_rgb(CV_YELLOW, 0.5f);
            for (float i=100.0f; i<SPEC_FREQ_MAX; i *= 10.0f)
            {
                float ax = dx*(logf(i*zx));
                cv->line(ax, 0, ax, height);
            }

            // Draw horizontal lines
            cv->set_color_rgb(CV_WHITE, 0.5f);
            for (float i=GAIN_AMP_M_72_DB; i<GAIN_AMP_P_48_DB; i *= GAIN_AMP_P_12_DB)
            {
                float ay = height + dy*(logf(i*zy));
                cv->line(0, ay, width, ay);
            }

            // Allocate buffer: f, x, y, tr
            pIDisplay           = core::IDBuffer::reuse(pIDisplay, 4, width+2);
            core::IDBuffer *b   = pIDisplay;
            if (b == NULL)
                return false;

            static const uint32_t c_colors[] =
            {
                CV_MIDDLE_CHANNEL,
                CV_LEFT_CHANNEL, CV_RIGHT_CHANNEL
            };
            const uint32_t *colors = (nChannels > 1) ? &c_colors[1] : &c_colors[0];

            // Initialize mesh
            b->v[0][0]          = SPEC_FREQ_MIN*0.5f;
            b->v[0][width+1]    = SPEC_FREQ_MAX*2.0f;
            b->v[3][0]          = 1.0f;
            b->v[3][width+1]    = 1.0f;

            bool aa = cv->set_anti_aliasing(true);
            cv->set_line_width(2);

            for (size_t i=0; i<nChannels; ++i)
            {
                channel_t *c    = &vChannels[i];

                for (size_t j=0; j<width; ++j)
                {
                    size_t k        = (j*meta::mb_limiter::FFT_MESH_POINTS)/width;
                    b->v[0][j+1]    = vFreqs[k];
                    b->v[3][j+1]    = c->vTrOut[k];
                }

                dsp::fill(b->v[1], 0.0f, width+2);
                dsp::fill(b->v[2], height, width+2);
                dsp::axis_apply_log1(b->v[1], b->v[0], zx, dx, width+2);
                dsp::axis_apply_log1(b->v[2], b->v[3], zy, dy, width+2);

                // Draw mesh
                uint32_t color = (bypassing || !(active())) ? CV_SILVER : colors[i];
                Color stroke(color), fill(color, 0.5f);
                cv->draw_poly(b->v[1], b->v[2], width+2, stroke, fill);
            }
            cv->set_anti_aliasing(aa);

            return true;
        }

        void mb_limiter::dump(dspu::IStateDumper *v, const char *name, const limiter_t *l)
        {
            v->begin_object(name, l, sizeof(limiter_t));
            {
                v->write_object("sLimit", &l->sLimit);

                v->write("bEnabled", l->bEnabled);
                v->write("fStereoLink", l->fStereoLink);
                v->write("fInLevel", l->fInLevel);
                v->write("fReductionLevel", l->fReductionLevel);
                v->write("vVcaBuf", l->vVcaBuf);

                v->write("pEnable", l->pEnable);
                v->write("pAlrOn", l->pAlrOn);
                v->write("pAlrAttack", l->pAlrAttack);
                v->write("pAlrRelease", l->pAlrRelease);
                v->write("pAlrKnee", l->pAlrKnee);

                v->write("pMode", l->pMode);
                v->write("pThresh", l->pThresh);
                v->write("pBoost", l->pBoost);
                v->write("pAttack", l->pAttack);
                v->write("pRelease", l->pRelease);
                v->write("pInMeter", l->pInMeter);
                v->write("pStereoLink", l->pStereoLink);
                v->write("pReductionMeter", l->pReductionMeter);
            }
            v->end_object();
        }

        void mb_limiter::dump(dspu::IStateDumper *v) const
        {
            v->write_object("sAnalyzer", &sAnalyzer);
            v->write("nChannels", nChannels);
            v->write("nMode", nMode);
            v->write("bSidechain", bSidechain);
            v->write("bExtSc", bExtSc);
            v->write("bEnvUpdate", bEnvUpdate);
            v->write("fInGain", fInGain);
            v->write("fOutGain", fOutGain);
            v->write("fZoom", fZoom);
            v->write("nRealSampleRate", nRealSampleRate);
            v->write("nEnvBoost", nEnvBoost);
            v->write("nLookahead", nLookahead);

            v->begin_array("vChannels", vChannels, nChannels);
            {
                //channel_t              *vChannels;          // Channels
                for (size_t i=0; i<nChannels; ++i)
                {
                    const channel_t *c      = &vChannels[i];
                    v->begin_object(c, sizeof(channel_t));
                    {
                        v->write_object("sBypass", &c->sBypass);
                        v->write_object("sFFTXOver", &c->sFFTXOver);
                        v->write_object("sFFTScXOver", &c->sFFTScXOver);
                        v->write_object("sDither", &c->sDither);
                        v->write_object("sOver", &c->sOver);
                        v->write_object("sScOver", &c->sScOver);
                        v->write_object("sScBoost", &c->sScBoost);
                        v->write_object("sDataDelayMB", &c->sDataDelayMB);
                        v->write_object("sDataDelaySB", &c->sDataDelaySB);
                        v->write_object("sDryDelay", &c->sDryDelay);

                        v->begin_array("vBands", meta::mb_limiter::BANDS_MAX);
                        {
                            for (size_t j=0; j<meta::mb_limiter::BANDS_MAX; ++j)
                            {
                                const band_t *b     = &c->vBands[j];

                                v->write_object("sEq", &b->sEq);
                                v->write_object("sPassFilter", &b->sPassFilter);
                                v->write_object("sRejFilter", &b->sRejFilter);
                                v->write_object("sAllFilter", &b->sAllFilter);

                                dump(v, "sLimiter", &b->sLimiter);

                                v->write("bSync", b->bSync);
                                v->write("bMute", b->bMute);
                                v->write("bSolo", b->bSolo);
                                v->write("bEnabled", b->bEnabled);
                                v->write("fPreamp", b->fPreamp);
                                v->write("fFreqStart", b->fFreqStart);
                                v->write("fFreqEnd", b->fFreqEnd);
                                v->write("fMakeup", b->fMakeup);

                                v->write("vDataBuf", b->vDataBuf);
                                v->write("vTrOut", b->vTrOut);

                                v->write("pFreqEnd", b->pFreqEnd);
                                v->write("pSolo", b->pSolo);
                                v->write("pMute", b->pMute);
                                v->write("pPreamp", b->pPreamp);
                                v->write("pMakeup", b->pMakeup);
                                v->write("pBandGraph", b->pBandGraph);
                            }
                        }
                        v->end_array();

                        v->writev("vPlan", c->vPlan, meta::mb_limiter::BANDS_MAX);
                        dump(v, "sLimiter", &c->sLimiter);

                        v->write("vIn", c->vIn);
                        v->write("vSc", c->vSc);
                        v->write("vOut", c->vOut);
                        v->write("vData", c->vData);
                        v->write("vInBuf", c->vInBuf);
                        v->write("vScBuf", c->vScBuf);
                        v->write("vDataBuf", c->vDataBuf);
                        v->write("vTrOut", c->vTrOut);
                        v->write("bFftIn", c->bFftIn);
                        v->write("bFftOut", c->bFftOut);
                        v->write("nAnInChannel", c->nAnInChannel);
                        v->write("nAnOutChannel", c->nAnOutChannel);

                        v->write("pIn", c->pIn);
                        v->write("pOut", c->pOut);
                        v->write("pSc", c->pSc);
                        v->write("pFftInEnable", c->pFftInEnable);
                        v->write("pFftOutEnable", c->pFftOutEnable);
                        v->write("pInMeter", c->pInMeter);
                        v->write("pOutMeter", c->pOutMeter);
                        v->write("pFftIn", c->pFftIn);
                        v->write("pFftOut", c->pFftOut);
                        v->write("pFilterGraph", c->pFilterGraph);
                    }
                    v->end_object();
                }
            }
            v->end_array();

            v->write("vTmpBuf", vTmpBuf);
            v->write("vEnvBuf", vEnvBuf);
            v->write("vIndexes", vIndexes);
            v->write("vFreqs", vFreqs);
            v->write("vTr", vTr);
            v->write("vTrTmp", vTrTmp);
            v->write("vFc", vFc);
            v->write("pIDisplay", pIDisplay);

            v->begin_array("vSplits", vSplits, meta::mb_limiter::BANDS_MAX-1);
            {
                for (size_t i=0; i<(meta::mb_limiter::BANDS_MAX-1); ++i)
                {
                    const split_t *s = &vSplits[i];
                    v->begin_object(s, sizeof(split_t));
                    {
                        v->write("bEnabled", s->bEnabled);
                        v->write("fFreq", s->fFreq);
                        v->write("pEnabled", s->pEnabled);
                        v->write("pFreq", s->pFreq);
                    }
                    v->end_object();
                }
            }
            v->end_array();

            v->writev("vPlan", vPlan, meta::mb_limiter::BANDS_MAX);
            v->write("nPlanSize", nPlanSize);

            v->write("pBypass", pBypass);
            v->write("pInGain", pInGain);
            v->write("pOutGain", pOutGain);
            v->write("pMode", pMode);
            v->write("pLookahead", pLookahead);
            v->write("pOversampling", pOversampling);
            v->write("pDithering", pDithering);
            v->write("pEnvBoost", pEnvBoost);
            v->write("pZoom", pZoom);
            v->write("pReactivity", pReactivity);
            v->write("pShift", pShift);
            v->write("pExtSc", pExtSc);

            v->write("pData", pData);
        }

    } /* namespace plugins */
} /* namespace lsp */


