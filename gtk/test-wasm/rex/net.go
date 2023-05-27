package rex

import (
    "fmt"
)

type Socket struct {
	Slot    int32
	revents uint32
}

func (s *Socket) IsReadAvailable() bool { return s.revents&0x0001 != 0 }
func (s *Socket) IsWritable() bool      { return s.revents&0x0004 != 0 }
func (s *Socket) IsErrored() bool       { return s.revents&0x0008 != 0 }
func (s *Socket) IsClosed() bool        { return s.revents&0x0010 != 0 }
func (s *Socket) IsEventsInvalid() bool { return s.revents&0x0020 != 0 }

type socketPoll struct {
	slot    int32
	events  uint32
	revents uint32
}

//go:wasm-module rex
//export net_tcp_listen
func net_tcp_listen(port uint32) int32

//go:wasm-module rex
//export net_tcp_accept
func net_tcp_accept(slot int32) int32

//go:wasm-module rex
//export net_poll
func net_poll(poll_slots *socketPoll, poll_slots_len uint32) int32

//go:wasm-module rex
//export net_recv
func net_recv(slot int32, b *byte, l uint32) int32

//go:wasm-module rex
//export net_send
func net_send(slot int32, b *byte, l uint32) int32

//go:wasm-module rex
//export net_close
func net_close(slot int32) int32

func NetPoll(sockets []*Socket) (n int, err error) {
	poll_slots := make([]socketPoll, len(sockets))
	for i := range sockets {
		poll_slots[i].slot = sockets[i].Slot
		poll_slots[i].events = 0x0001 // POLLIN
		poll_slots[i].revents = 0
	}

	res := net_poll(&poll_slots[0], uint32(len(poll_slots)))
	if res < 0 {
		return 0, fmt.Errorf("error polling sockets: %d", -res)
	}

	// copy revents back to `*Socket`s:
	n = int(res)
	for i := range sockets {
		if poll_slots[i].slot != sockets[i].Slot {
			err = fmt.Errorf("unexpected socket mismatch when building response")
			return
		}
		sockets[i].revents = poll_slots[i].revents
	}

	return
}

func TCPListen(port uint32) (s *Socket, err error) {
	res := net_tcp_listen(port)
	if res < 0 {
		return nil, fmt.Errorf("error creating listening socket: %d", -res)
	}
	s = &Socket{Slot: res}
	return
}

func (s *Socket) TCPAccept() (a *Socket, err error) {
	res := net_tcp_accept(s.Slot)
	if res < 0 {
		return nil, fmt.Errorf("error accepting socket: %d", -res)
	}
	a = &Socket{Slot: res}
	return
}

func (s *Socket) Read(b []byte) (n int, err error) {
	res := net_recv(s.Slot, &b[0], uint32(len(b)))
	if res < 0 {
		return 0, fmt.Errorf("error reading socket: %d", -res)
	}
	return int(res), nil
}

func (s *Socket) Write(b []byte) (n int, err error) {
	res := net_send(s.Slot, &b[0], uint32(len(b)))
	if res < 0 {
		return 0, fmt.Errorf("error writing socket: %d", -res)
	}
	return int(res), nil
}

func (s *Socket) Close() (err error) {
	res := net_close(s.Slot)
	if res < 0 {
		return fmt.Errorf("error closing socket: %d", -res)
	}
	return nil
}

func (s *Socket) Revents() uint32 {
	return s.revents
}
