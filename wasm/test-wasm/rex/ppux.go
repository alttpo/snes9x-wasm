package rex

import (
	"fmt"
	"unsafe"
)

//go:wasm-module rex
//export ppux_cmd_write
//go:wasmimport rex ppux_cmd_write
func ppux_cmd_write(b unsafe.Pointer, l uint32) int32

//go:wasm-module rex
//export ppux_vram_upload
//go:wasmimport rex ppux_vram_upload
func ppux_vram_upload(addr uint32, data unsafe.Pointer, size uint32) int32

//go:wasm-module rex
//export ppux_cgram_upload
//go:wasmimport rex ppux_cgram_upload
func ppux_cgram_upload(addr uint32, data unsafe.Pointer, size uint32) int32

type ppux struct {
	VRAM  ppux_vram
	CGRAM ppux_cgram
}

type ppux_vram struct{}
type ppux_cgram struct{}

var PPUX ppux

func (x *ppux) CmdWrite(p []uint32) (err error) {
	res := ppux_cmd_write(unsafe.Pointer(&p[0]), uint32(len(p)))
	if res != 0 {
		return fmt.Errorf("error writing to ppux command queue")
	}
	return nil
}

func (x *ppux_vram) Upload(addr uint32, data []byte) (err error) {
	res := ppux_vram_upload(addr, unsafe.Pointer(&data[0]), uint32(len(data)))
	if res != 0 {
		return fmt.Errorf("error uploading to ppux vram")
	}
	return nil
}

func (x *ppux_cgram) Upload(addr uint32, data []byte) (err error) {
	res := ppux_cgram_upload(addr, unsafe.Pointer(&data[0]), uint32(len(data)))
	if res != 0 {
		return fmt.Errorf("error uploading to ppux cgram")
	}
	return nil
}
