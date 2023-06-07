package rex

import (
	"fmt"
)

//go:wasm-module rex
//export ppux_write
func ppux_write(b *uint32, l uint32) bool

type ppux struct{}

var PPUX ppux

func (x *ppux) Write(p []uint32) (err error) {
	res := ppux_write(&p[0], uint32(len(p)))
	if !res {
		return fmt.Errorf("error writing to ppux command queue")
	}
	return nil
}
