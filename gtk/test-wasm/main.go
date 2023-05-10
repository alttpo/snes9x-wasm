package main

import (
	"fmt"
	"io"
	"os"
	"time"
)

type ReaderWriterAt interface {
	io.ReaderAt
	io.WriterAt
}

var (
	wramFile ReaderWriterAt
	nmiFile  io.Reader
)

func main() {
	var err error

	fmt.Println("opening wram")
	wramFile, err = os.OpenFile("/tmp/snes/mem/wram", os.O_RDWR, 0666)
	if err != nil {
		_, _ = fmt.Fprintf(os.Stderr, "open(wram): %v\n", err)
		return
	}
	//nmiFile, err = os.OpenFile("/tmp/snes/sig/nmi", os.O_RDONLY, 0666)
	//if err != nil {
	//	_, _ = fmt.Fprintf(os.Stderr, "open(nmi): %v\n", err)
	//	return
	//}

	fmt.Println("sleep 1s")
	time.Sleep(time.Second)
	fmt.Println("sleep 1s complete")

	ch := make(chan struct{})

	go func() {
		fmt.Println("hello from goroutine")

		var wram [131072]byte
		var nmiSignal [1]byte
		for i := 0; i < 16; i++ {
			var n int

			// wait for NMI:
			//n, err = nmiFile.Read(nmiSignal[:])
			//if n == 0 {
			//	continue
			//}
			//fmt.Println("NMI")
			_ = nmiSignal

			// read a byte from WRAM:
			n, err = wramFile.ReadAt(wram[0x0010:0x0010+1], 0x0010)
			if n == 0 {
				continue
			}
			fmt.Printf("wram[$10] = %02x\n", wram[0x10])
		}

		ch <- struct{}{}
	}()

	_ = <-ch

	fmt.Println("exit")
}
