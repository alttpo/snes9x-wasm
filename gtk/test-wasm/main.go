package main

import (
	_ "google.golang.org/grpc"
	"os"
)

func main() {
	var apiFile *os.File
	var err error
	apiFile, err = os.OpenFile("grpc:///snes", os.O_RDWR, 0666)
	if err != nil {
		return
	}
	defer apiFile.Close()

	//grpc

}
