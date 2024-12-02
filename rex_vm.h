
#ifndef _REX_VM_H_
#define _REX_VM_H_

/*
REX VM is a tiny stack-based programming language that can be expressed with a LISP-like syntax.

program:
	function-call*

function-call:
	`(` function-name expression* `)`

expression:
	uint |
	named-constant |
	data-pointer |
	byte-sequence |
	function-call

uint              = /  [0-9A-F]+ /                ; up to 32-bit uint
byte-sequence     = / $(_*[0-9A-F][0-9A-F]_*)+ /  ; up to 64 bytes
named-constant    = / #[0-9a-zA-Z_-]+ /           ; compile-time lookup by name and replaced with uint
data-pointer      = / &[0-9A-F]+ /                ; up to 16-bit uint
function-name     = /  [a-z][0-9a-z_-]* /

types:
	u8
	u16
	u24
	u32
	ptr:u16

example program:
```
	(def-vec  &0 10)
	(def-vec &10 10)
	(vec-assign &0 $9C002C_6CEAFF)
	(fn-start)
		(lbl 0)
		(jne (chip-read-u8 #2C00 0) 0)
		(chip-write-vec #2C00 0 &0)
		(rsp-write-vec &0)
		(jmp 0)
	(fn-end)
```

serialized binary format of an expression: (x and y are big-endian with MSB to LSB left-to-right across bytes)
	11xxxxxx [x bytes]                           = inline byte array of x bytes long (0..$3F)
	1000xxxx_yyyyyyyy [x exprs]                  = opcode y (0..$FF) with x argument expressions (0..$F)
	1001xxxx_xxxxxxxx                            = data pointer (0..$FFF)
	10100000_xxxxxxxx_xxxxxxxx                   = data pointer (0..$FFFF)
	0xxxxxxx                                     = uint up to $7F (u8)
	1011xxxx_xxxxxxxx                            = uint up to $FFF (u16)
	10000000_xxxxxxxx_xxxxxxxx                   = uint up to $FFFF (u16)
	10000001_xxxxxxxx_xxxxxxxx_xxxxxxxx          = uint up to $FFFFFF (u24)
	10000010_xxxxxxxx_xxxxxxxx_xxxxxxxx_xxxxxxxx = uint up to $FFFFFFFF (u32)

functions are statically typed:
	(def-vec    addr:ptr cap:u16)
	(vec-assign addr:ptr bytes:u8...)

	(chip-read-u8   chip:u8 offs:u32):u8
	(chip-read-u16  chip:u8 offs:u32):u16
	(chip-read-u24  chip:u8 offs:u32):u24
	(chip-write-u8  chip:u8 offs:u32 src:u8):void
	(chip-write-u16 chip:u8 offs:u32 src:u16):void
	(chip-write-u24 chip:u8 offs:u32 src:u24):void
	(chip-read      chip:u8 offs:u32 len:u16 dest:ptr...):void
	(chip-write     chip:u8 offs:u32 src:ptr...):void

 named-constants:
	 `#WRAM`
	 `#SRAM`
	 `#2C00`

 memory layout:
	 program
	 stack
	 data

*/

#include <stdint.h>

#ifndef REX_VM_MEMSZ
#  define REX_VM_MEMSZ 1024
#endif

#if REX_VM_MEMSZ > 4096
#  error REX_VM_MEMSZ cannot be more than 4096 bytes
#endif

struct rex_vm {
	uint16_t   p;               // program counter as index into m[], starts at 0
	uint16_t   s;               // stack pointer as index into m[], starts at len(m)-1

	uint8_t m[REX_VM_MEMSZ];    // read-write memory
};

void rex_vm_exec(struct rex_vm *vm);

enum rex_vmop {
	REX_VMOP_RETURN = 0,
	REX_VMOP_GOTO,
	REX_VMOP_IF,
	
	// TBD...

	REX_VMOP_NOT,
	REX_VMOP_AND,
	REX_VMOP_OR,
	REX_VMOP_XOR,
	REX_VMOP_SHL,
	REX_VMOP_SHR,
	REX_VMOP_EQ,
	REX_VMOP_NE,

	REX_VMOP_ADD,
	REX_VMOP_SUB,

	REX_VMOP_GT_SIGNED,
	REX_VMOP_LT_SIGNED,
	REX_VMOP_GE_SIGNED,
	REX_VMOP_LE_SIGNED,

	REX_VMOP_GT_UNSIGNED,
	REX_VMOP_LT_UNSIGNED,
	REX_VMOP_GE_UNSIGNED,
	REX_VMOP_LE_UNSIGNED,
};

#endif
