package rex

type ppux struct {
	VRAM  ppux_vram
	CGRAM ppux_cgram
}

type ppux_vram struct{}
type ppux_cgram struct{}

var PPUX ppux

func (x *ppux) CmdWrite(p []uint32) (err error) {
	return nil
}

func (x *ppux_vram) Upload(addr uint32, data []byte) (err error) {
	return nil
}

func (x *ppux_cgram) Upload(addr uint32, data []byte) (err error) {
	return nil
}
