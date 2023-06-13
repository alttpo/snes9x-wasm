package rex

import (
	"fmt"
)

//go:wasm-module rex
//export ppux_cmd_write
func ppux_cmd_write(b *uint32, l uint32) bool

//go:wasm-module rex
//export ppux_upload
func ppux_upload(addr uint32, data *uint8, size uint32) bool

type ppux struct{}

var PPUX ppux

func (x *ppux) CmdWrite(p []uint32) (err error) {
	res := ppux_cmd_write(&p[0], uint32(len(p)))
	if !res {
		return fmt.Errorf("error writing to ppux command queue")
	}
	return nil
}

func (x *ppux) Upload(addr uint32, data []byte) (err error) {
	res := ppux_upload(addr, &data[0], uint32(len(data)))
	if !res {
		return fmt.Errorf("error uploading to ppux ram")
	}
	return nil
}
