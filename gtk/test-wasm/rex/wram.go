package rex

import "fmt"

//go:wasm-module rex
//export wram_read
func wram_read(b *byte, l uint32, offset uint32) bool

//go:wasm-module rex
//export wram_write
func wram_write(b *byte, l uint32, offset uint32) bool

type wram struct{}

// WRAM provides linear read access to emulated snes WRAM
var WRAM wram

func (r *wram) ReadAt(p []byte, off int64) (n int, err error) {
	res := wram_read(&p[0], uint32(len(p)), uint32(off))
	if !res {
		err = fmt.Errorf("unable to read from WRAM at offset $%05x size $%x", off, len(p))
		return 0, err
	}
	return n, nil
}

func (r *wram) WriteAt(p []byte, off int64) (n int, err error) {
	res := wram_write(&p[0], uint32(len(p)), uint32(off))
	if !res {
		err = fmt.Errorf("unable to write to WRAM at offset $%05x size $%x", off, len(p))
		return 0, err
	}
	return n, nil
}
