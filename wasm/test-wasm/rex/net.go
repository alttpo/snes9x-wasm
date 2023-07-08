package rex

import (
	"fmt"
	"unsafe"
)

type Socket struct {
	Slot    int32
	events  uint32
	revents uint32
}

func (s *Socket) IsReadAvailable() bool { return s.revents&0x0001 != 0 }
func (s *Socket) IsWritable() bool      { return s.revents&0x0004 != 0 }
func (s *Socket) IsErrored() bool       { return s.revents&0x0008 != 0 }
func (s *Socket) IsClosed() bool        { return s.revents&0x0010 != 0 }
func (s *Socket) IsEventsInvalid() bool { return s.revents&0x0020 != 0 }

type AddrV4 struct {
	Addr uint32 // host byte order
	Port uint16 // host byte order
}

//go:wasm-module rex
//export net_tcp_socket
//go:wasmimport rex net_tcp_socket
func net_tcp_socket() int32

//go:wasm-module rex
//export net_udp_socket
//go:wasmimport rex net_udp_socket
func net_udp_socket() int32

//go:wasm-module rex
//export net_bind
//go:wasmimport rex net_bind
func net_bind(slot int32, ipv4_addr uint32, port uint32) int32

//go:wasm-module rex
//export net_connect
//go:wasmimport rex net_connect
func net_connect(slot int32, ipv4_addr uint32, port uint32) int32

//go:wasm-module rex
//export net_listen
//go:wasmimport rex net_listen
func net_listen(slot int32) int32

//go:wasm-module rex
//export net_accept
//go:wasmimport rex net_accept
func net_accept(slot int32, o_accepted_slot unsafe.Pointer, o_ipv4_addr unsafe.Pointer, o_port unsafe.Pointer) int32

//go:wasm-module rex
//export net_poll
//go:wasmimport rex net_poll
func net_poll(poll_slots unsafe.Pointer, poll_slots_len uint32) int32

//go:wasm-module rex
//export net_send
//go:wasmimport rex net_send
func net_send(slot int32, b unsafe.Pointer, l uint32) int32

//go:wasm-module rex
//export net_sendto
//go:wasmimport rex net_sendto
func net_sendto(slot int32, b unsafe.Pointer, l uint32, ipv4_addr uint32, port uint32) int32

//go:wasm-module rex
//export net_recv
//go:wasmimport rex net_recv
func net_recv(slot int32, b unsafe.Pointer, l uint32) int32

//go:wasm-module rex
//export net_recvfrom
//go:wasmimport rex net_recvfrom
func net_recvfrom(slot int32, b unsafe.Pointer, l uint32, o_ipv4_addr unsafe.Pointer, o_port unsafe.Pointer) int32

//go:wasm-module rex
//export net_close
//go:wasmimport rex net_close
func net_close(slot int32) int32

func NetPoll(sockets []Socket) (n int, err error) {
	for i := range sockets {
		sockets[i].events = 0x0001 // POLLIN
		sockets[i].revents = 0
	}

	res := net_poll(unsafe.Pointer(&sockets[0]), uint32(len(sockets)))
	if res < 0 {
		return 0, fmt.Errorf("error polling sockets: %d", -res)
	}

	return
}

func TCPSocket(s *Socket) (err error) {
	if s == nil {
		return fmt.Errorf("s cannot be nil")
	}
	res := net_tcp_socket()
	if res < 0 {
		return fmt.Errorf("error creating tcp socket: %d", -res)
	}
	s.Slot = res
	return
}

func UDPSocket(s *Socket) (err error) {
	if s == nil {
		return fmt.Errorf("s cannot be nil")
	}
	res := net_udp_socket()
	if res < 0 {
		return fmt.Errorf("error creating udp socket: %d", -res)
	}
	s.Slot = res
	return
}

func (s *Socket) Listen() (err error) {
	res := net_listen(s.Slot)
	if res < 0 {
		return fmt.Errorf("error listening on socket: %d", -res)
	}
	return
}

func (s *Socket) Accept(a *Socket) (addr AddrV4, err error) {
	if a == nil {
		err = fmt.Errorf("a cannot be nil")
		return
	}
	res := net_accept(s.Slot, unsafe.Pointer(&a.Slot), unsafe.Pointer(&addr.Addr), unsafe.Pointer(&addr.Port))
	if res < 0 {
		err = fmt.Errorf("error accepting socket: %d", -res)
		return
	}
	return
}

func (s *Socket) Bind(addr AddrV4) (err error) {
	res := net_bind(s.Slot, addr.Addr, uint32(addr.Port))
	if res < 0 {
		return fmt.Errorf("error binding socket: %d", -res)
	}
	return
}

func (s *Socket) Connect(addr AddrV4) (err error) {
	res := net_connect(s.Slot, addr.Addr, uint32(addr.Port))
	if res < 0 {
		return fmt.Errorf("error connecting udp socket: %d", -res)
	}
	return
}

func (s *Socket) Read(b []byte) (n int, err error) {
	res := net_recv(s.Slot, unsafe.Pointer(&b[0]), uint32(len(b)))
	if res < 0 {
		return 0, fmt.Errorf("error reading socket: %d", -res)
	}
	return int(res), nil
}

func (s *Socket) ReadFrom(b []byte) (n int, addr AddrV4, err error) {
	res := net_recvfrom(s.Slot, unsafe.Pointer(&b[0]), uint32(len(b)), unsafe.Pointer(&addr.Addr), unsafe.Pointer(&addr.Port))
	if res < 0 {
		return 0, AddrV4{}, fmt.Errorf("error reading socket: %d", -res)
	}
	return int(res), AddrV4{}, nil
}

func (s *Socket) Write(b []byte) (n int, err error) {
	res := net_send(s.Slot, unsafe.Pointer(&b[0]), uint32(len(b)))
	if res < 0 {
		return 0, fmt.Errorf("error writing socket: %d", -res)
	}
	return int(res), nil
}

func (s *Socket) WriteTo(b []byte, addr AddrV4) (n int, err error) {
	res := net_sendto(s.Slot, unsafe.Pointer(&b[0]), uint32(len(b)), addr.Addr, uint32(addr.Port))
	if res < 0 {
		return 0, fmt.Errorf("error reading socket: %d", -res)
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
