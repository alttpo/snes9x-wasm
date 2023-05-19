package main

import "unsafe"

//go:wasm-module snes
//export rom_read
func rom_read(b *byte, l uint32, offset uint32) bool

//go:wasm-module snes
//export wram_read
func wram_read(b []byte, offset uint32) bool

//go:wasm-module snes
//export ppux_write
func ppux_write(v []uint32) bool

//go:wasm-module snes
//export poll_events
func poll_events(mask uint32) uint32

func ReadROM(b []byte, offset uint32) bool {
	return rom_read((*byte)(unsafe.Pointer(&b[0])), uint32(len(b)), offset)
}
