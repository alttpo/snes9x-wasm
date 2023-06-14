package rex

import (
	"fmt"
)

//go:wasm-module rex
//export ppux_cmd_write
func ppux_cmd_write(b *uint32, l uint32) bool

//go:wasm-module rex
//export ppux_vram_upload
func ppux_vram_upload(addr uint32, data *uint8, size uint32) bool

//go:wasm-module rex
//export ppux_cgram_upload
func ppux_cgram_upload(addr uint32, data *uint8, size uint32) bool

type ppux struct {
	VRAM  ppux_vram
	CGRAM ppux_cgram
}

type ppux_vram struct{}
type ppux_cgram struct{}

var PPUX ppux

func (x *ppux) CmdWrite(p []uint32) (err error) {
	res := ppux_cmd_write(&p[0], uint32(len(p)))
	if !res {
		return fmt.Errorf("error writing to ppux command queue")
	}
	return nil
}

func (x *ppux_vram) Upload(addr uint32, data []byte) (err error) {
	res := ppux_vram_upload(addr, &data[0], uint32(len(data)))
	if !res {
		return fmt.Errorf("error uploading to ppux vram")
	}
	return nil
}

func (x *ppux_cgram) Upload(addr uint32, data []byte) (err error) {
	res := ppux_cgram_upload(addr, &data[0], uint32(len(data)))
	if !res {
		return fmt.Errorf("error uploading to ppux cgram")
	}
	return nil
}
