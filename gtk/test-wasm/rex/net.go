package rex

type NetPollSlot struct {
	Slot    int32
	Events  uint32
	Revents uint32
}

//go:wasm-module rex
//export net_tcp_listen
func net_tcp_listen(port uint32) int32

//go:wasm-module rex
//export net_tcp_accept
func net_tcp_accept(slot int32) int32

//go:wasm-module rex
//export net_poll
func net_poll(poll_slots *NetPollSlot, poll_slots_len uint32) int32

//go:wasm-module rex
//export net_recv
func net_recv(slot int32, b *byte, l uint32) int32

//go:wasm-module rex
//export net_send
func net_send(slot int32, b *byte, l uint32) int32

//go:wasm-module rex
//export net_close
func net_close(slot int32) int32

// TODO: use interfaces and methods for convenience

func NetTCPListen(port uint32) int32 {
	return net_tcp_listen(port)
}

func NetTCPAccept(slot int32) int32 {
	return net_tcp_accept(slot)
}

func NetPoll(poll_slots []NetPollSlot) int32 {
	return net_poll(&poll_slots[0], uint32(len(poll_slots)))
}

func NetRecv(slot int32, b []byte) int32 {
	return net_recv(slot, &b[0], uint32(len(b)))
}

func NetSend(slot int32, b []byte) int32 {
	return net_send(slot, &b[0], uint32(len(b)))
}

func NetClose(slot int32) int32 {
	return net_close(slot)
}
