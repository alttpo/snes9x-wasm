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
	var sb *strings.Builder

	frames := bytes.Buffer{}
	fw := rex.NewFrameWriter(&frames, 0)

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
	fw.Write(linkSprites[:0x400])
	fw.EndMessage()
	sb = toHex(&strings.Builder{}, frames.Bytes())
	fmt.Print("\n" + sb.String())

	var cmdWords [(0) + (1 + 2 + 8*7) + (1 + 4) + (1 + 2) + 1 + 10]uint32

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
		0b1000_0011_0000_0000_0000_0000_0000_0000+2,
		// set pointer to offsx[0]:
		//0b0000_0000_0000_0000_0000_0000_0000_0000|0xE2, // BG2H (lttp)
		0b0000_0000_0000_0000_0000_0000_0000_0000|0xB1, // BG1H (sm)
		// set pointer to offsy[0]:
		//0b0000_0000_0000_0000_0000_0000_0000_0000|0xE8, // BG2V (lttp)
		0b0000_0000_0000_0000_0000_0000_0000_0000|0xB3, // BG1V (sm)

		//   MSB                                             LSB
		//   1111 1111     1111 1111     0000 0000     0000 0000
		// [ fedc ba98 ] [ 7654 3210 ] [ fedc ba98 ] [ 7654 3210 ]
		//   1ooo oooo     ---- ----     ssss ssss     ssss ssss    o = opcode
		//                                                          s = size of packet in uint32_ts
		0b1000_0010_0000_0000_0000_0000_0000_0000+4,
		//    MSB                                             LSB
		//   1111 1111     1111 1111     0000 0000     0000 0000
		// [ fedc ba98 ] [ 7654 3210 ] [ fedc ba98 ] [ 7654 3210 ]
		//   yyyy yyyy     yyyy yyyy     xxxx xxxx     xxxx xxxx    x = x coordinate (0..65535)
		//   vfpp slll     jjjj iiii     hhhh hhhh     wwww wwww    y = y coordinate (0..65535)
		//   ---- ----     dddd dddd     dddd dddd     dddd dddd    d = bitmap data address in extra ram
		//   ---- ----     cccc cccc     cccc cccc     cccc cccc    c = cgram/palette address in extra ram (points to color 0 of palette)
		//                                                          w = width in pixels
		//                                                          h = height in pixels
		//                                                          f = horizontal flip
		//                                                          v = vertical flip
		//                                                          l = PPU layer
		//                                                          s = main or sub screen; main=0, sub=1
		//                                                          p = priority (0..3 for OBJ, 0..1 for BG)
		//                                                       iiii = if bit[n]=1, subtract offsx[n] from x coord
		//                                                       jjjj = if bit[n]=1, subtract offsy[n] from y coord
		// 2625 = BG2V of throne room, 640 = BG2H of throne room
		//((2625+132)<<16)|(640+132),
		// crateria opening:
		((1024+148)<<16)|(2048+120),
		0b0110_0100_0001_0001_0001_0000_0001_0000,
		0x0000,
		0x0000,
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
