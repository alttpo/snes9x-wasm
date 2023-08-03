package rex

import (
	"errors"
)

type iovm1 struct {
	n uint32
}

func (i *iovm1) Execute(vmprog []byte) (err error) {
	//TODO implement me
	panic("implement me")
}

func (i *iovm1) ExecState() IOVM1State {
	//TODO implement me
	panic("implement me")
}

func (i *iovm1) Reset() (err error) {
	//TODO implement me
	panic("implement me")
}

var IOVM1 = [2]iovm1{
	{0},
	{1},
}

type IOVM1Result = int32

const (
	IOVM1_SUCCESS IOVM1Result = iota

	IOVM1_ERROR_INVALID_OPERATION_FOR_STATE
	IOVM1_ERROR_UNKNOWN_OPCODE
	IOVM1_ERROR_INVALID_MEMORY_ACCESS
)

const (
	IOVM1_ERROR_OUT_OF_RANGE = 128 + iota
	IOVM1_ERROR_NO_DATA
	IOVM1_ERROR_BUFFER_TOO_SMALL
)

var iovm1Errors = map[IOVM1Result]error{
	IOVM1_SUCCESS: nil,
	IOVM1_ERROR_INVALID_OPERATION_FOR_STATE: errors.New("invalid operation for current state"),
	IOVM1_ERROR_UNKNOWN_OPCODE:              errors.New("unknown opcode"),
	IOVM1_ERROR_INVALID_MEMORY_ACCESS:       errors.New("invalid memory access"),
	IOVM1_ERROR_OUT_OF_RANGE:                   errors.New("out of range"),
	IOVM1_ERROR_NO_DATA:                        errors.New("no data"),
	IOVM1_ERROR_BUFFER_TOO_SMALL:               errors.New("buffer too small"),
}

type IOVM1State = int32

const (
	IOVM1_STATE_INIT IOVM1State = iota
	IOVM1_STATE_LOADED
	IOVM1_STATE_RESET
	IOVM1_STATE_EXECUTE_NEXT
	IOVM1_STATE_INVOKE_CALLBACK
	IOVM1_STATE_ENDED
)

type IOVM1Opcode = byte

const (
	IOVM1_OPCODE_END IOVM1Opcode = iota
	IOVM1_OPCODE_SETA8
	IOVM1_OPCODE_SETA16
	IOVM1_OPCODE_SETA24
	IOVM1_OPCODE_SETTDU
	IOVM1_OPCODE_SETLEN
	IOVM1_OPCODE_SETCMPMSK
	IOVM1_OPCODE_SETTIM
	IOVM1_OPCODE_READ
	IOVM1_OPCODE_WRITE
	IOVM1_OPCODE_WAIT_WHILE_NEQ
	IOVM1_OPCODE_WAIT_WHILE_EQ
	IOVM1_OPCODE_WAIT_WHILE_LT
	IOVM1_OPCODE_WAIT_WHILE_GT
	IOVM1_OPCODE_WAIT_WHILE_LTE
	IOVM1_OPCODE_WAIT_WHILE_GTE
)

type IOVM1Target = byte

const (
	IOVM1_TARGETFLAG_REVERSE     = 0x40
	IOVM1_TARGETFLAG_UPDATE_ADDR = 0x80
)

const (
	IOVM1_TARGET_WRAM IOVM1Target = iota
	IOVM1_TARGET_SRAM
	IOVM1_TARGET_ROM
	IOVM1_TARGET_2C00
	IOVM1_TARGET_VRAM
	IOVM1_TARGET_CGRAM
	IOVM1_TARGET_OAM
)

type IOVM1Register byte

func IOVM1Instruction(opcode IOVM1Opcode, ch uint8) uint8 {
	return (uint8(opcode) & 15) | ((ch & 3) << 4)
}

type IOVM interface {
	Execute(vmprog []byte) (err error)
	ExecState() IOVM1State
	Reset() (err error)
}
