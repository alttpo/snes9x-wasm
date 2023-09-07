package main

import (
	"fmt"
	"net"
	"net/netip"
	"testrex/rex"
	"time"
	"unsafe"
)

// each pixel is represented by a 4-byte little-endian uint32:
//
//	MSB                                             LSB
//	1111 1111     1111 1111     0000 0000     0000 0000
//
// [ fedc ba98 ] [ 7654 3210 ] [ fedc ba98 ] [ 7654 3210 ]
//
//	E--- ----     ---- --pp     -rrr rrgg     gggb bbbb    E = enable pixel
//	                                                       r = red (5-bits)
//	                                                       g = green (5-bits)
//	                                                       b = blue (5-bits)
//	                                                       p = priority [0..3]
//	 1000_0000_0000_00pp_0_rrrrr_ggggg_bbbbb
var testPixels = [8]uint32{
	// ---
	0b1000_0000_0000_0000_0_00000_00000_00000,
	// --B
	0b1000_0000_0000_0000_0_00000_00000_11111,
	// -G-
	0b1000_0000_0000_0000_0_00000_11111_00000,
	// -GB
	0b1000_0000_0000_0000_0_00000_11111_11111,
	// R--
	0b1000_0000_0000_0000_0_11111_00000_00000,
	// R-B
	0b1000_0000_0000_0000_0_11111_00000_11111,
	// RG-
	0b1000_0000_0000_0000_0_11111_11111_00000,
	// RGB
	0b1000_0000_0000_0000_0_11111_11111_11111,
}

var rotatingPixels [8]uint32

var linkSprites [0x7000]byte
var romTitle [21]byte

var cmdBytes [(1 + 2 + 8*7) + (1 + 5) + 1 + 10]uint32

var r int

func rotate() {
	n := copy(rotatingPixels[:], testPixels[r:])
	copy(rotatingPixels[n:], testPixels[0:8-n])
	r = (r + 1) & 7
}

var wram [0x20000]byte

func main() {
	var err error
	var rpcConn *net.TCPConn
	rpcConn, err = net.DialTCP(
		"",
		nil,
		net.TCPAddrFromAddrPort(
			netip.AddrPortFrom(
				netip.AddrFrom4([4]byte{127, 0, 0, 1}),
				11264,
			),
		),
	)
	if err != nil {
		panic(err)
	}
	defer func(rpcConn net.Conn) {
		_ = rpcConn.Close()
	}(rpcConn)

	rpc := rex.NewRPC(rpcConn)
	//rpc.IOVM1Upload()

	// read rom header:
	if err = blockingRead(&rex.ROM, romTitle[:], 0x7FC0); err != nil {
		_, _ = rex.Stderr.WriteString("unable to read ROM header: ")
		_, _ = rex.Stderr.WriteString(err.Error())
		_, _ = rex.Stderr.WriteString("\n")
	} else {
		//fmt.Printf("rom title: `%s`\n", strings.TrimRight(string(romTitle[:]), " \000"))
		_, _ = rex.Stdout.WriteString("rom title: `")
		_, _ = rex.Stdout.WriteString(unsafe.String(&romTitle[0], 21))
		_, _ = rex.Stdout.WriteString("`\n")
	}

	// read ROM contents of Link's entire sprite sheet:
	if err = blockingRead(&rex.ROM, linkSprites[:], 0x08_0000); err != nil {
		_, _ = rex.Stderr.WriteString("unable to read ROM sprites: ")
		_, _ = rex.Stderr.WriteString(err.Error())
		_, _ = rex.Stderr.WriteString("\n")
	} else {
		// upload to ppux vram from rom sprite data:
		_ = rex.PPUX.VRAM.Upload(0, linkSprites[:])
	}

	for {
		// receive any RPC responses and notifications:
		err = rpc.Receive(time.Now().Add(time.Microsecond * 100))
		if err != nil {
			panic(err)
		}

	}

	// upload palette to ppux cgram:
	palette := [0x20]byte{
		0x00, 0x00, 0xff, 0x7f, 0x7e, 0x23, 0xb7, 0x11, 0x9e, 0x36, 0xa5, 0x14, 0xff, 0x01, 0x78, 0x10,
		0x9d, 0x59, 0x47, 0x36, 0x68, 0x3b, 0x4a, 0x0a, 0xef, 0x12, 0x5c, 0x2a, 0x71, 0x15, 0x18, 0x7a,
	}
	_ = rex.PPUX.CGRAM.Upload(0, palette[:])

	// $068328 Sprite_Main in alttp-jp.sfc
	//ev_pc := rex.EventRegisterBreak(0x068328, time.Microsecond*2000)

	// load a IOVM1 program into vm0 and execute it each frame:
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
	if err = rex.IOVM1[0].Execute(vmprog[:]); err != nil {
		fmt.Printf("%v\n", err)
		return
	}

	r = 0

	buf := [256]byte{}
	_ = buf
	lastFrame := uint8(0)
	for {
		// TODO: get notified from rex
		if false {
			// iovm read complete:

			//var n uint32
			//var addr uint32
			//var target uint8
			//if n, addr, target, err = rex.IOVM1[0].Read(buf[:]); err != nil {
			//	_, _ = rex.Stdout.WriteString("error during iovm_read: ")
			//	_, _ = rex.Stdout.WriteString(err.Error())
			//	_, _ = rex.Stdout.WriteString("\n")
			//	continue
			//}
			//if n > 0 {
			//	//_, _ = rex.Stdout.WriteString(fmt.Sprintf("iovm_read: target=%d addr=%06x\n", target, addr))
			//	//_, _ = rex.Stdout.WriteString(hex.Dump(buf[:n]))
			//	//_, _ = rex.Stdout.WriteString("\n")
			//	if target == rex.IOVM1_TARGET_WRAM {
			//		// copy the read data into our wram copy:
			//		copy(wram[addr:addr+n], buf[:n])
			//	}
			//}
			continue
		}

		// TODO: get notified from rex
		if false {
			// event != rex.Ev_iovm0_end
			continue
		}

		// end of IOVM program occurs on NMI; reset it:
		if err = rex.IOVM1[0].Reset(); err != nil {
			_, _ = rex.Stdout.WriteString("iovm_reset: ")
			_, _ = rex.Stdout.WriteString(err.Error())
			_, _ = rex.Stdout.WriteString("\n")
		}

		// write to bg2 main a rotating test pixel pattern:
		cmd := cmdBytes[:0]
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
		_ = rex.PPUX.CmdWrite(cmd)

		// read some WRAM:
		currFrame := wram[0x1A]
		if uint8(currFrame-lastFrame) >= 2 {
			fmt.Printf("%02x -> %02x\n", lastFrame, currFrame)
		}
		lastFrame = wram[0x1A]
	}
}
