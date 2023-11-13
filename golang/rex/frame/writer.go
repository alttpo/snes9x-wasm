package frame

import "io"

type Writer struct {
	b   [64]byte
	w   io.Writer
	p   int
	chn uint8
}

func NewWriter(w io.Writer, chn uint8) *Writer {
	return &Writer{
		w:   w,
		p:   0,
		chn: chn,
	}
}

func (f *Writer) Write(p []byte) (total int, err error) {
	for len(p) > 0 {
		n := copy(f.b[1+f.p:], p)
		f.p += n
		total += n

		p = p[n:]
		if f.p >= 63 {
			// don't send the last full frame until Close():
			if len(p) == 0 {
				return
			}

			// send a full frame:
			f.b[0] = ((0 & 1) << 7) | ((byte(f.chn) & 1) << 6) | (byte(f.p) & 63)
			n, err = f.w.Write(f.b[0 : 1+f.p])
			if err != nil {
				return
			}

			f.p = 0
		}
	}
	return
}

func (f *Writer) Finalize() (err error) {
	f.b[0] = ((1 & 1) << 7) | ((byte(f.chn) & 1) << 6) | (byte(f.p) & 63)
	_, err = f.w.Write(f.b[0 : 1+f.p])
	f.p = 0
	return
}
