package rex

import (
	"fmt"
	"unsafe"
)

//go:wasmimport rex mem_read_sram
func sram_read(b unsafe.Pointer, l uint32, offset uint32) int32

//go:wasmimport rex mem_write_sram
func sram_write(b unsafe.Pointer, l uint32, offset uint32) int32

type sram struct{}

// SRAM provides linear read access to emulated snes SRAM
var SRAM sram

func (r *sram) ReadAt(p []byte, off int64) (n int, err error) {
	res := sram_read(unsafe.Pointer(&p[0]), uint32(len(p)), uint32(off))
	if res != 0 {
		err = fmt.Errorf("unable to read from SRAM at offset $%06x size $%x", off, len(p))
		return 0, err
	}
	return n, nil
}

func (r *sram) WriteAt(p []byte, off int64) (n int, err error) {
	res := sram_write(unsafe.Pointer(&p[0]), uint32(len(p)), uint32(off))
	if res != 0 {
		err = fmt.Errorf("unable to write to SRAM at offset $%06x size $%x", off, len(p))
		return 0, err
	}
	return n, nil
}
