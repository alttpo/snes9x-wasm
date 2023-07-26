package rex

import (
	"time"
	"unsafe"
)

//go:wasmimport rex event_wait_for
func event_wait_for(timeout_nsec uint32, o_event unsafe.Pointer) uint32

type Event = uint32

const (
	Ev_none Event = iota
	Ev_shutdown
	Ev_snes_nmi
	Ev_snes_irq
	Ev_ppu_frame_start
	Ev_ppu_frame_end
	Ev_ppu_frame_skip
	Ev_iovm0_read_complete
	Ev_iovm1_read_complete
	Ev_iovm0_end
	Ev_iovm1_end
)

// EventWaitFor blocks the wasm thread for timeout_nsec nanoseconds until an
// emulator event occurs. The emulator is then blocked until EventAcknowledge
// is called.
func EventWaitFor(timeout time.Duration) (event Event, ok bool) {
	ret := event_wait_for(uint32(timeout.Nanoseconds()), unsafe.Pointer(&event))
	ok = ret != 0
	return
}
