package rex

import (
	"errors"
	"unsafe"
)

//go:wasmimport rex iovm1_init
func iovm1_init() int32

//go:wasmimport rex iovm1_load
func iovm1_load(vmprog unsafe.Pointer, vmprog_len uint32) int32

//go:wasmimport rex iovm1_get_exec_state
func iovm1_get_exec_state() int32

//go:wasmimport rex iovm1_exec_reset
func iovm1_exec_reset() int32

//go:wasmimport rex iovm1_read_data
func iovm1_read_data(dst unsafe.Pointer, dst_len uint32, o_read unsafe.Pointer) int32

type iovm1 struct{}

var IOVM1 iovm1

type IOVM1Result = int32

const (
	IOVM1_SUCCESS IOVM1Result = iota
	IOVM1_ERROR_OUT_OF_RANGE
	IOVM1_ERROR_VM_INVALID_OPERATION_FOR_STATE
	IOVM1_ERROR_VM_UNKNOWN_OPCODE
)

var iovm1Errors = [4]error{
	nil,
	errors.New("out of range"),
	errors.New("invalid operation for current state"),
	errors.New("unknown opcode"),
}

type IOVM1State = int32

const (
	IOVM1_STATE_INIT IOVM1State = iota
	IOVM1_STATE_LOADED
	IOVM1_STATE_RESET
	IOVM1_STATE_EXECUTE_NEXT
	IOVM1_STATE_STALLED
	IOVM1_STATE_ENDED
)

type IOVM1Opcode = byte

const (
	IOVM1_OPCODE_END IOVM1Opcode = iota
	IOVM1_OPCODE_SETADDR
	IOVM1_OPCODE_SETOFFS
	IOVM1_OPCODE_SETBANK
	IOVM1_OPCODE_READ
	IOVM1_OPCODE_READ_N
	IOVM1_OPCODE_WRITE
	IOVM1_OPCODE_WRITE_N
	IOVM1_OPCODE_WHILE_NEQ
	IOVM1_OPCODE_WHILE_EQ
)

type IOVM1Target = byte

const (
	IOVM1_TARGET_WRAM IOVM1Target = iota
	IOVM1_TARGET_SRAM
	IOVM1_TARGET_ROM
)

func IOVM1Instruction(opcode IOVM1Opcode, target IOVM1Target) uint8 {
	return (uint8(opcode) & 31) | (uint8(target&7) << 5)
}

// Init stops any executing program, erases it, and resets to initial state
func (v *iovm1) Init() error {
	return iovm1Errors[iovm1_init()]
}

// Load a given program and start executing it immediately on next emulation step
func (v *iovm1) Load(vmprog []byte) error {
	return iovm1Errors[iovm1_load(unsafe.Pointer(&vmprog[0]), uint32(len(vmprog)))]
}

// ExecState gets the current state of the VM
func (v *iovm1) ExecState() IOVM1State {
	return iovm1_get_exec_state()
}

// Reset resets the executing program to the beginning
func (v *iovm1) Reset() error {
	return iovm1Errors[iovm1_exec_reset()]
}

// Read reads data from the queue fed by `read` amd `read_n` instructions executed
func (v *iovm1) Read(p []byte) (n int, err error) {
	res := iovm1_read_data(unsafe.Pointer(&p[0]), uint32(len(p)), unsafe.Pointer(&n))
	if res != IOVM1_SUCCESS {
		err = iovm1Errors[res]
	}
	return
}
