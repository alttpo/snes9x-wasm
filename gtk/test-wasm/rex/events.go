package rex

import "time"

//go:wasm-module rex
//export wait_for_events
func wait_for_events(mask uint32, timeout_usec uint32, o_events *uint32) bool

func WaitForEvents(mask uint32, timeout time.Duration) (events uint32, ok bool) {
	ok = wait_for_events(mask, uint32(timeout.Microseconds()), &events)
	return
}
