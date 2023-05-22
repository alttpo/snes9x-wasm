package rex

import "fmt"

//go:wasm-module rex
//export rom_read
func rom_read(b *byte, l uint32, offset uint32) bool

type rom struct{}

// ROM provides linear read access to emulated snes ROM
var ROM rom

func (r *rom) ReadAt(p []byte, off int64) (n int, err error) {
	res := rom_read(&p[0], uint32(len(p)), uint32(off))
	if !res {
		err = fmt.Errorf("unable to read from ROM at offset $%06x size $%x", off, len(p))
		return 0, err
	}
	return n, nil
}
