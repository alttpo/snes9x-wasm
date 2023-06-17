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

static Snes9xWasm *wasm = nullptr;

void snes9x_wasm_open(Snes9xWindow *window) {
    if (!wasm)
        wasm = new Snes9xWasm();

    wasm->window->set_transient_for(*window->window.get());

    wasm->show();
}

size_t snes9x_wasm_append_text(const char *text_begin, const char *text_end) {
    auto textView = wasm->get_object<Gtk::TextView>("wasm_console_view");
    auto textBuffer = wasm->textBuffer;

    // append to end of buffer:
    auto iter = textBuffer->end();
    textBuffer->insert(iter, text_begin, text_end);

    // scroll to end of buffer:
    iter = textBuffer->end();
    textView->scroll_to(iter);

    return text_end - text_begin;
}

Snes9xWasm::Snes9xWasm()
    : GtkBuilderWindow("wasm_window") {
    gtk_widget_realize(GTK_WIDGET(window->gobj()));

    textBuffer = Gtk::TextBuffer::create();
    auto textView = get_object<Gtk::TextView>("wasm_console_view");
    textView->set_buffer(textBuffer);

    // redirect wasm stdout/stderr to append to our text buffer:
    wasm_host_wasi_stdout(snes9x_wasm_append_text);
    wasm_host_wasi_stderr(snes9x_wasm_append_text);

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
