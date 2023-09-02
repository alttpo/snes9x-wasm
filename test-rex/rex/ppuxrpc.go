package rex

import "bytes"

const (
	rex_cmd_ppux_cmd_upload rexCommand = 16 + iota
	rex_cmd_ppux_vram_upload
	rex_cmd_ppux_cgram_upload
)

type PPUXCmdUploadComplete func(err error)
type PPUXVRAMUploadComplete func(err error)
type PPUXCGRAMUploadComplete func(err error)

type PPUXRPC interface {
	PPUXCmdUpload(p []uint32, cb PPUXCmdUploadComplete)
	PPUXVRAMUpload(addr uint32, data []byte, cb PPUXVRAMUploadComplete)
	PPUXCGRAMUpload(addr uint32, data []byte, cb PPUXCGRAMUploadComplete)
}

func (r *RPC) PPUXCmdUpload(p []uint32, cb PPUXCmdUploadComplete) {
	//TODO implement me
	panic("implement me")
}

func (r *RPC) parsePPUXCmdUploadComplete(br *bytes.Reader) error {
	panic("TODO")
}

func (r *RPC) PPUXVRAMUpload(addr uint32, data []byte, cb PPUXVRAMUploadComplete) {
	//TODO implement me
	panic("implement me")
}

func (r *RPC) parsePPUXVRAMUploadComplete(br *bytes.Reader) error {
	panic("TODO")
}

func (r *RPC) PPUXCGRAMUpload(addr uint32, data []byte, cb PPUXCGRAMUploadComplete) {
	//TODO implement me
	panic("implement me")
}

func (r *RPC) parsePPUXCGRAMUploadComplete(br *bytes.Reader) error {
	panic("TODO")
}
