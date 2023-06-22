package rex

import (
	"fmt"
	"unsafe"
)

//go:wasm-module rex
//export wram_read
//go:wasmimport rex wram_read
func wram_read(b unsafe.Pointer, l uint32, offset uint32) int32

//go:wasm-module rex
//export wram_write
//go:wasmimport rex wram_write
func wram_write(b unsafe.Pointer, l uint32, offset uint32) int32

type wram struct{}

// WRAM provides linear read access to emulated snes WRAM
var WRAM wram

func (r *wram) ReadAt(p []byte, off int64) (n int, err error) {
	res := wram_read(unsafe.Pointer(&p[0]), uint32(len(p)), uint32(off))
	if res != 0 {
		err = fmt.Errorf("unable to read from WRAM at offset $%05x size $%x", off, len(p))
		return 0, err
	}
	return n, nil
}

func (r *wram) WriteAt(p []byte, off int64) (n int, err error) {
	res := wram_write(unsafe.Pointer(&p[0]), uint32(len(p)), uint32(off))
	if res != 0 {
		err = fmt.Errorf("unable to write to WRAM at offset $%05x size $%x", off, len(p))
		return 0, err
	}
	return n, nil
}
