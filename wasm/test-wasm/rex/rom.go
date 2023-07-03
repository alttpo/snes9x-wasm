package rex

import (
	"fmt"
	"unsafe"
)

//go:wasmimport rex mem_read_rom
func rom_read(b unsafe.Pointer, l uint32, offset uint32) int32

type rom struct{}

// ROM provides linear read access to emulated snes ROM
var ROM rom

func (r *rom) ReadAt(p []byte, off int64) (n int, err error) {
	res := rom_read(unsafe.Pointer(&p[0]), uint32(len(p)), uint32(off))
	if res != 0 {
		err = fmt.Errorf("unable to read from ROM")
		return 0, err
	}
	return n, nil
}
