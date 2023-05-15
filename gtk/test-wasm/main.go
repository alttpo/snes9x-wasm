package main

import (
	"fmt"
	"io"
	"os"
	"strings"
	"time"
	"unsafe"
)

type ReaderWriterAt interface {
	io.ReaderAt
	io.WriterAt
}

var (
	romFile    ReaderWriterAt
	wramFile   ReaderWriterAt
	vramFile   ReaderWriterAt
	eventsFile io.Reader
	ppuxQueue  io.Writer
)

func main() {
	var err error

	var fWRAM *os.File
	fmt.Println("opening wram")
	fWRAM, err = os.OpenFile("/tmp/snes/mem/wram", os.O_RDWR, 0666)
	if err != nil {
		_, _ = fmt.Fprintf(os.Stderr, "open(wram): %v\n", err)
		return
	}
	defer fWRAM.Close()
	fmt.Printf("fd: %d\n", fWRAM.Fd())
	wramFile = fWRAM

	var fVRAM *os.File
	fmt.Println("opening vram")
	fVRAM, err = os.OpenFile("/tmp/snes/mem/vram", os.O_RDWR, 0666)
	if err != nil {
		_, _ = fmt.Fprintf(os.Stderr, "open(vram): %v\n", err)
		return
	}
	defer fVRAM.Close()
	fmt.Printf("fd: %d\n", fVRAM.Fd())
	vramFile = fVRAM

	var fROM *os.File
	fmt.Println("opening rom")
	fROM, err = os.OpenFile("/tmp/snes/mem/rom", os.O_RDWR, 0666)
	if err != nil {
		_, _ = fmt.Fprintf(os.Stderr, "open(rom): %v\n", err)
		return
	}
	defer fROM.Close()
	fmt.Printf("fd: %d\n", fROM.Fd())
	romFile = fROM

	var fEvents *os.File
	fmt.Println("opening events")
	fEvents, err = os.OpenFile("/tmp/snes/events", os.O_RDONLY, 0666)
	if err != nil {
		_, _ = fmt.Fprintf(os.Stderr, "open(events): %v\n", err)
		return
	}
	defer fEvents.Close()
	fmt.Printf("fd: %d\n", fEvents.Fd())
	eventsFile = fEvents

	var fPPUX *os.File
	fmt.Println("opening ppux")
	fPPUX, err = os.OpenFile("/tmp/snes/ppux/cmd", os.O_RDONLY, 0666)
	if err != nil {
		_, _ = fmt.Fprintf(os.Stderr, "open(ppux): %v\n", err)
		return
	}
	defer fPPUX.Close()
	fmt.Printf("fd: %d\n", fPPUX.Fd())
	ppuxQueue = fPPUX

	ppuxWrite := func(v []uint32) {
		_, _ = ppuxQueue.Write(unsafe.Slice((*byte)(unsafe.Pointer(&v[0])), len(v)*4))
	}

	// read rom header:
	var romTitle [21]byte
	_, err = romFile.ReadAt(romTitle[:], 0x7FC0)
	if err != nil {
		_, _ = fmt.Fprintf(os.Stderr, "read(rom): %v\n", err)
		return
	}
	fmt.Printf("rom title: `%s`\n", strings.TrimRight(string(romTitle[:]), " \000"))

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

	lastNMI := time.Now()
	for {
		var n int

		// wait for NMI:
		//st := time.Now()
		eventsSlice := unsafe.Slice((*byte)(unsafe.Pointer(&events)), 4)
		n, err = eventsFile.Read(eventsSlice)
		if err != nil {
			_, _ = fmt.Fprintf(os.Stderr, "read(events): %v\n", err)
		}
		nd := time.Now()
		if events&4 == 0 {
			//fmt.Printf("events timeout: %d us\n", nd.Sub(st).Microseconds())
			continue
		}
		fmt.Printf("NMI: %d us\n", nd.Sub(lastNMI).Microseconds())
		lastNMI = nd

		// write to bg1 main a rotating test pixel pattern:
		// (512*(110+y)+118)*4
		ppuxWrite([]uint32{
			//   MSB                                             LSB
			//   1111 1111     1111 1111     0000 0000     0000 0000
			// [ fedc ba98 ] [ 7654 3210 ] [ fedc ba98 ] [ 7654 3210 ]
			//   oooo oooo     ---- ----     ssss ssss     ssss ssss    o = opcode
			//                                                          s = size of packet in uint32_ts
			0b1000_0001_0000_0000_0000_0000_0000_0000 + (8 * 7) + 2,
			//   MSB                                             LSB
			//   1111 1111     1111 1111     0000 0000     0000 0000
			// [ fedc ba98 ] [ 7654 3210 ] [ fedc ba98 ] [ 7654 3210 ]
			//   ---- ----     ---- slll     ---- --ww     wwww wwww
			// w = 8, l = 1 (BG2), s = 0
			0b0000_0000_0000_0001_0000_0000_0000_0000 + 8,
			//   MSB                                             LSB
			//   1111 1111     1111 1111     0000 0000     0000 0000
			// [ fedc ba98 ] [ 7654 3210 ] [ fedc ba98 ] [ 7654 3210 ]
			//   ---- --yy     yyyy yyyy     ---- --xx     xxxx xxxx
			uint32((110 << 16) | (118 + r)),
		})
		for y := int64(0); y < 7; y++ {
			rotate()
			ppuxWrite(rotatingPixels[:])
		}
		// end of list:
		ppuxWrite([]uint32{0b1000_0000_0000_0000_0000_0000_0000_0000})

		// read half of WRAM:
		n, err = wramFile.ReadAt(wram[0x0:0x100], 0x0)
		if n == 0 {
			continue
		}
		fmt.Printf("%02x\n", wram[0x1A])
		//fmt.Printf("wram[$10] = %02x\n", wram[0x10])
	}

	fmt.Println("exit")
}
