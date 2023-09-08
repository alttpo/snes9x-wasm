package rex

import (
	"fmt"
	"rex/iovm1"
)

const (
	rex_cmd_iovm_upload rexCommand = iota
	rex_cmd_iovm_start
	rex_cmd_iovm_stop
	rex_cmd_iovm_reset
	rex_cmd_iovm_flags
	rex_cmd_iovm_getstate
)

const (
	rex_notify_iovm_end rexNotification = iota
	rex_notify_iovm_read
	rex_notify_iovm_write
	rex_notify_iovm_wait
)

type IOVM1OnRead interface {
	IOVM1OnReadStart(pc uint32, tdu uint8, addr uint32, dlen uint32)
	IOVM1OnReadChunk(chunk []byte)
	IOVM1OnReadEnd()
}

type IOVM1OnPrgEnd func(pc uint32, o iovm1.Opcode, result iovm1.Result, state iovm1.State)
type IOVM1OnWriteStart func(pc uint32, tdu uint8, addr uint32, len uint32)
type IOVM1OnWriteEnd func()
type IOVM1OnWaitComplete func(pc uint32, o iovm1.Opcode, result iovm1.Result, state iovm1.State)

type IOVM1UploadComplete func(err error)
type IOVM1StartComplete func(err error)
type IOVM1StopComplete func(err error)
type IOVM1SetFlagsComplete func(err error)
type IOVM1ResetComplete func(err error)
type IOVM1GetStateComplete func(state iovm1.State, err error)

type IOVM1RPC interface {
	IOVM1Upload(vmprog []byte, cb IOVM1UploadComplete)
	IOVM1Start(cb IOVM1StartComplete)
	IOVM1Stop(cb IOVM1StopComplete)
	IOVM1SetFlags(flags iovm1.Flags, cb IOVM1SetFlagsComplete)
	IOVM1Reset(cb IOVM1ResetComplete)
	IOVM1GetState(cb IOVM1GetStateComplete)

	IOVM1OnPrgEnd(cb IOVM1OnPrgEnd)
	IOVM1OnRead(cb IOVM1OnRead)
	IOVM1OnWriteStart(cb IOVM1OnWriteStart)
	IOVM1OnWriteEnd(cb IOVM1OnWriteEnd)
	IOVM1OnWaitComplete(cb IOVM1OnWaitComplete)
}

func (r *RPC) IOVM1Upload(vmprog []byte, cb IOVM1UploadComplete) {
	var err error
	_, err = r.fw.Write([]byte{rex_cmd_iovm_upload})
	if err != nil {
		cb(err)
		return
	}
	_, err = r.fw.Write(vmprog)
	if err != nil {
		cb(err)
		return
	}
	err = r.fw.EndMessage()
	if err != nil {
		cb(err)
		return
	}

	// push the callback to the tail of the queue:
	r.iovm1UploadComplete = append(r.iovm1UploadComplete, cb)
	return
}

func (r *RPC) parseIOVM1UploadComplete(ch *channelState) (err error) {
	if ch.buf.Len() < 2 {
		err = fmt.Errorf("parseIOVM1UploadComplete: expected at least 2 bytes")
		return
	}

	// parse the message:
	var result rexResult
	var vmerr iovm1.Result
	if result, err = ch.buf.ReadByte(); err != nil {
		return
	}
	if vmerr, err = ch.buf.ReadByte(); err != nil {
		return
	}

	// pop a callback off the head of the queue:
	if cb, ok := pop(&r.iovm1UploadComplete); ok && cb != nil {
		var cberr error
		if result == rex_success {
			cberr = nil
		} else if result == rex_cmd_error {
			cberr = iovm1.Errors[vmerr]
		} else {
			cberr = Errors[result]
		}
		cb(cberr)
	}

	return
}

func (r *RPC) IOVM1Start(cb IOVM1StartComplete) {
	var err error
	_, err = r.fw.Write([]byte{rex_cmd_iovm_start})
	if err != nil {
		cb(err)
		return
	}
	err = r.fw.EndMessage()
	if err != nil {
		cb(err)
		return
	}

	r.iovm1StartComplete = append(r.iovm1StartComplete, cb)
	return
}

func (r *RPC) parseIOVM1StartComplete(ch *channelState) error {
	panic("TODO")
}

