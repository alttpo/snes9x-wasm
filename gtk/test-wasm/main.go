package main

import (
	"encoding/hex"
	"fmt"
	"io"
	"os"
	"strings"
	"time"
)

type ReaderWriterAt interface {
	io.ReaderAt
	io.WriterAt
}

var (
	romFile  ReaderWriterAt
	wramFile ReaderWriterAt
	vramFile ReaderWriterAt
	nmiFile  io.Reader
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

	var fNMI *os.File
	fmt.Println("opening nmi")
	fNMI, err = os.OpenFile("/tmp/snes/sig/blocking/nmi", os.O_RDONLY, 0666)
	if err != nil {
		_, _ = fmt.Fprintf(os.Stderr, "open(nmi): %v\n", err)
		return
	}
	defer fNMI.Close()
	fmt.Printf("fd: %d\n", fNMI.Fd())
	nmiFile = fNMI

	//fmt.Println("sleep 1s")
	//time.Sleep(time.Second)
	//fmt.Println("sleep 1s complete")

	ch := make(chan struct{})

	// read rom header:
	var romTitle [21]byte
	_, err = romFile.ReadAt(romTitle[:], 0x7FC0)
	if err != nil {
		_, _ = fmt.Fprintf(os.Stderr, "read(rom): %v\n", err)
		return
	}
	fmt.Printf("rom title: `%s`\n", strings.TrimRight(string(romTitle[:]), " \000"))

	go func() {
		fmt.Println("hello from goroutine")

		var wram [0x20000]byte
		var nmiSignal [1]byte
		for {
			var n int

			// wait for NMI:
			st := time.Now()
			n, err = nmiFile.Read(nmiSignal[:])
			if err != nil {
				_, _ = fmt.Fprintf(os.Stderr, "read(nmi): %v\n", err)
			}
			nd := time.Now()
			if nmiSignal[0] == 0 {
				fmt.Printf("NMI timeout: %d us\n", nd.Sub(st).Microseconds())
				continue
			}
			fmt.Printf("NMI: %d us\n", nd.Sub(st).Microseconds())

			// read one byte from WRAM:
			n, err = wramFile.ReadAt(wram[0x10:0x11], 0x10)
			if n == 0 {
				continue
			}
			fmt.Printf("wram[$10] = %02x\n", wram[0x10])

			if wram[0x10] == 0x14 {
				break
			}
		}

		var vram [0x10000]byte
		for {
			st := time.Now()
			_, err = nmiFile.Read(nmiSignal[:])
			if err != nil {
				_, _ = fmt.Fprintf(os.Stderr, "read(nmi): %v\n", err)
			}
			nd := time.Now()
			if nmiSignal[0] == 0 {
				fmt.Printf("NMI timeout: %d us\n", nd.Sub(st).Microseconds())
				continue
			}
			fmt.Printf("NMI: %d us\n", nd.Sub(st).Microseconds())

			_, err = vramFile.ReadAt(vram[0xE000:], 0xE000)
			if err != nil {
				_, _ = fmt.Fprintf(os.Stderr, "readat(vram): %v\n", err)
			}
			fmt.Println(hex.Dump(vram[0xE000:0xE010]))

			_, err = wramFile.ReadAt(wram[0x10:0x11], 0x10)
			if err != nil {
				_, _ = fmt.Fprintf(os.Stderr, "readat(wram): %v\n", err)
			}
			fmt.Println(hex.Dump(wram[0x0010:0x0011]))

			if wram[0x10] == 0x07 {
				break
			}
		}

		ch <- struct{}{}
	}()

	_ = <-ch

	fmt.Println("exit")
}
