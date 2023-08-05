package main

import (
	"bytes"
	"encoding/hex"
	"io"
	"strings"
	"testing"
	"testrex/rex"
	"unsafe"
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

func TestPPUX(t *testing.T) {
	var cmdWords [(1 + 2 + 8*7) + (1 + 5) + 1 + 10]uint32

	// write to bg2 main a rotating test pixel pattern:
	cmd := cmdWords[:0]
	cmd = append(cmd,
		//   MSB                                             LSB
		//   1111 1111     1111 1111     0000 0000     0000 0000
		// [ fedc ba98 ] [ 7654 3210 ] [ fedc ba98 ] [ 7654 3210 ]
		//   1ooo oooo     ---- ----     ssss ssss     ssss ssss    o = opcode
		//                                                          s = size of packet in uint32_ts
		0b1000_0001_0000_0000_0000_0000_0000_0000+(8*7)+2,
		//   MSB                                             LSB
		//   1111 1111     1111 1111     0000 0000     0000 0000
		// [ fedc ba98 ] [ 7654 3210 ] [ fedc ba98 ] [ 7654 3210 ]
		//   ---- --yy     yyyy yyyy     ---- --xx     xxxx xxxx
		uint32((110<<16)|(118+r)),
		//   MSB                                             LSB
		//   1111 1111     1111 1111     0000 0000     0000 0000
		// [ fedc ba98 ] [ 7654 3210 ] [ fedc ba98 ] [ 7654 3210 ]
		//   -o-- slll     ---- ----     ---- --ww     wwww wwww
		// w = 8, l = 1 (BG2), s = 0, o = 0
		0b0000_0000_0000_0001_0000_0000_0000_0000+8,
	)
	for y := int64(0); y < 7; y++ {
		rotate()
		cmd = append(cmd, rotatingPixels[:]...)
	}
	cmd = append(cmd,
		//   MSB                                             LSB
		//   1111 1111     1111 1111     0000 0000     0000 0000
		// [ fedc ba98 ] [ 7654 3210 ] [ fedc ba98 ] [ 7654 3210 ]
		//   1ooo oooo     ---- ----     ssss ssss     ssss ssss    o = opcode
		//                                                          s = size of packet in uint32_ts
		0b1000_0010_0000_0000_0000_0000_0000_0000+4,
		//   MSB                                             LSB
		//  1111 1111     1111 1111     0000 0000     0000 0000
		//[ fedc ba98 ] [ 7654 3210 ] [ fedc ba98 ] [ 7654 3210 ]
		//  ---- --yy     yyyy yyyy     ---- --xx     xxxx xxxx    x = x coordinate (0..1023)
		//  --pp slll     ---- --vf     hhhh hhhh     wwww wwww    y = y coordinate (0..1023)
		//  ---- ----     dddd dddd     dddd dddd     dddd dddd    d = bitmap data address in extra ram
		//  ---- ----     cccc cccc     cccc cccc     cccc cccc    c = cgram/palette address in extra ram (points to color 0 of palette)
		//                                                         w = width in pixels
		//                                                         h = height in pixels
		//                                                         f = horizontal flip
		//                                                         v = vertical flip
		//                                                         l = PPU layer
		//                                                         s = main or sub screen; main=0, sub=1
		//                                                         p = priority (0..3 for OBJ, 0..1 for BG)
		(132<<16)|132,
		0b0010_0100_0000_0000_0001_0000_0001_0000,
		0x0000,
		0x0000,
	)
	// end of list:
	cmd = append(cmd, 0b1000_0000_0000_0000_0000_0000_0000_0000)

	cmdBytes := unsafe.Slice((*byte)(unsafe.Pointer(&cmd[0])), len(cmd)*4)
	t.Logf("%d\n", len(cmdBytes))

	frames := bytes.Buffer{}
	fw := rex.NewFrameWriter(&frames, 0)
	// ppux_exec:
	fw.Write([]byte{0x03})
	io.Copy(fw, bytes.NewReader(cmdBytes))
	fw.Close()

	sb := toHex(&strings.Builder{}, frames.Bytes())
	t.Log("\n" + sb.String())
}

func toHex(sb *strings.Builder, b []byte) *strings.Builder {
	he := hex.NewEncoder(sb)
	for i := range b {
		_, _ = he.Write(b[i : i+1])
		sb.WriteByte(' ')
	}
	return sb
}
