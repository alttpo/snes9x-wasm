package main

import (
	"encoding/hex"
	"testing"
	"testrex/rex"
)

func TestIOVM(t *testing.T) {
	vmprog := [...]byte{
		// setup channel 0 for WRAM read:
		rex.IOVM1Instruction(rex.IOVM1_OPCODE_SETTDU, 0),
		rex.IOVM1_TARGET_WRAM,
		rex.IOVM1Instruction(rex.IOVM1_OPCODE_SETA8, 0),
		0x10,
		rex.IOVM1Instruction(rex.IOVM1_OPCODE_SETLEN, 0),
		0xF0,
		0x00,

		// setup channel 3 for NMI $2C00 write in reverse direction so $2C00 byte is written last:
		rex.IOVM1Instruction(rex.IOVM1_OPCODE_SETCMPMSK, 3),
		0x00,
		0xFF,
		rex.IOVM1Instruction(rex.IOVM1_OPCODE_SETTDU, 3),
		rex.IOVM1_TARGET_2C00 | rex.IOVM1_TARGETFLAG_REVERSE,
		rex.IOVM1Instruction(rex.IOVM1_OPCODE_SETA8, 3),
		0x00, // $2C00
		rex.IOVM1Instruction(rex.IOVM1_OPCODE_SETLEN, 3),
		6, // write 6 bytes
		0,
		rex.IOVM1Instruction(rex.IOVM1_OPCODE_WRITE, 3),
		0x9C, // 2C00: STZ $2C00
		0x00,
		0x2C,
		0x6C, // 2C03: JMP ($FFEA)
		0xEA,
		0xFF,
		// wait while [$2C00] != $00
		rex.IOVM1Instruction(rex.IOVM1_OPCODE_WAIT_WHILE_NEQ, 3),

		// now issue WRAM read on channel 0:
		rex.IOVM1Instruction(rex.IOVM1_OPCODE_READ, 0),
	}
	t.Log("\n" + hex.Dump(vmprog[:]))
}
