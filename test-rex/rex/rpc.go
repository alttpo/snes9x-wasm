package rex

import (
	"bytes"
	"errors"
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

var Errors = map[rexResult]error{
	rex_success:       nil,
	rex_msg_too_short: errors.New("rex message too short"),
	rex_cmd_unknown:   errors.New("rex command unknown"),
	rex_cmd_error:     errors.New("rex command error"),
}

func pop[T any](queue *[]T) (item T, ok bool) {
	if queue == nil {
		ok = false
		return
	}
	if len(*queue) == 0 {
		ok = false
		return
	}

	item = (*queue)[0]
	*queue = (*queue)[1:]

	return item, true
}

type channelState struct {
	hasType bool
	msgType uint8
	buf     bytes.Buffer
	isFinal bool
	state   int

	frameHandler   map[uint8]func(ch *channelState) error
	messageHandler map[uint8]func(ch *channelState) error
}

type RPC struct {
	c  *net.TCPConn
	fw *frame.Writer
	fr *frame.Reader

	ch [2]channelState

	// RPC complete callbacks:

	iovm1UploadComplete   []IOVM1UploadComplete
	iovm1StartComplete    []IOVM1StartComplete
	iovm1StopComplete     []IOVM1StopComplete
	iovm1SetFlagsComplete []IOVM1SetFlagsComplete
	iovm1ResetComplete    []IOVM1ResetComplete
	iovm1GetStateComplete []IOVM1GetStateComplete

	ppuxCmdUploadComplete   []PPUXCmdUploadComplete
	ppuxVRAMUploadComplete  []PPUXVRAMUploadComplete
	ppuxCGRAMUploadComplete []PPUXCGRAMUploadComplete

	// notification callbacks:

	iovm1OnPrgEnd       IOVM1OnPrgEnd
	iovm1OnReadStart    IOVM1OnReadStart
	iovm1OnReadFrame    IOVM1OnReadFrame
	iovm1OnReadEnd      IOVM1OnReadEnd
	iovm1OnWriteStart   IOVM1OnWriteStart
	iovm1OnWriteEnd     IOVM1OnWriteEnd
	iovm1OnWaitComplete IOVM1OnWaitComplete
}

func NewRPC(c *net.TCPConn) *RPC {
	r := &RPC{
		c:  c,
		fw: frame.NewWriter(c, 0),
		fr: frame.NewReader(c),
	}
	r.ch[0].messageHandler = map[uint8]func(*channelState) error{
		// channel 0: completion responses:
		rex_cmd_iovm_upload:   r.parseIOVM1UploadComplete,
		rex_cmd_iovm_start:    r.parseIOVM1StartComplete,
		rex_cmd_iovm_stop:     r.parseIOVM1StopComplete,
		rex_cmd_iovm_reset:    r.parseIOVM1ResetComplete,
		rex_cmd_iovm_flags:    r.parseIOVM1FlagsComplete,
		rex_cmd_iovm_getstate: r.parseIOVM1GetstateComplete,

		rex_cmd_ppux_cmd_upload:   r.parsePPUXCmdUploadComplete,
		rex_cmd_ppux_vram_upload:  r.parsePPUXVRAMUploadComplete,
		rex_cmd_ppux_cgram_upload: r.parsePPUXCGRAMUploadComplete,
	}
	r.ch[1].messageHandler = map[uint8]func(*channelState) error{
		// channel 1: notifications:
		rex_notify_iovm_end:   r.parseIOVM1OnEnd,
		rex_notify_iovm_write: r.parseIOVM1OnWrite,
		rex_notify_iovm_wait:  r.parseIOVM1OnWait,
	}
	r.ch[1].frameHandler = map[uint8]func(*channelState) error{
		rex_notify_iovm_read: r.frameIOVM1OnRead,
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

	ch := f.Channel()
	r.ch[ch].isFinal = f.IsFinal()
	data := f.Data()

	// extract the first byte as the message type:
	if !r.ch[ch].hasType {
		if len(data) >= 1 {
			r.ch[ch].msgType = data[0]
			r.ch[ch].hasType = true
			data = data[1:]
		}
	}

	// append to the message buffer:
	r.ch[ch].buf.Write(data)
	if mt, ok := r.ch[ch].msgType, r.ch[ch].hasType; ok {
		// check if there's a frame handler:
		if fh, ok := r.ch[ch].frameHandler[mt]; ok {
			if err = fh(&r.ch[ch]); err != nil {
				return err
			}
		}
	}

	// handle the complete message:
	if f.IsFinal() {
		err = r.ch[ch].handleResponse()

		// reset for next message:
		r.ch[ch].buf.Reset()
		r.ch[ch].state = 0
		r.ch[ch].msgType = 0
		r.ch[ch].hasType = false
		r.ch[ch].isFinal = false

		if err != nil {
			return
		}
	}

	return
}

func (ch *channelState) handleResponse() (err error) {
	if !ch.hasType {
		// completely empty messages are ignored:
		return
	}

	// check for a message handler:
	if fn, ok := ch.messageHandler[ch.msgType]; ok {
		err = fn(ch)
		return
	}

	return
}
