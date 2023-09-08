package frame

type F struct {
	b   [64]byte
	n   int
	fin bool
	chn uint8
}

func (f *F) IsFinal() bool  { return f.fin }
func (f *F) Channel() uint8 { return f.chn }
func (f *F) Data() []byte   { return f.b[0:f.n] }
