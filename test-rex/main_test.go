package main

import (
	"bytes"
	"encoding/hex"
	"fmt"
	"io"
	"os"
	"strings"
	"testing"
	"testrex/rex"
	"testrex/rex/frame"
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
	//fmt.Print("\n" + hex.Dump(vmprog[:]))

	var sb *strings.Builder

	frames := bytes.Buffer{}
	fw := frame.NewWriter(&frames, 0)
	fw.Write([]byte{0x00})
	fw.Write(vmprog[:])
	fw.EndMessage()

	sb = toHex(&strings.Builder{}, frames.Bytes())
	fmt.Print("\n" + sb.String())
}

func TestPPUX(t *testing.T) {
	var sb *strings.Builder

	frames := bytes.Buffer{}
	fw := frame.NewWriter(&frames, 0)

	// ppux_cgram_upload:
	palette := [0x20]byte{
		0x00, 0x00, 0xff, 0x7f, 0x7e, 0x23, 0xb7, 0x11, 0x9e, 0x36, 0xa5, 0x14, 0xff, 0x01, 0x78, 0x10,
		0x9d, 0x59, 0x47, 0x36, 0x68, 0x3b, 0x4a, 0x0a, 0xef, 0x12, 0x5c, 0x2a, 0x71, 0x15, 0x18, 0x7a,
	}
	fw.Write([]byte{0x12, 0, 0, 0, 0})
	fw.Write(palette[:])
	fw.EndMessage()

	sb = toHex(&strings.Builder{}, frames.Bytes())
	fmt.Print("\n" + sb.String())

	// ppux_vram_upload:
	frames.Reset()
	fw.Write([]byte{0x11, 0, 0, 0, 0})
	{
		var lf []byte
		var err error
		lf, err = os.ReadFile("/Users/jim.dunne/Developer/me/alttpo/alttp-jp.sfc")
		if err != nil {
			t.Fatal(err)
		}
		copy(linkSprites[:], lf[0x08_0000:])
	}
	fw.Write(linkSprites[:0x7000])
	fw.EndMessage()
	sb = toHex(&strings.Builder{}, frames.Bytes())
	fmt.Print("\n" + sb.String())

	var cmdWords [(0) + (1 + 2 + 8*7) + (1 + 4) + (1 + 2) + 1 + 10]uint32

	cmd := cmdWords[:0]
	cmd = append(cmd,
		//   MSB                                             LSB
		//   1111 1111     1111 1111     0000 0000     0000 0000
		// [ fedc ba98 ] [ 7654 3210 ] [ fedc ba98 ] [ 7654 3210 ]
		//   1ooo oooo     ---- ----     ssss ssss     ssss ssss    o = opcode
		//                                                          s = size of packet in uint32_ts
		0b1000_0011_0000_0000_0000_0000_0000_0000+3,
		// index 0
		0,
		// set pointer to offsx[0]:
		0b0000_0000_0000_0000_0000_0000_0000_0000|0xE2, // BG2H (lttp)
		//0b0000_0000_0000_0000_0000_0000_0000_0000|0xB1, // BG1H (sm)
		// set pointer to offsy[0]:
		0b0000_0000_0000_0000_0000_0000_0000_0000|0xE8, // BG2V (lttp)
		//0b0000_0000_0000_0000_0000_0000_0000_0000|0xB3, // BG1V (sm)

		//   MSB                                             LSB
		//   1111 1111     1111 1111     0000 0000     0000 0000
		// [ fedc ba98 ] [ 7654 3210 ] [ fedc ba98 ] [ 7654 3210 ]
		//   1ooo oooo     ---- ----     ssss ssss     ssss ssss    o = opcode
		//                                                          s = size of packet in uint32_ts
		0b1000_0010_0000_0000_0000_0000_0000_0000+4,
		//   MSB                                             LSB
		//   1111 1111     1111 1111     0000 0000     0000 0000
		// [ fedc ba98 ] [ 7654 3210 ] [ fedc ba98 ] [ 7654 3210 ]
		//   iiii ----     --ww ---x     xxxx xxxx     xxxx xxxx
		//   jjjj ----     --hh ---y     yyyy yyyy     yyyy yyyy
		//   --bb --dd     dddd dddd     dddd dddd     dddd dddd
		//   vfpp slll     ---- -ccc     cccc cccc     cccc cccc
		//
		//    x = x coordinate (-65536..65535)
		//    w = width in pixels  = 8 << w  (8, 16, 32, 64)
		// iiii = if bit[n]=1, subtract offsx[n] from x coord
		//    y = y coordinate (-65536..65535)
		//    h = height in pixels = 8 << h  (8, 16, 32, 64)
		// jjjj = if bit[n]=1, subtract offsy[n] from y coord
		//    d = bitmap data address in extra ram
		//    b = bits per pixel   = 2 << b  (2, 4, 8)
		//    c = cgram/palette address in extra ram (points to color 0 of palette)
		//    l = PPU layer (0: BG1, 1: BG2, 2: BG3, 3: BG4, 4: OBJ)
		//    s = main or sub screen; main=0, sub=1
		//    p = priority (0..3 for OBJ, 0..1 for BG)
		//    f = horizontal flip
		//    v = vertical flip
		// 2625 = BG2V of throne room, 640 = BG2H of throne room
		// X = 640+132
		// Y = 2625+132
		// crateria opening:
		0b0001_0000_0001_0000_0000_0000_0000_0000|(2048+119),   // x, w = 16
		0b0001_0000_0001_0000_0000_0000_0000_0000|(2048+152+8), // y, h = 16
		0b0001_0000_0000_0000_0000_0000_0000_0000|0x0440,       // d = 0x0200, b = 1
		0b0110_0100_0000_0000_0000_0000_0000_0000|0x0000,       // c = 0x0000, f = 1, p = 2, s = 0, l = 4

		0b1000_0010_0000_0000_0000_0000_0000_0000+4,
		0b0001_0000_0001_0000_0000_0000_0000_0000|(2048+120), // x, w = 16
		0b0001_0000_0001_0000_0000_0000_0000_0000|(2048+152), // y, h = 16
		0b0001_0000_0000_0000_0000_0000_0000_0000|0x0000,     // d = 0, b = 1
		0b0110_0100_0000_0000_0000_0000_0000_0000|0x0000,     // c = 0, f = 1, p = 2, s = 0, l = 4
	)
	// end of list:
	cmd = append(cmd, 0b1000_0000_0000_0000_0000_0000_0000_0000)

	cmdBytes := unsafe.Slice((*byte)(unsafe.Pointer(&cmd[0])), len(cmd)*4)

	// ppux_exec:
	frames.Reset()
	fw.Write([]byte{0x10})
	io.Copy(fw, bytes.NewReader(cmdBytes))
	fw.EndMessage()
	sb = toHex(&strings.Builder{}, frames.Bytes())
	fmt.Print("\n" + sb.String())
}

func toHex(sb *strings.Builder, b []byte) *strings.Builder {
	he := hex.NewEncoder(sb)
	for i := range b {
		_, _ = he.Write(b[i : i+1])
		if i&63 == 63 || i == len(b)-1 {
			sb.WriteByte('\n')
		} else {
			sb.WriteByte(' ')
		}
	}
	return sb
}
