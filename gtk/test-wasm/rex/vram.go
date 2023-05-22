package rex

import "fmt"

//go:wasm-module rex
//export vram_read
func vram_read(b *byte, l uint32, offset uint32) bool

type vram struct{}

// VRAM provides linear read access to emulated snes VRAM
var VRAM vram

func (r *vram) ReadAt(p []byte, off int64) (n int, err error) {
	res := vram_read(&p[0], uint32(len(p)), uint32(off))
	if !res {
		err = fmt.Errorf("unable to read fvram VRAM at offset $%04x size $%x", off, len(p))
		return 0, err
	}
	return n, nil
}
