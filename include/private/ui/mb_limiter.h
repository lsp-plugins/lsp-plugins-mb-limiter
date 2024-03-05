/*
 * Copyright (C) 2024 Linux Studio Plugins Project <https://lsp-plug.in/>
 *           (C) 2024 Vladimir Sadovnikov <sadko4u@gmail.com>
 *
 * This file is part of lsp-plugins-mb-limiter
 * Created on: 6 дек. 2023 г.
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
#ifndef PRIVATE_UI_MB_LIMITER_H_
#define PRIVATE_UI_MB_LIMITER_H_

#include <lsp-plug.in/plug-fw/ui.h>
#include <lsp-plug.in/lltl/darray.h>

namespace lsp
{
    namespace plugui
    {
        class mb_limiter_ui: public ui::Module, public ui::IPortListener
        {
            protected:
                typedef struct split_t
                {
                    mb_limiter_ui          *pUI;
                    ui::IPort              *pFreq;          // Split frequency port
                    ui::IPort              *pOn;            // Split enable port

                    float                   fFreq;          // Split frequency
                    bool                    bOn;            // Split is enabled

                    tk::GraphMarker        *wMarker;        // Graph marker for editing
                    tk::GraphText          *wNote;          // Text with note and frequency
                } split_t;

            protected:
                lltl::darray<split_t> vSplits;          // List of split widgets and ports
                lltl::parray<split_t> vActiveSplits;    // List of split widgets and ports

            protected:
                static status_t slot_split_mouse_in(tk::Widget *sender, void *ptr, void *data);
                static status_t slot_split_mouse_out(tk::Widget *sender, void *ptr, void *data);
                static ssize_t  compare_splits_by_freq(const split_t *a, const split_t *b);

            protected:
                template <class T>
                T              *find_split_widget(const char *fmt, const char *base, size_t id);
                ui::IPort      *find_port(const char *fmt, const char *base, size_t id);
                split_t        *find_split_by_widget(tk::Widget *widget);
                split_t        *find_split_by_port(ui::IPort *port);

            protected:
                void            on_split_mouse_in(split_t *s);
                void            on_split_mouse_out();

                void            add_splits();
                void            resort_active_splits();
                void            update_split_note_text(split_t *s);
                void            toggle_active_split_fequency(split_t *initiator);

            public:
                explicit mb_limiter_ui(const meta::plugin_t *meta);
                virtual ~mb_limiter_ui() override;

                virtual status_t    post_init() override;

                virtual void        notify(ui::IPort *port, size_t flags) override;
        };

    } /* namespace plugui */
} /* namespace lsp */

#endif /* PRIVATE_UI_MB_LIMITER_H_ */
