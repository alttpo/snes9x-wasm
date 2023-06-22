package rex

import (
	"fmt"
	"unsafe"
)

//go:wasm-module rex
//export oam_read
//go:wasmimport rex oam_read
func oam_read(b unsafe.Pointer, l uint32, offset uint32) int32

type oam struct{}

// OAM provides linear read access to emulated snes OAM
var OAM oam

func (r *oam) ReadAt(p []byte, off int64) (n int, err error) {
	res := oam_read(unsafe.Pointer(&p[0]), uint32(len(p)), uint32(off))
	if res != 0 {
		err = fmt.Errorf("unable to read foam OAM at offset $%03x size $%x", off, len(p))
		return 0, err
	}
	return n, nil
}
