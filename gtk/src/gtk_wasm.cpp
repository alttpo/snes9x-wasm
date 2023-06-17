/*****************************************************************************\
     Snes9x - Portable Super Nintendo Entertainment System (TM) emulator.
                This file is licensed under the Snes9x License.
   For further information, consult the LICENSE file in the root directory.
\*****************************************************************************/

#include "gtk_compat.h"
#include "gtk_wasm.h"
#include "snes9x.h"
#include "gfx.h"

#include "wasm_host.h"

Snes9xWasm *wasmWindow = nullptr;

void snes9x_wasm_open(Snes9xWindow *window) {
    if (!wasmWindow)
        wasmWindow = new Snes9xWasm();

    wasmWindow->window->set_transient_for(*window->window.get());

    wasmWindow->show();
}

Snes9xWasm::Snes9xWasm()
    : GtkBuilderWindow("wasm_window") {
    gtk_widget_realize(GTK_WIDGET(window->gobj()));

    auto textView = get_object<Gtk::TextView>("wasm_console_view");
    textView->set_buffer(top_level->wasmTextBuffer);

    connect_signals();
}

Snes9xWasm::~Snes9xWasm() {
}

void Snes9xWasm::connect_signals() {
    //window->signal_key_press_event().connect(sigc::mem_fun(*this, &Snes9xWasm::key_pressed), false);
    const Glib::RefPtr<Gtk::CheckButton> &autoScrollChk = get_object<Gtk::CheckButton>("wasm-auto-scroll");
    autoScrollChk->signal_toggled().connect([&]{
        auto_scroll = get_object<Gtk::CheckButton>("wasm-auto-scroll")->get_active();
    });
    autoScrollChk->set_active(auto_scroll);
}

void Snes9xWasm::show() {
    window->show();
}
