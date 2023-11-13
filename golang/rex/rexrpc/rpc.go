package rexrpc

import (
	"fmt"
	"net"
	"rex/frame"
	"sync"
)

type FrameHandler interface {
	HandleFrame(data []byte, isFinal bool) error
}

type ResponseFrameHandlerFactory func(c chan<- Response) FrameHandler

type NotificationFrameHandlerFactory func() FrameHandler

// channelState is for parsing response frames
type channelState struct {
	hasType      bool
	msgType      ResponseType
	frameHandler FrameHandler
}

type RPC struct {
	c *net.TCPConn

	fr *frame.Reader

	fwLock sync.Mutex
	fw     *frame.Writer

	ch [2]channelState

	responseFrameHandlerFactory     map[ResponseType]ResponseFrameHandlerFactory
	notificationFrameHandlerFactory map[ResponseType]NotificationFrameHandlerFactory

	responseChannel chan Response
}

func NewRPC(c *net.TCPConn) *RPC {
	r := &RPC{
		c:  c,
		fw: frame.NewWriter(c, 0),
		fr: frame.NewReader(c),

		responseFrameHandlerFactory:     make(map[ResponseType]ResponseFrameHandlerFactory),
		notificationFrameHandlerFactory: make(map[ResponseType]NotificationFrameHandlerFactory),

		responseChannel: make(chan Response), // command responses
	}
	return r
}

func (r *RPC) Responses() <-chan Response {
	return r.responseChannel
}

func (r *RPC) FrameWriter() func(func(fw *frame.Writer) error) error {
	return func(doWrite func(fw *frame.Writer) error) error {
		// take an exclusive lock on the frame.Writer so multiple goroutines don't
		r.fwLock.Lock()
		defer r.fwLock.Unlock()
		return doWrite(r.fw)
	}
}

func (r *RPC) RegisterResponseFrameHandlerFactory(rsp ResponseType, factory ResponseFrameHandlerFactory) {
	if _, ok := r.responseFrameHandlerFactory[rsp]; ok {
		panic(fmt.Errorf("response frame handler factory already registered"))
	}
	r.responseFrameHandlerFactory[rsp] = factory
}

func (r *RPC) RegisterNotificationFrameHandlerFactory(rsp ResponseType, factory NotificationFrameHandlerFactory) {
	if _, ok := r.notificationFrameHandlerFactory[rsp]; ok {
		panic(fmt.Errorf("notification frame handler factory already registered"))
	}
	r.notificationFrameHandlerFactory[rsp] = factory
}

func (r *RPC) Receive() (err error) {
	var ok bool

	// blocking read:
	f := frame.F{}
	ok, err = r.fr.ReadInto(&f)
	if err != nil {
		return
	}
	if !ok {
		return
	}

	ch := f.Channel()
	data := f.Data()
	final := f.IsFinal()

	chs := &r.ch[ch]

	// extract the first byte as the message type:
	if !chs.hasType {
		if len(data) >= 1 {
			chs.msgType = ResponseType(data[0])
			chs.hasType = true
			data = data[1:]

			// instantiate the response frame handler if registered:
			if ch == 0 {
				if factory, ok := r.responseFrameHandlerFactory[chs.msgType]; ok {
					chs.frameHandler = factory(r.responseChannel)
				}
			} else {
				if factory, ok := r.notificationFrameHandlerFactory[chs.msgType]; ok {
					chs.frameHandler = factory()
				}
			}
		}
	}

	// append to the message buffer:
	if fh := chs.frameHandler; fh != nil {
		// frame handlers should send events to events channel:
		if err = fh.HandleFrame(data, final); err != nil {
			return
		}
	}

	if final {
		// reset for next message:
		chs.msgType = 0
		chs.hasType = false
		chs.frameHandler = nil
	}

	return
}
