package ppux

import (
	"rex/rexrpc"
)

const (
	CmdCmdUpload rexrpc.CommandType = 16 + iota
	CmdVramUpload
	CmdCgramUpload
)

const (
	RspCmdUpload rexrpc.ResponseType = 0x80 + 16 + iota
	RspVramUpload
	RspCgramUpload
)

type RPC interface {
	CmdUpload(p []uint32) error
	VRAMUpload(addr uint32, data []byte) error
	CGRAMUpload(addr uint32, data []byte) error
}
