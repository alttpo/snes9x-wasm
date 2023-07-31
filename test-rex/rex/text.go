package rex

type stdout struct{}

var Stdout stdout

func (_ stdout) WriteString(s string) (n int, err error) {

	return len(s), nil
}

func (_ stdout) Write(p []byte) (n int, err error) {
	return len(p), nil
}

type stderr struct{}

var Stderr stderr

func (_ stderr) WriteString(s string) (n int, err error) {
	return len(s), nil
}

func (_ stderr) Write(p []byte) (n int, err error) {
	return len(p), nil
}
