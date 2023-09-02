package rex

import (
	"bytes"
	"testrex/rex/iovm1"
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

type IOVM1OnPrgEnd func(pc uint32, o iovm1.Opcode, result iovm1.Result, state iovm1.State)
type IOVM1OnReadStart func(pc uint32, tdu uint8, addr uint32, len uint32)
type IOVM1OnReadFrame func(chunk []byte)
type IOVM1OnReadEnd func()
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
	IOVM1OnReadStart(cb IOVM1OnReadStart)
	IOVM1OnReadFrame(cb IOVM1OnReadFrame)
	IOVM1OnReadEnd(cb IOVM1OnReadEnd)
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

	r.iovm1UploadComplete = cb
	return
}

func (r *RPC) parseIOVM1UploadComplete(br *bytes.Reader) (err error) {
	var b byte
	if b, err = br.ReadByte(); err != nil {
		return
	}
	_ = b
	panic("TODO")
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

	r.iovm1StartComplete = cb
	return
}

func (r *RPC) parseIOVM1StartComplete(br *bytes.Reader) error {
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

	r.iovm1StopComplete = cb
	return
}

func (r *RPC) parseIOVM1StopComplete(br *bytes.Reader) error {
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

	r.iovm1ResetComplete = cb
	return
}

func (r *RPC) parseIOVM1ResetComplete(br *bytes.Reader) error {
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

	r.iovm1SetFlagsComplete = cb
	return
}

func (r *RPC) parseIOVM1FlagsComplete(br *bytes.Reader) error {
	panic("TODO")
}

func (r *RPC) IOVM1GetState(cb IOVM1GetStateComplete) {
	//TODO implement me
	panic("implement me")
}

func (r *RPC) parseIOVM1GetstateComplete(br *bytes.Reader) error {
	panic("TODO")
}

func (r *RPC) IOVM1OnPrgEnd(cb IOVM1OnPrgEnd)             { r.iovm1OnPrgEnd = cb }
func (r *RPC) IOVM1OnReadStart(cb IOVM1OnReadStart)       { r.iovm1OnReadStart = cb }
func (r *RPC) IOVM1OnReadFrame(cb IOVM1OnReadFrame)       { r.iovm1OnReadFrame = cb }
func (r *RPC) IOVM1OnReadEnd(cb IOVM1OnReadEnd)           { r.iovm1OnReadEnd = cb }
func (r *RPC) IOVM1OnWriteStart(cb IOVM1OnWriteStart)     { r.iovm1OnWriteStart = cb }
func (r *RPC) IOVM1OnWriteEnd(cb IOVM1OnWriteEnd)         { r.iovm1OnWriteEnd = cb }
func (r *RPC) IOVM1OnWaitComplete(cb IOVM1OnWaitComplete) { r.iovm1OnWaitComplete = cb }

func (r *RPC) parseIOVM1OnEnd(br *bytes.Reader) error {
	panic("TODO")
}

func (r *RPC) parseIOVM1OnRead(br *bytes.Reader) error {
	panic("TODO")
}

func (r *RPC) parseIOVM1OnWrite(br *bytes.Reader) error {
	panic("TODO")
}

func (r *RPC) parseIOVM1OnWait(br *bytes.Reader) error {
	panic("TODO")
}
