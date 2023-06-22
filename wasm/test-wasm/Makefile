REX_SRC := rex/events.go \
 rex/net.go \
 rex/oam.go \
 rex/ppux.go \
 rex/rom.go \
 rex/sram.go \
 rex/vram.go \
 rex/wram.go

main.wasm: main.go go.mod go.sum $(REX_SRC)
#	tinygo build -target wasi -panic trap -o main.wasm
	GOOS=wasip1 GOARCH=wasm gotip build -gcflags="-N -dwarf=true -dwarflocationlists=false" -ldflags="-w=false" -o main.wasm
