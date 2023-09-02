package rex

import (
	"bytes"
	"net"
	"testrex/rex/frame"
	"time"
)

type rexCommand = uint8
type rexNotification = uint8
type rexResult = uint8

const (
	rex_success rexResult = iota
	rex_msg_too_short
	rex_cmd_unknown
	rex_cmd_error
)

type RPC struct {
	c  *net.TCPConn
	fw *frame.Writer
	fr *frame.Reader

	msg [2][]byte

	// RPC complete callbacks:

	iovm1UploadComplete   IOVM1UploadComplete
	iovm1StartComplete    IOVM1StartComplete
	iovm1StopComplete     IOVM1StopComplete
	iovm1SetFlagsComplete IOVM1SetFlagsComplete
	iovm1ResetComplete    IOVM1ResetComplete
	iovm1GetStateComplete IOVM1GetStateComplete

	ppuxCmdUploadComplete   PPUXCmdUploadComplete
	ppuxVRAMUploadComplete  PPUXVRAMUploadComplete
	ppuxCGRAMUploadComplete PPUXCGRAMUploadComplete

	// notification callbacks:

	iovm1OnPrgEnd       IOVM1OnPrgEnd
	iovm1OnReadStart    IOVM1OnReadStart
	iovm1OnReadFrame    IOVM1OnReadFrame
	iovm1OnReadEnd      IOVM1OnReadEnd
	iovm1OnWriteStart   IOVM1OnWriteStart
	iovm1OnWriteEnd     IOVM1OnWriteEnd
	iovm1OnWaitComplete IOVM1OnWaitComplete

	responseHandler [2]map[uint8]func(br *bytes.Reader) error
}

func NewRPC(c *net.TCPConn) *RPC {
	r := &RPC{
		c:  c,
		fw: frame.NewWriter(c, 0),
		fr: frame.NewReader(c),
	}
	r.responseHandler = [2]map[uint8]func(br *bytes.Reader) error{
		// channel 0: completion responses:
		{
			rex_cmd_iovm_upload:   r.parseIOVM1UploadComplete,
			rex_cmd_iovm_start:    r.parseIOVM1StartComplete,
			rex_cmd_iovm_stop:     r.parseIOVM1StopComplete,
			rex_cmd_iovm_reset:    r.parseIOVM1ResetComplete,
			rex_cmd_iovm_flags:    r.parseIOVM1FlagsComplete,
			rex_cmd_iovm_getstate: r.parseIOVM1GetstateComplete,

			rex_cmd_ppux_cmd_upload:   r.parsePPUXCmdUploadComplete,
			rex_cmd_ppux_vram_upload:  r.parsePPUXVRAMUploadComplete,
			rex_cmd_ppux_cgram_upload: r.parsePPUXCGRAMUploadComplete,
		},
		// channel 1: notifications:
		{
			rex_notify_iovm_end:   r.parseIOVM1OnEnd,
			rex_notify_iovm_read:  r.parseIOVM1OnRead,
			rex_notify_iovm_write: r.parseIOVM1OnWrite,
			rex_notify_iovm_wait:  r.parseIOVM1OnWait,
		},
	}
	return r
}

func (r *RPC) Receive(deadline time.Time) (err error) {
	err = r.c.SetReadDeadline(deadline)
	if err != nil {
		return
	}

	var ok bool

	f := frame.F{}
	ok, err = r.fr.ReadInto(&f)
	if err != nil {
		return
	}
	if !ok {
		return
	}

	channel := f.Channel()
	data := f.Data()

	r.msg[channel] = append(r.msg[channel], data...)
	if f.IsFinal() {
		err = r.handleResponse(channel)

		// reset for next message:
		r.msg[channel] = nil

		if err != nil {
			return
		}
	}

	return
}

func (r *RPC) handleResponse(channel uint8) (err error) {
	if channel > 1 {
		panic("channel must be 0 or 1")
	}

	br := bytes.NewReader(r.msg[channel])

	var b byte
	b, err = br.ReadByte()
	if err != nil {
		return
	}

	if fn, ok := r.responseHandler[channel][b]; ok {
		err = fn(br)
		return
	}

	return
}