func (r *RPC) IOVM1Stop(cb IOVM1StopComplete) {
	var err error
	_, err = r.fw.Write([]byte{rex_cmd_iovm_stop})
	if err != nil {
		cb(err)
		return
	}
	err = r.fw.EndMessage()
	if err != nil {
		cb(err)
		return
	}

	r.iovm1StopComplete = append(r.iovm1StopComplete, cb)
	return
}

func (r *RPC) parseIOVM1StopComplete(ch *channelState) error {
	panic("TODO")
}

func (r *RPC) IOVM1Reset(cb IOVM1ResetComplete) {
	var err error
	_, err = r.fw.Write([]byte{rex_cmd_iovm_reset})
	if err != nil {
		cb(err)
		return
	}
	err = r.fw.EndMessage()
	if err != nil {
		cb(err)
		return
	}

	r.iovm1ResetComplete = append(r.iovm1ResetComplete, cb)
	return
}

func (r *RPC) parseIOVM1ResetComplete(ch *channelState) error {
	panic("TODO")
}

func (r *RPC) IOVM1SetFlags(flags iovm1.Flags, cb IOVM1SetFlagsComplete) {
	var err error
	_, err = r.fw.Write([]byte{rex_cmd_iovm_flags, flags})
	if err != nil {
		cb(err)
		return
	}

	err = r.fw.EndMessage()
	if err != nil {
		cb(err)
		return
	}

	r.iovm1SetFlagsComplete = append(r.iovm1SetFlagsComplete, cb)
	return
}

func (r *RPC) parseIOVM1FlagsComplete(ch *channelState) error {
	panic("TODO")
}

func (r *RPC) IOVM1GetState(cb IOVM1GetStateComplete) {
	var err error
	_, err = r.fw.Write([]byte{rex_cmd_iovm_getstate})
	if err != nil {
		cb(0, err)
		return
	}

	err = r.fw.EndMessage()
	if err != nil {
		cb(0, err)
		return
	}

	r.iovm1GetStateComplete = append(r.iovm1GetStateComplete, cb)
	return
}

func (r *RPC) parseIOVM1GetstateComplete(ch *channelState) error {
	panic("TODO")
}

func (r *RPC) IOVM1OnPrgEnd(cb IOVM1OnPrgEnd)             { r.iovm1OnPrgEnd = cb }
func (r *RPC) IOVM1OnRead(cb IOVM1OnRead)                 { r.iovm1OnRead = cb }
func (r *RPC) IOVM1OnWriteStart(cb IOVM1OnWriteStart)     { r.iovm1OnWriteStart = cb }
func (r *RPC) IOVM1OnWriteEnd(cb IOVM1OnWriteEnd)         { r.iovm1OnWriteEnd = cb }
func (r *RPC) IOVM1OnWaitComplete(cb IOVM1OnWaitComplete) { r.iovm1OnWaitComplete = cb }

func (r *RPC) parseIOVM1OnEnd(ch *channelState) error {
	panic("TODO")
}

func (r *RPC) frameIOVM1OnRead(ch *channelState) (err error) {
	switch ch.state {
	case 0:
		// wait until we have enough header data to consume:
		if ch.buf.Len() < 10 {
			return
		}
		ch.state++
		fallthrough
	case 1:
		pc := LittleEndian.Uint32(ch.buf.Next(4))
		tdu := LittleEndian.Uint8(ch.buf.Next(1))
		addr := LittleEndian.Uint24(ch.buf.Next(3))
		dlen := uint32(LittleEndian.Uint16(ch.buf.Next(2)))
		if dlen == 0 {
			dlen = 65536
		}
		// read started:
		if cb := r.iovm1OnRead; cb != nil {
			cb.IOVM1OnReadStart(pc, tdu, addr, dlen)
		}
		ch.state++
		fallthrough
	case 2:
		// read raw data:
		if ch.buf.Len() > 0 {
			if cb := r.iovm1OnRead; cb != nil {
				cb.IOVM1OnReadChunk(ch.buf.Next(ch.buf.Len()))
			}
		}
		if ch.isFinal {
			if cb := r.iovm1OnRead; cb != nil {
				cb.IOVM1OnReadEnd()
			}
			ch.state++
		}
		break
	default:
		break
	}

	return
}

func (r *RPC) parseIOVM1OnWrite(ch *channelState) error {
	panic("TODO")
}

func (r *RPC) parseIOVM1OnWait(ch *channelState) error {
	panic("TODO")
}
