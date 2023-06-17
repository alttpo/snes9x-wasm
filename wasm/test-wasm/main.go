package main

import (
	"fmt"
	"main/rex"
	"strings"
	"time"
)

const (
	ev_none = iota
	ev_shutdown
	ev_snes_nmi
	ev_snes_irq
	ev_ppu_frame_start
	ev_ppu_frame_end
	ev_ppu_frame_skip
)

var slotsArray [8]*rex.Socket
var slots []*rex.Socket

// each pixel is represented by a 4-byte little-endian uint32:
//
//	MSB                                             LSB
//	1111 1111     1111 1111     0000 0000     0000 0000
//
// [ fedc ba98 ] [ 7654 3210 ] [ fedc ba98 ] [ 7654 3210 ]
//
//	---- ----     ---- --pp     Errr rrgg     gggb bbbb    E = enable pixel
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
	var event uint32

	// read rom header:
	_, _ = rex.ROM.ReadAt(romTitle[:], 0x7FC0)
	fmt.Printf("rom title: `%s`\n", strings.TrimRight(string(romTitle[:]), " \000"))

	// read ROM contents of link's entire sprite sheet:
	_, _ = rex.ROM.ReadAt(linkSprites[:], 0x08_0000)
	// upload to ppux vram from rom sprite data:
	_ = rex.PPUX.VRAM.Upload(0, linkSprites[:])

	// upload palette to ppux cgram:
	palette := [0x20]byte{
		0x00, 0x00, 0xff, 0x7f, 0x7e, 0x23, 0xb7, 0x11, 0x9e, 0x36, 0xa5, 0x14, 0xff, 0x01, 0x78, 0x10,
		0x9d, 0x59, 0x47, 0x36, 0x68, 0x3b, 0x4a, 0x0a, 0xef, 0x12, 0x5c, 0x2a, 0x71, 0x15, 0x18, 0x7a,
	}
	_ = rex.PPUX.CGRAM.Upload(0, palette[:])

	// listen on tcp port 25600
	slots = slotsArray[:1:8]
	var err error
	slots[0], err = rex.TCPListen(25600)
	if err != nil {
		fmt.Printf("listen: %v\n", err)
	}

	r = 0

	//lastEvent := time.Now()
	lastFrame := uint8(0)
	for {
		handleNetwork()

		// poll for snes events:
		var ok bool
		event, ok = rex.WaitForEvent(time.Microsecond * 1000)
		if !ok {
			//fmt.Printf("wait_for_event: %d us\n", time.Now().Sub(lastEvent).Microseconds())
			continue
		}

		//nd := time.Now()
		//fmt.Printf("event(%032b): %d us\n", events, nd.Sub(lastEvent).Microseconds())
		//lastEvent = nd

		// graceful exit condition:
		if event == ev_shutdown {
			break
		}

		if event == ev_ppu_frame_skip {
			// read some WRAM:
			_, _ = rex.WRAM.ReadAt(wram[0x0:0x100], 0x0)
			rex.AcknowledgeLastEvent()

			currFrame := wram[0x1A]
			if uint8(currFrame-lastFrame) >= 2 {
				fmt.Printf("%02x -> %02x\n", lastFrame, currFrame)
			}
			lastFrame = wram[0x1A]
			fmt.Printf("%02x\n", lastFrame)
			continue
		}

		if event != ev_ppu_frame_start {
			rex.AcknowledgeLastEvent()
			//fmt.Printf("events timeout: %d us\n", nd.Sub(st).Microseconds())
			continue
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
			//   ---o slll     ---- ----     ---- --ww     wwww wwww
			// w = 8, l = 1 (BG2), s = 0, o = 0
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
		cmd = append(cmd,
			//   MSB                                             LSB
			//   1111 1111     1111 1111     0000 0000     0000 0000
			// [ fedc ba98 ] [ 7654 3210 ] [ fedc ba98 ] [ 7654 3210 ]
			//   1ooo oooo     ---- ----     ssss ssss     ssss ssss    o = opcode
			//                                                          s = size of packet in uint32_ts
			0b1000_0010_0000_0000_0000_0000_0000_0000+5,
			//   MSB                                             LSB
			//   1111 1111     1111 1111     0000 0000     0000 0000
			// [ fedc ba98 ] [ 7654 3210 ] [ fedc ba98 ] [ 7654 3210 ]
			//   ---- ----     ---- ----     ---- --xx     xxxx xxxx    x = x coordinate (0..1023)
			//   ---- ----     ---- ----     ---- --yy     yyyy yyyy    y = y coordinate (0..1023)
			//   ---- ----     dddd dddd     dddd dddd     dddd dddd    d = bitmap data address in extra ram
			//   ---- ----     cccc cccc     cccc cccc     cccc cccc    c = cgram/palette address in extra ram (points to color 0 of palette)
			//   --pp slll     ---- ----     ---- ----     fvhh hwww    w = 8<<w width in pixels
			//                                                          h = 8<<h height in pixels
			//                                                          f = horizontal flip
			//                                                          v = vertical flip
			//                                                          l = PPU layer
			//                                                          s = main or sub screen; main=0, sub=1
			132,
			132,
			0x0000,
			0x0000,
			0b0010_0100_0000_0000_0000_0000_1000_1001,
		)
		// end of list:
		cmd = append(cmd, 0b1000_0000_0000_0000_0000_0000_0000_0000)
		_ = rex.PPUX.CmdWrite(cmd)

		// read some WRAM:
		_, _ = rex.WRAM.ReadAt(wram[0x0:0x100], 0x0)
		currFrame := wram[0x1A]
		if uint8(currFrame-lastFrame) >= 2 {
			fmt.Printf("%02x -> %02x\n", lastFrame, currFrame)
		}
		lastFrame = wram[0x1A]

		// unblock emulator:
		rex.AcknowledgeLastEvent()

		fmt.Printf("%02x\n", lastFrame)
	}

	slots[0].Close()

	fmt.Println("exit")
}

