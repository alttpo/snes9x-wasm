package rex

import (
	"errors"
	"io"
	"rex/iovm1"
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

func (m *MemoryTarget) GenerateReadProgram(buf io.Writer, addr uint32, n int, ch uint8) (err error) {
	if n == 0 {
		return
	}
	if n > 65536 {
		err = errBufferTooLarge
		return
	}

	if n == 65536 {
		// encode 65536 bytes as 0:
		n = 0
	}

	_, err = buf.Write([]byte{
		// set target:
		iovm1.Instruction(iovm1.OPCODE_SETTDU, ch),
		m.t,
		// set address:
		iovm1.Instruction(iovm1.OPCODE_SETA24, ch), // TODO: select A8, A16, A24 based on addr value
		byte(addr & 0xFF),
		byte((addr >> 8) & 0xFF),
		byte((addr >> 16) & 0xFF),
		// set transfer length:
		iovm1.Instruction(iovm1.OPCODE_SETLEN, ch),
		byte(n & 0xFF),
		byte((n >> 8) & 0xFF),
		// read chunk:
		iovm1.Instruction(iovm1.OPCODE_READ, ch),
		//iovm1.Instruction(iovm1.OPCODE_END, 0),
	})

	return
}

func (m *MemoryTarget) GenerateWriteProgram(buf io.Writer, addr uint32, p []byte) (err error) {
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

	if _, err = buf.Write([]byte{
		// set target:
		iovm1.Instruction(iovm1.OPCODE_SETTDU, 0),
		m.t,
		// set address:
		iovm1.Instruction(iovm1.OPCODE_SETA24, 0), // TODO: select A8, A16, A24 based on addr value
		byte(addr & 0xFF),
		byte((addr >> 8) & 0xFF),
		byte((addr >> 16) & 0xFF),
		// set transfer length:
		iovm1.Instruction(iovm1.OPCODE_SETLEN, 0),
		byte(n & 0xFF),
		byte((n >> 8) & 0xFF),
		// write chunk:
		iovm1.Instruction(iovm1.OPCODE_WRITE, 0),
	}); err != nil {
		return
	}

	// write the data:
	if _, err = buf.Write(p); err != nil {
		return
	}

	//if _, err = buf.Write([]byte{
	//	iovm1.Instruction(iovm1.OPCODE_END, 0),
	//}); err != nil {
	//	return
	//}

	return
}

func (m *MemoryTarget) Target() iovm1.Target {
	return m.t
}
