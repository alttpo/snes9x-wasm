package rex

import (
	"fmt"
	"unsafe"
)

//go:wasmimport rex mem_read_vram
func vram_read(b unsafe.Pointer, l uint32, offset uint32) int32

type vram struct{}

// VRAM provides linear read access to emulated snes VRAM
var VRAM vram

func (r *vram) ReadAt(p []byte, off int64) (n int, err error) {
	res := vram_read(unsafe.Pointer(&p[0]), uint32(len(p)), uint32(off))
	if res != 0 {
		err = fmt.Errorf("unable to read fvram VRAM at offset $%04x size $%x", off, len(p))
		return 0, err
	}
	return n, nil
}
