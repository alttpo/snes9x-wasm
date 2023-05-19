package main

import (
	"fmt"
	"math"
	"strings"
	"time"
)

const (
	ev_snes_nmi = 1 << iota
	ev_snes_irq
	ev_ppu_frame_start
	ev_ppu_frame_end

	ev_msg_received = 1 << 30
	ev_shutdown     = 1 << 31
)

func main() {
	var wram [0x20000]byte
	var events uint32

	// each pixel is represented by a 4-byte little-endian uint32:
	//   MSB                                             LSB
	//   1111 1111     1111 1111     0000 0000     0000 0000
	// [ fedc ba98 ] [ 7654 3210 ] [ fedc ba98 ] [ 7654 3210 ]
	//   ---- ----     ---- --pp     Errr rrgg     gggb bbbb    E = enable pixel
	//                                                          r = red (5-bits)
	//                                                          g = green (5-bits)
	//                                                          b = blue (5-bits)
	//                                                          p = priority [0..3]
	//    0000_0000_0000_00pp_1_rrrrr_ggggg_bbbbb
	testPixels := [8]uint32{
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

	r := 0
	rotatingPixels := [8]uint32{}
	rotate := func() {
		n := copy(rotatingPixels[:], testPixels[r:])
		copy(rotatingPixels[n:], testPixels[0:8-n])
		r = (r + 1) & 7
	}

	// read rom header:
	var romTitle [21]byte
	_ = ReadROM(romTitle[:], 0x7FC0)
	fmt.Printf("rom title: `%s`\n", strings.TrimRight(string(romTitle[:]), " \000"))

	lastEvent := time.Now()
	for {
		// poll for snes events:
		events = poll_events(math.MaxUint32)

		nd := time.Now()
		if events == 0 {
			continue
		}
		fmt.Printf("event(%08b): %d us\n", events, nd.Sub(lastEvent).Microseconds())
		lastEvent = nd

		// graceful exit condition:
		if events&ev_shutdown != 0 {
			break
		}

		if events&ev_ppu_frame_end == 0 {
			//fmt.Printf("events timeout: %d us\n", nd.Sub(st).Microseconds())
			continue
		}

		// write to bg2 main a rotating test pixel pattern:
		cmdBytes := [1 + 2 + 8*7 + 1]uint32{}
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
			//   ---- ----     ---- slll     ---- --ww     wwww wwww
			// w = 8, l = 1 (BG2), s = 0
			0b0000_0000_0000_0001_0000_0000_0000_0000+8,
			//   MSB                                             LSB
			//   1111 1111     1111 1111     0000 0000     0000 0000
			// [ fedc ba98 ] [ 7654 3210 ] [ fedc ba98 ] [ 7654 3210 ]
			//   ---- --yy     yyyy yyyy     ---- --xx     xxxx xxxx
			uint32((110<<16)|(118+r)),
		)
		for y := int64(0); y < 7; y++ {
			rotate()
			cmd = append(cmd, rotatingPixels[:]...)
		}
		// end of list:
		cmd = append(cmd, 0b1000_0000_0000_0000_0000_0000_0000_0000)
		ppux_write(cmd)

		// read half of WRAM:
		wram_read(wram[0x0:0x100], 0x0)
		fmt.Printf("%02x\n", wram[0x1A])
		//fmt.Printf("wram[$10] = %02x\n", wram[0x10])
	}

	fmt.Println("exit")
}
