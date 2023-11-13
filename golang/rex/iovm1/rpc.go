package iovm1

import (
	"bytes"
	"context"
	"fmt"
	"rex/frame"
	"rex/rexrpc"
)

const (
	CmdUpload rexrpc.CommandType = iota
	CmdStart
	CmdStop
	CmdReset
	CmdSetFlags
	CmdGetState
)

const (
	RspUpload rexrpc.ResponseType = iota
	RspStart
	RspStop
	RspReset
	RspSetFlags
	RspGetState
)
const (
	NotifyEnd rexrpc.ResponseType = 0x80 + iota
	NotifyRead
	NotifyWrite
	NotifyWait
)

type RPC struct {
	fw func(func(fw *frame.Writer) error) error

	// response-specific completion channels:

	uploadCh chan UploadComplete
	startCh  chan StartComplete

	// notification-specific channels:

	readCh chan ReadChunk
}

func NewRPC(u *rexrpc.RPC) *RPC {
	r := &RPC{
		fw:       u.FrameWriter(),
		uploadCh: make(chan UploadComplete),
		readCh:   make(chan ReadChunk, 4),
	}

	u.RegisterResponseFrameHandlerFactory(
		RspUpload,
		func(c chan<- rexrpc.Response) rexrpc.FrameHandler {
			return &UploadCompleteFrameHandler{r: r}
		},
	)
	u.RegisterNotificationFrameHandlerFactory(
		NotifyRead,
		func() rexrpc.FrameHandler {
			return &NotifyReadFrameHandler{r: r}
		},
	)

	return r
}

// RPC completion responses:

type UploadCompleteFrameHandler struct {
	r   *RPC
	buf bytes.Buffer
}

type UploadComplete struct {
	RexResult rexrpc.Result
	VMErr     Result
}

func (_ UploadComplete) ResponseType() rexrpc.ResponseType { return RspUpload }

type StartComplete struct {
	RexResult rexrpc.Result
	VMErr     Result
}

func (_ StartComplete) ResponseType() rexrpc.ResponseType { return RspStart }

type StopComplete struct {
	RexResult rexrpc.Result
	VMErr     Result
}

func (_ StopComplete) ResponseType() rexrpc.ResponseType { return RspStop }

type SetFlagsComplete struct {
	RexResult rexrpc.Result
	VMErr     Result
}

func (_ SetFlagsComplete) ResponseType() rexrpc.ResponseType { return RspSetFlags }

type ResetComplete struct {
	RexResult rexrpc.Result
	VMErr     Result
}

func (_ ResetComplete) ResponseType() rexrpc.ResponseType { return RspReset }

type GetStateComplete struct {
	RexResult rexrpc.Result
	VMErr     Result
	State     State
}

func (_ GetStateComplete) ResponseType() rexrpc.ResponseType { return RspGetState }

// Notifications:

type NotifyReadFrameHandler struct {
	r *RPC

	buf   bytes.Buffer
	state int

	readChunk ReadChunk
}

func (m *NotifyReadFrameHandler) HandleFrame(data []byte, isFinal bool) (err error) {
	m.buf.Write(data)

	switch m.state {
	case 0:
		// wait until we have enough header data to consume:
		if m.buf.Len() < 10 {
			return
		}
		m.state++
		fallthrough
	case 1:
		m.readChunk.PC = rexrpc.LittleEndian.Uint32(m.buf.Next(4))
		m.readChunk.TDU = rexrpc.LittleEndian.Uint8(m.buf.Next(1))
		m.readChunk.Addr = rexrpc.LittleEndian.Uint24(m.buf.Next(3))
		m.readChunk.ChunkOffs = 0
		m.readChunk.ChunkLen = 0
		dlen := uint32(rexrpc.LittleEndian.Uint16(m.buf.Next(2)))
		if dlen == 0 {
			dlen = 65536
		}
		m.readChunk.Len = dlen

		// read started:
		m.state++
		fallthrough
	case 2:
		// read raw data:
		if m.buf.Len() > 0 {
			// read chunk:
			m.readChunk.ChunkLen = uint32(m.buf.Len())
			copy(m.readChunk.Chunk[:], m.buf.Next(63))
			m.r.readCh <- m.readChunk

			// don't keep accumulating message data:
			m.buf.Reset()
			// increment the offset for the next chunk read notification:
			m.readChunk.ChunkOffs += m.readChunk.ChunkLen
		}
		if isFinal {
			// read ended:
			m.readChunk.IsFinal = true
			m.r.readCh <- m.readChunk

			// reset for the next notification:
			m.buf.Reset()
			m.state = 0
		}
		break
	default:
		panic("invalid state")
	}

	return
}

