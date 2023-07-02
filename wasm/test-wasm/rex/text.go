package rex

import "unsafe"

//go:wasmimport rex log_trace
func trace_printf(flags uint32, b unsafe.Pointer, l uint32)

//go:wasmimport rex log_stdout
func stdout_write(b unsafe.Pointer, l uint32)

//go:wasmimport rex log_stderr
func stderr_write(b unsafe.Pointer, l uint32)

type stdout struct{}

var Stdout stdout

func (_ stdout) WriteString(s string) (n int, err error) {
	p := ([]byte)(s)
	stdout_write(unsafe.Pointer(&p[0]), uint32(len(p)))
	return len(p), nil
}

func (_ stdout) Write(p []byte) (n int, err error) {
	stdout_write(unsafe.Pointer(&p[0]), uint32(len(p)))
	return len(p), nil
}

type stderr struct{}

var Stderr stderr

func (_ stderr) Write(p []byte) (n int, err error) {
	stderr_write(unsafe.Pointer(&p[0]), uint32(len(p)))
	return len(p), nil
}

func (_ stderr) WriteString(s string) (n int, err error) {
	p := ([]byte)(s)
	stderr_write(unsafe.Pointer(&p[0]), uint32(len(p)))
	return len(p), nil
}
