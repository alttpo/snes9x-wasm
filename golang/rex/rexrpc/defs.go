package rexrpc

import (
	"errors"
)

type Response interface {
	ResponseType() ResponseType
}

type CommandType uint8
type ResponseType uint8

// Result of RPC command
type Result = uint8

const (
	Success Result = iota
	MsgTooShort
	CmdUnknown
	CmdError
)

var Errors = map[Result]error{
	Success:     nil,
	MsgTooShort: errors.New("rex message too short"),
	CmdUnknown:  errors.New("rex command unknown"),
	CmdError:    errors.New("rex command error"),
}