func (r *RPC) Upload(ctx context.Context, vmprog []byte) (err error) {
	// lock on the frame.Writer:
	if err = r.fw(func(fw *frame.Writer) (err error) {
		_, err = fw.Write([]byte{byte(CmdUpload)})
		if err != nil {
			return
		}
		_, err = fw.Write(vmprog)
		if err != nil {
			return
		}
		err = fw.Finalize()
		if err != nil {
			return
		}
		return
	}); err != nil {
		return
	}

	// wait for response:
	select {
	case rsp := <-r.uploadCh:
		err = rexrpc.Errors[rsp.RexResult]
		if err == nil {
			err = Errors[rsp.VMErr]
		}
		return
	case <-ctx.Done():
		return ctx.Err()
	}
}

func (m *UploadCompleteFrameHandler) HandleFrame(data []byte, isFinal bool) (err error) {
	m.buf.Write(data)

	if m.buf.Len() != 2 {
		err = fmt.Errorf("UploadCompleteFrameHandler: expected message size of 2 bytes")
		return
	}

	defer m.buf.Reset()

	// parse the message:
	var c UploadComplete
	if c.RexResult, err = m.buf.ReadByte(); err != nil {
		return
	}
	if c.VMErr, err = m.buf.ReadByte(); err != nil {
		return
	}

	// require isFinal
	if !isFinal {
		err = fmt.Errorf("required final frame after message completion")
		return
	}

	// send the response:
	m.r.uploadCh <- c
	return
}

func (r *RPC) Start(ctx context.Context) (err error) {
	// lock on the frame.Writer:
	if err = r.fw(func(fw *frame.Writer) (err error) {
		_, err = fw.Write([]byte{byte(CmdStart)})
		if err != nil {
			//cb(err)
			return
		}
		err = fw.Finalize()
		if err != nil {
			//cb(err)
			return
		}
		return
	}); err != nil {
		return
	}

	// wait for response:
	select {
	case rsp := <-r.startCh:
		err = rexrpc.Errors[rsp.RexResult]
		if err == nil {
			err = Errors[rsp.VMErr]
		}
		return
	case <-ctx.Done():
		return ctx.Err()
	}
}

func (r *RPC) Stop(ctx context.Context) (err error) {
	// lock on the frame.Writer:
	if err = r.fw(func(fw *frame.Writer) (err error) {
		_, err = fw.Write([]byte{byte(CmdStop)})
		if err != nil {
			//cb(err)
			return
		}
		err = fw.Finalize()
		if err != nil {
			//cb(err)
			return
		}
		return
	}); err != nil {
		return
	}

	return
}

func (r *RPC) Reset(ctx context.Context) (err error) {
	// lock on the frame.Writer:
	if err = r.fw(func(fw *frame.Writer) (err error) {
		_, err = fw.Write([]byte{byte(CmdReset)})
		if err != nil {
			//cb(err)
			return
		}
		err = fw.Finalize()
		if err != nil {
			//cb(err)
			return
		}
		return
	}); err != nil {
		return
	}

	return
}

func (r *RPC) SetFlags(ctx context.Context, flags Flags) (err error) {
	// lock on the frame.Writer:
	if err = r.fw(func(fw *frame.Writer) (err error) {
		_, err = fw.Write([]byte{byte(CmdSetFlags), flags})
		if err != nil {
			//cb(err)
			return
		}

		err = fw.Finalize()
		if err != nil {
			//cb(err)
			return
		}
		return
	}); err != nil {
		return
	}

	return
}

func (r *RPC) GetState(ctx context.Context) (err error) {
	// lock on the frame.Writer:
	if err = r.fw(func(fw *frame.Writer) (err error) {
		_, err = fw.Write([]byte{byte(CmdGetState)})
		if err != nil {
			//cb(0, err)
			return
		}

		err = fw.Finalize()
		if err != nil {
			//cb(0, err)
			return
		}
		return
	}); err != nil {
		return
	}

	return
}
