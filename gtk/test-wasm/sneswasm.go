package main

import (
	"time"
)

//go:wasm-module snes
//export wait_for_events
func wait_for_events(mask uint32, timeout_usec uint32, o_events *uint32) bool

//go:wasm-module snes
//export rom_read
func rom_read(b *byte, l uint32, offset uint32) bool

//go:wasm-module snes
//export wram_read
func wram_read(b *byte, l uint32, offset uint32) bool

//go:wasm-module snes
//export ppux_write
func ppux_write(b *uint32, l uint32) bool

//go:wasm-module snes
//export net_tcp_listen
func net_tcp_listen(port uint32) int32

//go:wasm-module snes
//export net_tcp_accept
func net_tcp_accept(fd int32) int32

type NetPollSlot struct {
	Slot    int32
	Events  uint32
	Revents uint32
}

//go:wasm-module snes
//export net_poll
func net_poll(poll_slots *NetPollSlot, poll_slots_len uint32) int32

//go:wasm-module snes
//export net_close
func net_close(fd int32) int32

func WaitForEvents(mask uint32, timeout time.Duration) (events uint32, ok bool) {
	ok = wait_for_events(mask, uint32(timeout.Microseconds()), &events)
	return
}

func ReadROM(b []byte, offset uint32) bool {
	return rom_read(&b[0], uint32(len(b)), offset)
}

func ReadWRAM(b []byte, offset uint32) bool {
	return wram_read(&b[0], uint32(len(b)), offset)
}

func PPUXWrite(b []uint32) bool {
	return ppux_write(&b[0], uint32(len(b)))
}

func NetTCPListen(port uint32) int32 {
	return net_tcp_listen(port)
}

func NetTCPAccept(fd int32) int32 {
	return net_tcp_accept(fd)
}

func NetPoll(poll_slots []NetPollSlot) int32 {
	return net_poll(&poll_slots[0], uint32(len(poll_slots)))
}

func NetClose(fd int32) int32 {
	return net_close(fd)
}
