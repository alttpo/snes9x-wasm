package rex

import (
	"errors"
	"unsafe"
)

//go:wasmimport rex iovm1_init
func iovm1_init(n uint32) int32

//go:wasmimport rex iovm1_load
func iovm1_load(n uint32, vmprog unsafe.Pointer, vmprog_len uint32) int32

//go:wasmimport rex iovm1_get_exec_state
func iovm1_get_exec_state(n uint32) int32

//go:wasmimport rex iovm1_exec_reset
func iovm1_exec_reset(n uint32) int32

//go:wasmimport rex iovm1_read_data
func iovm1_read_data(
	n uint32,
	dst unsafe.Pointer,
	dst_len uint32,
	o_read unsafe.Pointer,
	o_addr unsafe.Pointer,
	o_target unsafe.Pointer,
) int32

type iovm1 struct {
	n uint32
}

var IOVM1 = [2]iovm1{
	{0},
	{1},
}

type IOVM1Result = int32

const (
	IOVM1_SUCCESS IOVM1Result = iota

	IOVM1_ERROR_VM_INVALID_OPERATION_FOR_STATE
	IOVM1_ERROR_VM_UNKNOWN_OPCODE
	IOVM1_ERROR_VM_INVALID_MEMORY_ACCESS
)

const (
	IOVM1_ERROR_OUT_OF_RANGE = 128 + iota
	IOVM1_ERROR_NO_DATA
	IOVM1_ERROR_BUFFER_TOO_SMALL
)

var iovm1Errors = map[IOVM1Result]error{
	IOVM1_SUCCESS: nil,
	IOVM1_ERROR_VM_INVALID_OPERATION_FOR_STATE: errors.New("invalid operation for current state"),
	IOVM1_ERROR_VM_UNKNOWN_OPCODE:              errors.New("unknown opcode"),
	IOVM1_ERROR_VM_INVALID_MEMORY_ACCESS:       errors.New("invalid memory access"),
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
	IOVM1_OPCODE_SETTV
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

// Init stops any executing program, erases it, and resets to initial state
func (v *iovm1) Init() (err error) {
	res := iovm1_init(v.n)
	if res != IOVM1_SUCCESS {
		err = iovm1Errors[res]
	}
	return
}

// Load a given program and start executing it immediately on next emulation step
func (v *iovm1) Load(vmprog []byte) (err error) {
	res := iovm1_load(v.n, unsafe.Pointer(&vmprog[0]), uint32(len(vmprog)))
	if res != IOVM1_SUCCESS {
		err = iovm1Errors[res]
	}
	return
}

// ExecState gets the current state of the VM
func (v *iovm1) ExecState() IOVM1State {
	return iovm1_get_exec_state(v.n)
}

// Reset resets the executing program to the beginning
func (v *iovm1) Reset() (err error) {
	res := iovm1_exec_reset(v.n)
	if res != IOVM1_SUCCESS {
		err = iovm1Errors[res]
	}
	return
}

// Read reads data from the queue fed by `read` amd `read_n` instructions executed
func (v *iovm1) Read(p []byte) (n uint32, addr uint32, target uint8, err error) {
	res := iovm1_read_data(
		v.n,
		unsafe.Pointer(&p[0]),
		uint32(len(p)),
		unsafe.Pointer(&n),
		unsafe.Pointer(&addr),
		unsafe.Pointer(&target),
	)
	if res != IOVM1_SUCCESS {
		err = iovm1Errors[res]
	}
	return
}