var msg [65536]byte

func handleNetwork() {
	// poll for net i/o non-blocking:
	if len(slots) == 0 {
		return
	}
	//fmt.Printf("polling %d slots\n", len(slots))

	var n int
	var err error
	n, err = rex.NetPoll(slots)
	if err != nil {
		fmt.Printf("poll: %v\n", err)
		return
	}

	//fmt.Printf("poll: %d slots have events\n", n)
	for i := 1; i < len(slots); i++ {
		revents := slots[i].Revents()
		if revents == 0 {
			continue
		}

		fmt.Printf("poll: slot[%d]: revents=0x%04x\n", slots[i].Slot, revents)
		if slots[i].IsReadAvailable() {
			n, err = slots[i].Read(msg[:])
			if err != nil {
				fmt.Printf("read: slot[%d]: %v\n", slots[i].Slot, err)
			} else if n > 0 {
				_, _ = slots[i].Write(msg[:n])
			}
		}
		if slots[i].IsClosed() {
			fmt.Printf("slot[%d]: closing\n", slots[i].Slot)
			_ = slots[i].Close()
			slots = append(slots[0:i], slots[i+1:]...)
			i--
		}
	}

	if len(slots) > 0 {
		revents := slots[0].Revents()
		if revents == 0 {
			return
		}

		fmt.Printf("poll: slot[%d]: revents=0x%04x\n", slots[0].Slot, revents)
		if slots[0].IsReadAvailable() {
			var accepted *rex.Socket
			accepted, err = slots[0].TCPAccept()
			if err != nil {
				fmt.Printf("accept: %v\n", err)
			} else {
				fmt.Printf("accepted slot[%d]\n", accepted.Slot)
				slots = append(slots, accepted)
			}
		}
	}
}
