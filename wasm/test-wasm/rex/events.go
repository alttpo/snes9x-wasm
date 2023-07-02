package rex

import (
	"time"
	"unsafe"
)

//go:wasmimport rex event_wait_for
func event_wait_for(timeout_usec uint32, o_event unsafe.Pointer) uint32

//go:wasmimport rex event_ack_last
func event_ack_last()

//go:wasmimport rex event_register_pc_break
func event_register_pc_break(pc uint32, timeout_nsec uint32) uint32

//go:wasmimport rex event_unregister_pc_break
func event_unregister_pc_break(pc uint32)

// WaitForEvent blocks the wasm thread for timeout_usec microseconds until an
// emulator event occurs. The emulator is then blocked until AcknowledgeLastEvent
// is called.
func WaitForEvent(timeout time.Duration) (event uint32, ok bool) {
	ret := event_wait_for(uint32(timeout.Microseconds()), unsafe.Pointer(&event))
	ok = ret != 0
	return
}

// Acknowledge the last event to unblock the emulator.
func AcknowledgeLastEvent() {
	event_ack_last()
}

func RegisterPCEvent(pc uint32, timeout time.Duration) uint32 {
	return event_register_pc_break(pc, uint32(timeout.Nanoseconds()))
}

func UnregisterPCEvent(pc uint32) {
	event_unregister_pc_break(pc)
}
