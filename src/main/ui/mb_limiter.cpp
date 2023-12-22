/*
 * Copyright (C) 2020 Linux Studio Plugins Project <https://lsp-plug.in/>
 *           (C) 2020 Vladimir Sadovnikov <sadko4u@gmail.com>
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

                 if (s.wMarker != NULL)
                 {
                     s.wMarker->slots()->bind(tk::SLOT_MOUSE_IN, slot_split_mouse_in, this);
                     s.wMarker->slots()->bind(tk::SLOT_MOUSE_OUT, slot_split_mouse_out, this);
                 }

                 if (s.pFreq != NULL)
                     s.pFreq->bind(this);

                 vSplits.add(&s);
             }
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
             for (size_t i=0, n=vSplits.size(); i<n; ++i)
             {
                 split_t *d = vSplits.uget(i);
                 if (d->pFreq == port)
                     update_split_note_text(d);
             }

         }

     } // namespace plugui
 } // namespace lsp
