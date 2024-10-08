/*
 * Copyright (C) 2024 Linux Studio Plugins Project <https://lsp-plug.in/>
 *           (C) 2024 Vladimir Sadovnikov <sadko4u@gmail.com>
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

#ifndef PRIVATE_PLUGINS_MB_LIMITER_H_
#define PRIVATE_PLUGINS_MB_LIMITER_H_

#include <lsp-plug.in/dsp-units/ctl/Bypass.h>
#include <lsp-plug.in/dsp-units/ctl/Counter.h>
#include <lsp-plug.in/dsp-units/dynamics/Limiter.h>
#include <lsp-plug.in/dsp-units/filters/DynamicFilters.h>
#include <lsp-plug.in/dsp-units/filters/Equalizer.h>
#include <lsp-plug.in/dsp-units/util/Analyzer.h>
#include <lsp-plug.in/dsp-units/util/Delay.h>
#include <lsp-plug.in/dsp-units/util/Dither.h>
#include <lsp-plug.in/dsp-units/util/FFTCrossover.h>
#include <lsp-plug.in/dsp-units/util/Oversampler.h>
#include <lsp-plug.in/plug-fw/core/IDBuffer.h>
#include <lsp-plug.in/plug-fw/plug.h>

#include <private/meta/mb_limiter.h>

namespace lsp
{
    namespace plugins
    {
        /**
         * Base class for the latency compensation delay
         */
        class mb_limiter: public plug::Module
        {
            protected:
                enum sc_mode_t
                {
                    SCM_INTERNAL,
                    SCM_EXTERNAL,
                    SCM_LINK,
                };

                enum xover_mode_t
                {
                    XOVER_CLASSIC,
                    XOVER_LINEAR_PHASE
                };

                typedef struct limiter_t
                {
                    dspu::Limiter           sLimit;             // Limiter

                    bool                    bEnabled;           // Enabled flag
                    float                   fStereoLink;        // Stereo linking
                    float                   fInLevel;           // Input level
                    float                   fReductionLevel;    // Gain reduction level
                    float                  *vVcaBuf;            // Voltage-controlled amplification value for each band

                    plug::IPort            *pEnable;            // Enable
                    plug::IPort            *pAlrOn;             // Automatic level regulation
                    plug::IPort            *pAlrAttack;         // Automatic level regulation attack
                    plug::IPort            *pAlrRelease;        // Automatic level regulation release
                    plug::IPort            *pAlrKnee;           // Limiter knee

                    plug::IPort            *pMode;              // Operating mode
                    plug::IPort            *pThresh;            // Limiter threshold
                    plug::IPort            *pBoost;             // Gain boost
                    plug::IPort            *pAttack;            // Attack time
                    plug::IPort            *pRelease;           // Release time
                    plug::IPort            *pInMeter;           // Input gain meter
                    plug::IPort            *pStereoLink;        // Stereo linking
                    plug::IPort            *pReductionMeter;    // Reduction gain meter
                } limiter_t;

                typedef struct band_t
                {
                    dspu::Equalizer         sEq;                // Sidechain equalizer
                    dspu::Filter            sPassFilter;        // Passing filter for 'classic' mode
                    dspu::Filter            sRejFilter;         // Rejection filter for 'classic' mode
                    dspu::Filter            sAllFilter;         // All-pass filter for phase compensation

                    limiter_t               sLimiter;           // Limiter

                    bool                    bSync;              // Synchronization request
                    bool                    bMute;              // Mute channel
                    bool                    bSolo;              // Solo channel
                    bool                    bEnabled;           // Band is enabled
                    float                   fPreamp;            // Sidechain pre-amplification
                    float                   fFreqStart;         // Start frequency of the band
                    float                   fFreqEnd;           // End frequency of the band
                    float                   fMakeup;            // Makeup gain

                    float                  *vDataBuf;           // Data buffer
                    float                  *vTrOut;             // Transfer function output

                    plug::IPort            *pFreqEnd;           // Frequency range end
                    plug::IPort            *pSolo;              // Solo switch
                    plug::IPort            *pMute;              // Mute switch
                    plug::IPort            *pPreamp;            // Sidechain preamp
                    plug::IPort            *pMakeup;            // Band makeup
                    plug::IPort            *pBandGraph;         // Frequency band filter graph
                } band_t;

                typedef struct split_t
                {
                    bool                    bEnabled;           // Split band is enabled
                    float                   fFreq;              // Split band frequency

                    plug::IPort            *pEnabled;           // Enable port
                    plug::IPort            *pFreq;              // Split frequency
                } split_t;

                typedef struct channel_t
                {
                    dspu::Bypass            sBypass;            // Bypass
                    dspu::FFTCrossover      sFFTXOver;          // FFT crossover
                    dspu::FFTCrossover      sFFTScXOver;        // FFT crossover for sidechain
                    dspu::Dither            sDither;            // Dither
                    dspu::Oversampler       sOver;              // Oversampler object for signal
                    dspu::Oversampler       sScOver;            // Sidechain oversampler object for signal
                    dspu::Filter            sScBoost;           // Sidechain booster
                    dspu::Delay             sDataDelayMB;       // Data delay for multi-band processing
                    dspu::Delay             sDataDelaySB;       // Data delay for single-band processing
                    dspu::Delay             sDryDelay;          // Dry delay

                    band_t                  vBands[meta::mb_limiter::BANDS_MAX];    // Band processors
                    band_t                 *vPlan[meta::mb_limiter::BANDS_MAX];     // Actual plan
                    limiter_t               sLimiter;           // Output limiter

                    const float            *vIn;                // Input data
                    const float            *vSc;                // Sidechain data
                    const float            *vShmIn;             // Shared memory input
                    float                  *vOut;               // Output data
                    float                  *vData;              // Intermediate buffer with processed data
                    float                  *vInBuf;             // Oversampled input data buffer
                    float                  *vScBuf;             // Oversampled sidechain data buffer
                    float                  *vDataBuf;           // Oversampled buffer for processed data
                    float                  *vTrOut;             // Transfer function output
                    bool                    bFftIn;             // Output input FFT analysis
                    bool                    bFftOut;            // Output output FFT analysis
                    size_t                  nAnInChannel;       // Analyzer channel used for input signal analysis
                    size_t                  nAnOutChannel;      // Analyzer channel used for output signal analysis

                    plug::IPort            *pIn;                // Input port
                    plug::IPort            *pOut;               // Output port
                    plug::IPort            *pSc;                // Sidechain port
                    plug::IPort            *pShmIn;             // Shared memory input port
                    plug::IPort            *pFftInEnable;       // Input FFT enable
                    plug::IPort            *pFftOutEnable;      // Output FFT enable
                    plug::IPort            *pInMeter;           // Input gain meter
                    plug::IPort            *pOutMeter;          // Output gain meter
                    plug::IPort            *pFftIn;             // Input FFT graph
                    plug::IPort            *pFftOut;            // Output FFT graph
                    plug::IPort            *pFilterGraph;       // Output filter graph
                } channel_t;

            protected:
                dspu::Analyzer          sAnalyzer;          // Analyzer
                dspu::Counter           sCounter;           // Sync counter
                uint32_t                nChannels;          // Number of channels
                xover_mode_t            nMode;              // Operating mode
                bool                    bSidechain;         // Sidechain switch is present
                bool                    bEnvUpdate;         // Request for envelope update
                uint32_t                nScMode;            // Sidechain mode
                float                   fInGain;            // Input gain
                float                   fOutGain;           // Output gain
                float                   fZoom;              // Zoom
                uint32_t                nRealSampleRate;    // Real sample rate
                uint32_t                nEnvBoost;          // Envelope boosting
                uint32_t                nLookahead;         // Lookahead buffer size

                channel_t              *vChannels;          // Channels
                float                  *vEmptyBuf;          // Empty buffer filled with zeros
                float                  *vTmpBuf;            // Temporary buffer
                float                  *vEnvBuf;            // Temporary envelope buffer
                uint32_t               *vIndexes;           // Analyzer FFT indexes
                float                  *vFreqs;             // Analyzer FFT frequencies
                float                  *vTr;                // Buffer for computing transfer function
                float                  *vTrTmp;             // Temporary buffer for computing transfer function
                float                  *vFc;                // Filter characteristics
                core::IDBuffer         *pIDisplay;          // Inline display buffer

                split_t                 vSplits[meta::mb_limiter::BANDS_MAX-1];     // Frequency splits
                uint8_t                 vPlan[meta::mb_limiter::BANDS_MAX];         // Execution plan (band indices)
                size_t                  nPlanSize;          // Plan size

                plug::IPort            *pBypass;            // Bypass port
                plug::IPort            *pInGain;            // Input gain
                plug::IPort            *pOutGain;           // Output gain
                plug::IPort            *pMode;              // Operating mode
                plug::IPort            *pLookahead;         // Lookahead time
                plug::IPort            *pOversampling;      // Oversampling
                plug::IPort            *pDithering;         // Dithering
                plug::IPort            *pEnvBoost;          // Envelope boost
                plug::IPort            *pZoom;              // Zoom
                plug::IPort            *pReactivity;        // Reactivity
                plug::IPort            *pShift;             // Shift gain
                plug::IPort            *pScMode;            // Sidechain mode

                uint8_t                *pData;

            protected:
                void                    output_meters();
                void                    output_fft_curves();
                void                    perform_analysis(size_t samples);
                void                    oversample_data(size_t samples, size_t ovs_samples);
                void                    compute_multiband_vca_gain(channel_t *c, size_t samples);
                void                    process_multiband_stereo_link(size_t samples);
                void                    apply_multiband_vca_gain(channel_t *c, size_t samples);
                void                    process_single_band(size_t samples);
                void                    downsample_data(size_t samples);
                void                    perform_stereo_link(float *cl, float *cr, float link, size_t samples);
                void                    output_audio(size_t samples);

                size_t                  decode_real_sample_rate(size_t mode);
                uint32_t                decode_sidechain_mode(uint32_t sc) const;

                void                    do_destroy();

            protected:
                static dspu::limiter_mode_t     decode_limiter_mode(ssize_t mode);
                static dspu::over_mode_t        decode_oversampling_mode(size_t mode);
                static bool                     decode_filtering(size_t mode);
                static size_t                   decode_dithering(size_t mode);
                static size_t                   select_fft_rank(size_t sample_rate);
                static void                     process_band(void *object, void *subject, size_t band, const float *data, size_t sample, size_t count);
                static void                     process_sc_band(void *object, void *subject, size_t band, const float *data, size_t sample, size_t count);

                static void                     dump(dspu::IStateDumper *v, const char *name, const limiter_t *l);

            public:
                explicit mb_limiter(const meta::plugin_t *meta);
                mb_limiter(const mb_limiter &) = delete;
                mb_limiter(mb_limiter &&) = delete;
                virtual ~mb_limiter() override;

                mb_limiter & operator = (const mb_limiter &) = delete;
                mb_limiter & operator = (mb_limiter &&) = delete;

                virtual void            init(plug::IWrapper *wrapper, plug::IPort **ports) override;
                virtual void            destroy() override;

            public:
                virtual void            update_sample_rate(long sr) override;
                virtual void            update_settings() override;
                virtual void            process(size_t samples) override;
                virtual void            ui_activated() override;
                virtual bool            inline_display(plug::ICanvas *cv, size_t width, size_t height) override;
                virtual void            dump(dspu::IStateDumper *v) const override;
        };

    } /* namespace plugins */
} /* namespace lsp */


#endif /* PRIVATE_PLUGINS_MB_LIMITER_H_ */

