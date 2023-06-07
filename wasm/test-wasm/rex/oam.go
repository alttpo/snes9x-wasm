package rex

import "fmt"

//go:wasm-module rex
//export oam_read
func oam_read(b *byte, l uint32, offset uint32) bool

type oam struct{}

// OAM provides linear read access to emulated snes OAM
var OAM oam

func (r *oam) ReadAt(p []byte, off int64) (n int, err error) {
	res := oam_read(&p[0], uint32(len(p)), uint32(off))
	if !res {
		err = fmt.Errorf("unable to read foam OAM at offset $%03x size $%x", off, len(p))
		return 0, err
	}
	return n, nil
}
