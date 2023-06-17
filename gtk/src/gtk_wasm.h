/*****************************************************************************\
     Snes9x - Portable Super Nintendo Entertainment System (TM) emulator.
                This file is licensed under the Snes9x License.
   For further information, consult the LICENSE file in the root directory.
\*****************************************************************************/

#ifndef __GTK_WASM_H
#define __GTK_WASM_H

#include "gtk_compat.h"
#include "gtk_s9x.h"
#include "gtk_builder_window.h"

void snes9x_wasm_open(Snes9xWindow *window);

class Snes9xWasm : public GtkBuilderWindow {
public:
    Snes9xWasm();
    ~Snes9xWasm();

    void show();

    void connect_signals();

    Glib::RefPtr<Gtk::TextBuffer> textBuffer;
};

#endif /* __GTK_WASM_H */
