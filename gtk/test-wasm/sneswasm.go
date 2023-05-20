package main

import (
	"time"
	"unsafe"
)

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
//export wait_for_events
func wait_for_events(mask uint32, timeout_usec uint32, o_events *uint32) bool

func ReadROM(b []byte, offset uint32) bool {
	return rom_read((*byte)(unsafe.Pointer(&b[0])), uint32(len(b)), offset)
}

func ReadWRAM(b []byte, offset uint32) bool {
	return wram_read((*byte)(unsafe.Pointer(&b[0])), uint32(len(b)), offset)
}

func WaitForEvents(mask uint32, timeout time.Duration) (events uint32, ok bool) {
	ok = wait_for_events(mask, uint32(timeout.Microseconds()), &events)
	return
}

func PPUXWrite(b []uint32) bool {
	return ppux_write((*uint32)(unsafe.Pointer(&b[0])), uint32(len(b)))
}
