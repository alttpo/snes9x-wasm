package rex

import (
	"errors"
)

type MemoryTarget struct {
	t IOVM1Target
}

// WRAM provides linear read access to emulated snes ROM
var WRAM = MemoryTarget{IOVM1_TARGET_WRAM}

// SRAM provides linear read/write access to emulated snes SRAM
var SRAM = MemoryTarget{IOVM1_TARGET_SRAM}

// ROM provides linear read/write access to emulated snes ROM
var ROM = MemoryTarget{IOVM1_TARGET_ROM}

// NMI2C00 provides linear read/write access to emulated snes memory mapped to $2C00
var NMI2C00 = MemoryTarget{IOVM1_TARGET_2C00}

// VRAM provides linear read access to emulated snes VRAM
var VRAM = MemoryTarget{IOVM1_TARGET_VRAM}

// CGRAM provides linear read access to emulated snes CGRAM
var CGRAM = MemoryTarget{IOVM1_TARGET_CGRAM}

// OAM provides linear read access to emulated snes OAM
var OAM = MemoryTarget{IOVM1_TARGET_OAM}

var errBufferTooLarge = errors.New("buffer too large")

func (m *MemoryTarget) BeginRead(p []byte, addr uint32) (err error) {
	n := len(p)
	if n == 0 {
		return nil
	}
	if n > 65536 {
		return errBufferTooLarge
	}
	if n == 65536 {
		// encode 65536 bytes as 0:
		n = 0
	}

	prog := [...]byte{
		// set target:
		IOVM1Instruction(IOVM1_OPCODE_SETTDU, 0),
		m.t,
		// set address:
		IOVM1Instruction(IOVM1_OPCODE_SETA24, 0),
		byte(addr & 0xFF),
		byte((addr >> 8) & 0xFF),
		byte((addr >> 16) & 0xFF),
		// set transfer length:
		IOVM1Instruction(IOVM1_OPCODE_SETLEN, 0),
		byte(n & 0xFF),
		byte((n >> 8) & 0xFF),
		// read chunk:
		IOVM1Instruction(IOVM1_OPCODE_READ, 0),
		IOVM1Instruction(IOVM1_OPCODE_END, 0),
	}
	if err = IOVM1[1].Execute(prog[:]); err != nil {
		return
	}

	return nil
}

func (m *MemoryTarget) BeginWrite(p []byte, addr uint32) (err error) {
	n := len(p)
	if n == 0 {
		return nil
	}
	if n > 65536 {
		return errBufferTooLarge
	}
	if n == 65536 {
		// encode 65536 bytes as 0:
		n = 0
	}

	prog := make([]byte, 0, 11+n)
	prog = append(
		prog,
		// set target:
		IOVM1Instruction(IOVM1_OPCODE_SETTDU, 0),
		m.t,
		// set address:
		IOVM1Instruction(IOVM1_OPCODE_SETA24, 0),
		byte(addr&0xFF),
		byte((addr>>8)&0xFF),
		byte((addr>>16)&0xFF),
		// set transfer length:
		IOVM1Instruction(IOVM1_OPCODE_SETLEN, 0),
		byte(n&0xFF),
		byte((n>>8)&0xFF),
		// read chunk:
		IOVM1Instruction(IOVM1_OPCODE_WRITE, 0),
	)
	prog = append(prog, p...)
	prog = append(prog,
		IOVM1Instruction(IOVM1_OPCODE_END, 0),
	)
	if err = IOVM1[1].Execute(prog); err != nil {
		return
	}

	return nil
}

func (m *MemoryTarget) Target() IOVM1Target {
	return m.t
}
