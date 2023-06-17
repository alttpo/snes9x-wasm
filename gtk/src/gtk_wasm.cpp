/*****************************************************************************\
     Snes9x - Portable Super Nintendo Entertainment System (TM) emulator.
                This file is licensed under the Snes9x License.
   For further information, consult the LICENSE file in the root directory.
\*****************************************************************************/

#include <string>
#include <stdlib.h>
#include "gtk_compat.h"
#include "gtk_wasm.h"
#include "snes9x.h"
#include "gfx.h"

static Snes9xWasm *wasm = nullptr;

void snes9x_wasm_open(Snes9xWindow *window) {
    if (!wasm)
        wasm = new Snes9xWasm();

    wasm->window->set_transient_for(*window->window.get());

    wasm->show();
}

Snes9xWasm::Snes9xWasm()
    : GtkBuilderWindow("wasm_window") {
    gtk_widget_realize(GTK_WIDGET(window->gobj()));

    connect_signals();
}

Snes9xWasm::~Snes9xWasm() {
}

void Snes9xWasm::connect_signals() {
    //window->signal_key_press_event().connect(sigc::mem_fun(*this, &Snes9xWasm::key_pressed), false);
}

void Snes9xWasm::show() {
    window->show();
}
