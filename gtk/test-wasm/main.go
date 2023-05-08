package main

import (
	"encoding/binary"
	"encoding/hex"
	_ "google.golang.org/protobuf/proto"
	"io"
	"os"
)

var (
	fIn  io.Reader
	fOut io.Writer
)

func main() {
	var err error

	fIn = os.Stdin
	fOut = os.Stdout

	var frame [65536]byte
	msgSizeLE := frame[0:2]

	for {
		var n int

		// read message size as uint16:
		n, err = io.ReadFull(fIn, msgSizeLE)
		if err != nil {
			_, _ = os.Stderr.WriteString(err.Error())
			return
		}
		_ = n

		size := int(binary.LittleEndian.Uint16(msgSizeLE))
		if size == 0 {
			// ignore empty messages:
			continue
		}

		// read message:
		msg := frame[2 : 2+size]
		n, err = io.ReadFull(fIn, msg)
		if err != nil {
			_, _ = os.Stderr.WriteString(err.Error())
			return
		}

		// echo message back:
		n, err = fOut.Write(frame[0 : 2+size])
		if err != nil {
			_, _ = os.Stderr.WriteString(err.Error())
			return
		}

		// dump hex of message to stderr:
		errDump := hex.Dumper(os.Stderr)
		errDump.Write(msg)
		errDump.Close()
	}
}
