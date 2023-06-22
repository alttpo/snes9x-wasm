package rex

import (
	"time"
	"unsafe"
)

//go:wasm-module rex
//export wait_for_event
//go:wasmimport rex wait_for_event
func wait_for_event(timeout_usec uint32, o_event unsafe.Pointer) uint32

//go:wasm-module rex
//export ack_last_event
//go:wasmimport rex ack_last_event
func ack_last_event()

// WaitForEvent blocks the wasm thread for timeout_usec microseconds until an
// emulator event occurs. The emulator is then blocked until AcknowledgeLastEvent
// is called.
func WaitForEvent(timeout time.Duration) (event uint32, ok bool) {
	ret := wait_for_event(uint32(timeout.Microseconds()), (unsafe.Pointer)(&event))
	ok = ret != 0
	return
}

// Acknowledge the last event to unblock the emulator.
func AcknowledgeLastEvent() {
	ack_last_event()
}
