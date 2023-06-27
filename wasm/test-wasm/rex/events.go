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

//go:wasm-module rex
//export register_pc_event
//go:wasmimport rex register_pc_event
func register_pc_event(pc uint32, timeout_nsec uint32) uint32

//go:wasm-module rex
//export unregister_pc_event
//go:wasmimport rex unregister_pc_event
func unregister_pc_event(pc uint32)

// WaitForEvent blocks the wasm thread for timeout_usec microseconds until an
// emulator event occurs. The emulator is then blocked until AcknowledgeLastEvent
// is called.
func WaitForEvent(timeout time.Duration) (event uint32, ok bool) {
	ret := wait_for_event(uint32(timeout.Microseconds()), unsafe.Pointer(&event))
	ok = ret != 0
	return
}

// Acknowledge the last event to unblock the emulator.
func AcknowledgeLastEvent() {
	ack_last_event()
}

func RegisterPCEvent(pc uint32, timeout time.Duration) uint32 {
	return register_pc_event(pc, uint32(timeout.Nanoseconds()))
}

func UnregisterPCEvent(pc uint32) {
	unregister_pc_event(pc)
}
