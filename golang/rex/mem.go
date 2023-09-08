package rex

import (
	"errors"
	"github.com/alttpo/snes9x/golang/rex/iovm1"
)

type MemoryTarget struct {
	t iovm1.Target
}

// WRAM provides linear read access to emulated snes ROM
var WRAM = MemoryTarget{iovm1.TARGET_WRAM}

// SRAM provides linear read/write access to emulated snes SRAM
var SRAM = MemoryTarget{iovm1.TARGET_SRAM}

// ROM provides linear read/write access to emulated snes ROM
var ROM = MemoryTarget{iovm1.TARGET_ROM}

// NMI2C00 provides linear read/write access to emulated snes memory mapped to $2C00
var NMI2C00 = MemoryTarget{iovm1.TARGET_2C00}

// VRAM provides linear read access to emulated snes VRAM
var VRAM = MemoryTarget{iovm1.TARGET_VRAM}

// CGRAM provides linear read access to emulated snes CGRAM
var CGRAM = MemoryTarget{iovm1.TARGET_CGRAM}

// OAM provides linear read access to emulated snes OAM
var OAM = MemoryTarget{iovm1.TARGET_OAM}

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
		iovm1.Instruction(iovm1.OPCODE_SETTDU, 0),
		m.t,
		// set address:
		iovm1.Instruction(iovm1.OPCODE_SETA24, 0),
		byte(addr & 0xFF),
		byte((addr >> 8) & 0xFF),
		byte((addr >> 16) & 0xFF),
		// set transfer length:
		iovm1.Instruction(iovm1.OPCODE_SETLEN, 0),
		byte(n & 0xFF),
		byte((n >> 8) & 0xFF),
		// read chunk:
		iovm1.Instruction(iovm1.OPCODE_READ, 0),
		iovm1.Instruction(iovm1.OPCODE_END, 0),
	}

	_ = prog
	panic("TODO")

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
		iovm1.Instruction(iovm1.OPCODE_SETTDU, 0),
		m.t,
		// set address:
		iovm1.Instruction(iovm1.OPCODE_SETA24, 0),
		byte(addr&0xFF),
		byte((addr>>8)&0xFF),
		byte((addr>>16)&0xFF),
		// set transfer length:
		iovm1.Instruction(iovm1.OPCODE_SETLEN, 0),
		byte(n&0xFF),
		byte((n>>8)&0xFF),
		// read chunk:
		iovm1.Instruction(iovm1.OPCODE_WRITE, 0),
	)
	prog = append(prog, p...)
	prog = append(prog,
		iovm1.Instruction(iovm1.OPCODE_END, 0),
	)

	_ = prog
	panic("TODO")

	return nil
}

func (m *MemoryTarget) Target() iovm1.Target {
	return m.t
}
