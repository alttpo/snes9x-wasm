package iovm1

import "errors"

type Flags = uint8

const (
	FlagNotifyWriteStart Flags = 1 << iota
	FlagNotifyWriteByte
	FlagNotifyWriteEnd
	FlagNotifyWaitComplete
	_ // 4
	_ // 5
	FlagAutoRestartOnError
	FlagAutoRestartOnEnd
)

type Result = byte

const (
	Success Result = iota

	OutOfRange
	InvalidOperationForState
	UnknownOpcode
	TimedOut
	MemoryTargetUndefined
	MemoryTargetNotReadable
	MemoryTargetNotWritable
	MemoryTargetAddressOutOfRange
)

var Errors = map[Result]error{
	Success:                       nil,
	OutOfRange:                    errors.New("out of range"),
	InvalidOperationForState:      errors.New("invalid operation for current state"),
	UnknownOpcode:                 errors.New("unknown opcode"),
	TimedOut:                      errors.New("timed out"),
	MemoryTargetUndefined:         errors.New("memory target undefined"),
	MemoryTargetNotReadable:       errors.New("memory target not readable"),
	MemoryTargetNotWritable:       errors.New("memory target not writable"),
	MemoryTargetAddressOutOfRange: errors.New("memory target address out of range"),
}

type State = int32

const (
	STATE_INIT State = iota
	STATE_LOADED
	STATE_RESET
	STATE_EXECUTE_NEXT
	STATE_INVOKE_CALLBACK
	STATE_ENDED
)

type Opcode = byte

const (
	OPCODE_END Opcode = iota
	OPCODE_SETA8
	OPCODE_SETA16
	OPCODE_SETA24
	OPCODE_SETTDU
	OPCODE_SETLEN
	OPCODE_SETCMPMSK
	OPCODE_SETTIM
	OPCODE_READ
	OPCODE_WRITE
	OPCODE_WAIT_WHILE_NEQ
	OPCODE_WAIT_WHILE_EQ
	OPCODE_WAIT_WHILE_LT
	OPCODE_WAIT_WHILE_GT
	OPCODE_WAIT_WHILE_LTE
	OPCODE_WAIT_WHILE_GTE
)

type Target = byte

const (
	TARGETFLAG_REVERSE     = 0x40
	TARGETFLAG_UPDATE_ADDR = 0x80
)

const (
	TARGET_WRAM Target = iota
	TARGET_SRAM
	TARGET_ROM
	TARGET_2C00
	TARGET_VRAM
	TARGET_CGRAM
	TARGET_OAM
)

type Register byte

func Instruction(opcode Opcode, ch uint8) uint8 {
	return (uint8(opcode) & 15) | ((ch & 3) << 4)
}
