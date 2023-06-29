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

#ifndef PRIVATE_PLUGINS_MB_LIMITER_H_
#define PRIVATE_PLUGINS_MB_LIMITER_H_

#include <lsp-plug.in/dsp-units/ctl/Blink.h>
#include <lsp-plug.in/dsp-units/ctl/Bypass.h>
#include <lsp-plug.in/dsp-units/dynamics/Limiter.h>
#include <lsp-plug.in/dsp-units/filters/DynamicFilters.h>
#include <lsp-plug.in/dsp-units/filters/Equalizer.h>
#include <lsp-plug.in/dsp-units/util/Analyzer.h>
#include <lsp-plug.in/dsp-units/util/Delay.h>
#include <lsp-plug.in/dsp-units/util/Dither.h>
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
            private:
                mb_limiter & operator = (const mb_limiter &);
                mb_limiter (const mb_limiter &);

            protected:
                typedef struct limiter_t
                {
                    dspu::Limiter           sLimit;             // Limiter
                    dspu::Blink             sBlink;             // Blink meter

                    bool                    bEnabled;           // Enabled flag
                    float                   fStereoLink;        // Stereo linking

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
                    plug::IPort            *pStereoLink;        // Stereo linking
                    plug::IPort            *pReductionMeter;    // Reduction gain meter
                } limiter_t;

                typedef struct band_t: public limiter_t
                {
                    dspu::Equalizer         sEq;                // Sidechain equalizer
                    dspu::Filter            sPassFilter;        // Passing filter for 'classic' mode
                    dspu::Filter            sRejFilter;         // Rejection filter for 'classic' mode
                    dspu::Filter            sAllFilter;         // All-pass filter for phase compensation

                    bool                    bSync;              // Synchronization request
                    bool                    bMute;              // Mute channel
                    bool                    bSolo;              // Solo channel
                    float                   fPreamp;            // Sidechain pre-amplification
                    float                   fFreqStart;         // Start frequency of the band
                    float                   fFreqEnd;           // End frequency of the band
                    float                   fMakeup;            // Makeup gain
                    float                   fGainLevel;         // Gain level

                    float                  *vTrOut;             // Transfer function output
                    float                  *vVCA;               // Voltage-controlled amplification value for each band

                    size_t                  nFilterID;          // Identifier of the filter

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
                    dspu::Dither            sDither;            // Dither
                    dspu::Oversampler       sOver;              // Oversampler object for signal
                    dspu::Oversampler       sScOver;            // Sidechain oversampler object for signal
                    dspu::Filter            sScBoost;           // Sidechain booster
                    dspu::Delay             sDryDelay;          // Dry delay

                    band_t                  vBands[meta::mb_limiter::BANDS_MAX];    // Band processors
                    band_t                 *vPlan[meta::mb_limiter::BANDS_MAX];     // Actual plan
                    limiter_t               sLimiter;           // Output limiter

                    const float            *vIn;                // Input data
                    const float            *vSc;                // Sidechain data
                    float                  *vOut;               // Output data
                    float                  *vTrOut;             // Transfer function output
                    bool                    bFftIn;             // Output input FFT analysis
                    bool                    bFftOut;            // Output output FFT analysis
                    size_t                  nAnInChannel;       // Analyzer channel used for input signal analysis
                    size_t                  nAnOutChannel;      // Analyzer channel used for output signal analysis

                    plug::IPort            *pIn;                // Input port
                    plug::IPort            *pOut;               // Output port
                    plug::IPort            *pSc;                // Sidechain port
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
                dspu::DynamicFilters    sFilters;           // Dynamic filters for each band in 'modern' mode
                size_t                  nChannels;          // Number of channels
                bool                    bSidechain;         // Sidechain switch is present
                bool                    bExtSc;             // External sidechain turned on
                bool                    bModern;            // Modern mode
                bool                    bEnvUpdate;         // Request for envelope update
                float                   fInGain;            // Input gain
                float                   fOutGain;           // Output gain
                float                   fZoom;              // Zoom
                size_t                  nOversampling;      // Oversampling
                size_t                  nEnvBoost;          // Envelope boosting
                size_t                  nLookahead;         // Lookahead buffer size

                channel_t              *vChannels;          // Channels
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
                plug::IPort            *pLookahead;         // Lookahead time
                plug::IPort            *pMode;              // Mode
                plug::IPort            *pOversampling;      // Oversampling
                plug::IPort            *pDithering;         // Dithering
                plug::IPort            *pEnvBoost;          // Envelope boost
                plug::IPort            *pZoom;              // Zoom
                plug::IPort            *pReactivity;        // Reactivity
                plug::IPort            *pShift;             // Shift gain
                plug::IPort            *pExtSc;             // External sidechain

                uint8_t                *pData;

            protected:
                void                    output_fft_curves();
                dspu::limiter_mode_t    decode_limiter_mode(ssize_t mode);

            public:
                explicit mb_limiter(const meta::plugin_t *meta);
                virtual ~mb_limiter() override;

                virtual void            init(plug::IWrapper *wrapper, plug::IPort **ports) override;
                virtual void            destroy() override;

            public:
                virtual void            update_sample_rate(long sr) override;
                virtual void            update_settings() override;
                virtual void            process(size_t samples) override;
                virtual bool            inline_display(plug::ICanvas *cv, size_t width, size_t height) override;
                virtual void            dump(dspu::IStateDumper *v) const override;
        };

    } /* namespace plugins */
} /* namespace lsp */


#endif /* PRIVATE_PLUGINS_MB_LIMITER_H_ */

