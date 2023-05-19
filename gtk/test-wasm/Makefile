main.wasm: main.go sneswasm.go go.mod go.sum
	tinygo build -target wasi -panic trap -o main.wasm
#	GOOS=wasip1 GOARCH=wasm gotip build -o main.wasm
