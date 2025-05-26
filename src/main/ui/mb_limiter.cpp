/*
 * Copyright (C) 2025 Linux Studio Plugins Project <https://lsp-plug.in/>
 *           (C) 2025 Vladimir Sadovnikov <sadko4u@gmail.com>
 *
 * This file is part of lsp-plugins-mb-limiter
 * Created on: 25 нояб. 2020 г.
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

#include <lsp-plug.in/plug-fw/ui.h>
#include <lsp-plug.in/dsp-units/units.h>

#include <private/plugins/mb_limiter.h>
#include <private/ui/mb_limiter.h>
#include <lsp-plug.in/stdlib/string.h>
#include <lsp-plug.in/stdlib/stdio.h>
#include <lsp-plug.in/stdlib/locale.h>

namespace lsp
{
    namespace plugui
    {
        //---------------------------------------------------------------------
        // Plugin UI factory
        static const meta::plugin_t *plugin_uis[] =
        {
            &meta::mb_limiter_mono,
            &meta::mb_limiter_stereo,
            &meta::sc_mb_limiter_mono,
            &meta::sc_mb_limiter_stereo
        };

        static ui::Module *ui_factory(const meta::plugin_t *meta)
        {
            return new mb_limiter_ui(meta);
        }

        static ui::Factory factory(ui_factory, plugin_uis, 4);

        static const char *note_names[] =
        {
            "c", "c#", "d", "d#", "e", "f", "f#", "g", "g#", "a", "a#", "b"
        };

        template <class T>
        T *mb_limiter_ui::find_split_widget(const char *fmt, const char *base, size_t id)
        {
            char widget_id[64];
            ::snprintf(widget_id, sizeof(widget_id)/sizeof(char), fmt, base, int(id));
            return pWrapper->controller()->widgets()->get<T>(widget_id);
        }

        mb_limiter_ui::split_t *mb_limiter_ui::find_split_by_port(ui::IPort *port)
        {
            for (lltl::iterator<split_t> it = vSplits.values(); it; ++it)
            {
                split_t *d = it.get();
                if ((d->pFreq == port) ||
                    (d->pOn == port))
                    return d;
            }
            return NULL;
        }

        mb_limiter_ui::mb_limiter_ui(const meta::plugin_t *meta): ui::Module(meta)
        {
        }

        mb_limiter_ui::~mb_limiter_ui()
        {
        }

        status_t mb_limiter_ui::slot_split_mouse_in(tk::Widget *sender, void *ptr, void *data)
        {
            // Fetch parameters
            mb_limiter_ui *ui = static_cast<mb_limiter_ui *>(ptr);
            if (ui == NULL)
                return STATUS_BAD_STATE;

            split_t *s = ui->find_split_by_widget(sender);
            if (s != NULL)
                ui->on_split_mouse_in(s);

            return STATUS_OK;
        }

        status_t mb_limiter_ui::slot_split_mouse_out(tk::Widget *sender, void *ptr, void *data)
        {
            // Fetch parameters
            mb_limiter_ui *ui = static_cast<mb_limiter_ui *>(ptr);
            if (ui == NULL)
                return STATUS_BAD_STATE;

            ui->on_split_mouse_out();

            return STATUS_OK;
        }

        ui::IPort *mb_limiter_ui::find_port(const char *fmt, const char *base, size_t id)
        {
            char port_id[32];
            ::snprintf(port_id, sizeof(port_id)/sizeof(char), fmt, base, int(id));
            return pWrapper->port(port_id);
        }

        mb_limiter_ui::split_t *mb_limiter_ui::find_split_by_widget(tk::Widget *widget)
        {
            for (size_t i=0, n=vSplits.size(); i<n; ++i)
            {
                split_t *d = vSplits.uget(i);
                if ((d->wMarker == widget) ||
                    (d->wNote == widget))
                    return d;
            }
            return NULL;
        }

        void mb_limiter_ui::on_split_mouse_in(split_t *s)
        {
            if (s->wNote != NULL)
            {
                s->wNote->visibility()->set(true);
                update_split_note_text(s);
            }
        }

        void mb_limiter_ui::on_split_mouse_out()
        {
            for (size_t i=0, n=vSplits.size(); i<n; ++i)
            {
                split_t *d = vSplits.uget(i);
                if (d->wNote != NULL)
                    d->wNote->visibility()->set(false);
            }
        }

        void mb_limiter_ui::add_splits()
        {
            static const char *fmt = "%s_%d";

            for (size_t port_id=0; port_id<meta::mb_limiter::BANDS_MAX; ++port_id)
            {
                split_t s;

                s.pUI           = this;

                s.wMarker       = find_split_widget<tk::GraphMarker>(fmt, "split_marker", port_id);
                s.wNote         = find_split_widget<tk::GraphText>(fmt, "split_note", port_id);

                s.pFreq         = find_port(fmt, "sf", port_id);
                s.pOn           = find_port(fmt, "se", port_id);

                s.fFreq         = (s.pFreq != NULL) ? s.pFreq->value() : 0.0f;
                s.bOn           = (s.pOn != NULL) ? s.pOn->value() >= 0.5f : false;

                if (s.wMarker != NULL)
                {
                    s.wMarker->slots()->bind(tk::SLOT_MOUSE_IN, slot_split_mouse_in, this);
                    s.wMarker->slots()->bind(tk::SLOT_MOUSE_OUT, slot_split_mouse_out, this);
                }

                if (s.pFreq != NULL)
                    s.pFreq->bind(this);
                if (s.pOn != NULL)
                    s.pOn->bind(this);

                vSplits.add(&s);
            }

            resort_active_splits();
        }

        ssize_t mb_limiter_ui::compare_splits_by_freq(const split_t *a, const split_t *b)
        {
            if (a->fFreq < b->fFreq)
                return -1;
            return (a->fFreq > b->fFreq) ? 1 : 0;
        }

        void mb_limiter_ui::resort_active_splits()
        {
            vActiveSplits.clear();

            // Form unsorted list of active splits
            for (lltl::iterator<split_t> it = vSplits.values(); it; ++it)
            {
                split_t *s = it.get();
                if (!s->bOn)
                    continue;
                vActiveSplits.add(s);
            }

            // Sort active splits
            vActiveSplits.qsort(compare_splits_by_freq);
        }

        void mb_limiter_ui::update_split_note_text(split_t *s)
        {
            // Get the frequency
            float freq = (s->pFreq != NULL) ? s->pFreq->value() : -1.0f;
            if (freq < 0.0f)
            {
                s->wNote->visibility()->set(false);
                return;
            }

            // Update the note name displayed in the text
            {
                // Fill the parameters
                expr::Parameters params;
                tk::prop::String lc_string;
                LSPString text;
                lc_string.bind(s->wNote->style(), pDisplay->dictionary());
                SET_LOCALE_SCOPED(LC_NUMERIC, "C");

                // Frequency
                text.fmt_ascii("%.2f", freq);
                params.set_string("frequency", &text);

                // Filter number and audio channel
                lc_string.set("lists.mb_limiter.split_id");

                lc_string.params()->set_int("id", (vSplits.index_of(s) % meta::mb_limiter::BANDS_MAX)+1);
                lc_string.format(&text);
                params.set_string("id", &text);
                lc_string.params()->clear();

                // Process split note
                float note_full = dspu::frequency_to_note(freq);
                if (note_full != dspu::NOTE_OUT_OF_RANGE)
                {
                    note_full += 0.5f;
                    ssize_t note_number = ssize_t(note_full);

                    // Note name
                    ssize_t note        = note_number % 12;
                    text.fmt_ascii("lists.notes.names.%s", note_names[note]);
                    lc_string.set(&text);
                    lc_string.format(&text);
                    params.set_string("note", &text);

                    // Octave number
                    ssize_t octave      = (note_number / 12) - 1;
                    params.set_int("octave", octave);

                    // Cents
                    ssize_t note_cents  = (note_full - float(note_number)) * 100 - 50;
                    if (note_cents < 0)
                        text.fmt_ascii(" - %02d", -note_cents);
                    else
                        text.fmt_ascii(" + %02d", note_cents);
                    params.set_string("cents", &text);

                    s->wNote->text()->set("lists.mb_limiter.notes.full", &params);
                }
                else
                    s->wNote->text()->set("lists.mb_limiter.notes.unknown", &params);
            }
        }

        status_t mb_limiter_ui::post_init()
        {
            status_t res = ui::Module::post_init();
            if (res != STATUS_OK)
                return res;

            // Add splits widgets
            add_splits();

            return STATUS_OK;
        }

        void mb_limiter_ui::notify(ui::IPort *port, size_t flags)
        {
            bool need_resort_active_splits = false;
            split_t *freq_initiator = NULL;

            for (size_t i=0, n=vSplits.size(); i<n; ++i)
            {
                split_t *s = vSplits.uget(i);
                if (s->pOn == port)
                {
                    s->bOn          = port->value() >= 0.5f;
                    need_resort_active_splits = true;
                }
                if (s->pFreq == port)
                {
                    s->fFreq        = port->value();
                    update_split_note_text(s);

                    if (flags & ui::PORT_USER_EDIT)
                    {
                        if (s->bOn)
                            freq_initiator = s;
                    }
                    else if (s->bOn)
                        need_resort_active_splits = true;
                }
            }

            // Resort order of active splits if needed
            if (need_resort_active_splits)
                resort_active_splits();
            if (freq_initiator != NULL)
                toggle_active_split_fequency(freq_initiator);
        }

        void mb_limiter_ui::toggle_active_split_fequency(split_t *initiator)
        {
            lltl::parray<ui::IPort> notify_list;
            bool left_position  = true;
            const float freq    = initiator->pFreq->value();

            // Form unsorted list of active splits
            for (lltl::iterator<split_t> it = vActiveSplits.values(); it; ++it)
            {
                split_t *s = it.get();
                if (!s->bOn)
                    continue;

                // Main logic
                if (s == initiator)
                {
                    left_position = false;
                    continue;
                }

                if (left_position)
                {
                    if ((s->pFreq != NULL) && (s->fFreq > freq * 0.999f))
                    {
                        s->pFreq->set_value(freq * 0.999f);
                        notify_list.add(s->pFreq);
                    }
                }
                else
                {
                    if ((s->pFreq != NULL) && (s->fFreq < freq * 1.001f))
                    {
                        s->pFreq->set_value(freq * 1.001f);
                        notify_list.add(s->pFreq);
                    }
                }
            }

            // Notify all modified ports
            for (lltl::iterator<ui::IPort> it = notify_list.values(); it; ++it)
                it->notify_all(ui::PORT_NONE);
        }

    } /* namespace plugui */
} /* namespace lsp */
