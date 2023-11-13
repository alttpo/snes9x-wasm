package iovm1

type PrgEnd struct {
	PC     uint32
	O      Opcode
	Result Result
	State  State
}

type ReadChunk struct {
	PC   uint32
	TDU  uint8
	Addr uint32
	Len  uint32

	ChunkOffs uint32
	ChunkLen  uint32
	Chunk     [63]byte

	IsFinal bool
}

type WriteStart struct {
	PC   uint32
	TDU  uint8
	Addr uint32
	Len  uint32
}

type WriteEnd struct {
	PC   uint32
	TDU  uint8
	Addr uint32
	Len  uint32
}

type WaitComplete struct {
	PC     uint32
	O      Opcode
	Result Result
	State  State
}
