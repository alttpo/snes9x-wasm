package frame

import (
	"io"
)

type Reader struct {
	b [64]byte // recv frame data
	h int      // head
	t int      // tail
	f bool     // read frame header?
	x uint8    // frame header byte
	l int      // frame length

	r   io.Reader
	err error
}

func NewReader(r io.Reader) *Reader {
	return &Reader{r: r}
}

func (r *Reader) ReadInto(f *F) (ok bool, err error) {
	if r.err == nil {
		if r.t < 64 {
			// read more data:
			var n int
			n, r.err = r.r.Read(r.b[r.t:])
			if r.err != nil && r.h >= r.t {
				err = r.err
				return
			}
			r.t += n
		}
	} else {
		// consumed all the data, report the last read error:
		if r.h >= r.t {
			err = r.err
			return
		}
	}

	if !r.f {
		// read the frame header byte:

		// [7654 3210]
		//  fcll llll   f = final frame of message
		//              c = channel (0..1)
		//              l = length of frame (0..63)
		r.x = r.b[r.h]
		r.h++
		// determine length of frame:
		r.l = int(r.x & 63)
		r.f = true
	}

	if r.h+r.l > r.t {
		// not enough data for the full frame:
		return
	}

	// fill in the frame details:
	f.fin = (r.x>>7)&1 == 1
	f.chn = (r.x >> 6) & 1
	copy(f.b[0:], r.b[r.h:r.h+r.l])
	f.n = r.l
	ok = true

	// ready to read the next header byte:
	r.f = false
	r.h += r.l

	if r.h >= r.t {
		// buffer is empty:
		r.h = 0
		r.t = 0
		return
	}

	// remaining bytes begin the next frame:
	copy(r.b[0:], r.b[r.h:])
	r.t -= r.h
	r.h = 0

	return
}
