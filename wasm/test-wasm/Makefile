REX_SRC := \
 rex/events.go \
 rex/iovm.go \
 rex/mem.go \
 rex/net.go \
 rex/ppux.go \
 rex/text.go

main.wasm: main.go go.mod go.sum $(REX_SRC)
# -gc=none
	tinygo build -target wasi -scheduler=none -panic=trap -o main.wasm
#	GOOS=wasip1 GOARCH=wasm gotip build -o main.wasm
