package rex

import "time"

//go:wasm-module rex
//export wait_for_event
func wait_for_event(timeout_usec uint32, o_event *uint32) bool

//go:wasm-module rex
//export ack_last_event
func ack_last_event()

// WaitForEvent blocks the wasm thread for timeout_usec microseconds until an
// emulator event occurs. The emulator is then blocked until AcknowledgeLastEvent
// is called.
func WaitForEvent(timeout time.Duration) (event uint32, ok bool) {
	ok = wait_for_event(uint32(timeout.Microseconds()), &event)
	return
}

// Acknowledge the last event to unblock the emulator.
func AcknowledgeLastEvent() {
	ack_last_event()
}
