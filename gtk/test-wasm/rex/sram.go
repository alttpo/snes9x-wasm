package rex

import "fmt"

//go:wasm-module rex
//export sram_read
func sram_read(b *byte, l uint32, offset uint32) bool

//go:wasm-module rex
//export sram_write
func sram_write(b *byte, l uint32, offset uint32) bool

type sram struct{}

// SRAM provides linear read access to emulated snes SRAM
var SRAM sram

func (r *sram) ReadAt(p []byte, off int64) (n int, err error) {
	res := sram_read(&p[0], uint32(len(p)), uint32(off))
	if !res {
		err = fmt.Errorf("unable to read from SRAM at offset $%06x size $%x", off, len(p))
		return 0, err
	}
	return n, nil
}

func (r *sram) WriteAt(p []byte, off int64) (n int, err error) {
	res := sram_write(&p[0], uint32(len(p)), uint32(off))
	if !res {
		err = fmt.Errorf("unable to write to SRAM at offset $%06x size $%x", off, len(p))
		return 0, err
	}
	return n, nil
}
