
#ifndef _REX_VM_H_
#define _REX_VM_H_

#include <stdint.h>

#define REX_VM_PRGSZ 128

struct rex_vm {
	uint16_t p;                 // instruction pointer into m[]
	uint16_t r[16];             // general purpose 16-bit registers

	uint16_t x[REX_VM_PRGSZ];   // program instruction memory
};

void rex_vm_exec(struct rex_vm *vm);

/*
  instructions are 16-bit (MSB on left):
  [iooo oooo ssss dddd]
    o: opcode
    s: source register
    d: destination register
    i = 0: no immediate value
    i = 1: followed by 16-bit immediate value

opcodes:
  0: EXIT

  1: READ  byte from mem chip into destination register
  2: WRITE byte   to mem chip from source register

  4: LOAD  load immediate value,                            store to destination register
  5: MOVE  load source register,                            store to destination register

  B: ADD   load source register,                 add immed, store to destination register
  C: SUB   load source register,            subtract immed, store to destination register
  6: AND   load source register,            AND with immed, store to destination register
  7:  OR   load source register,             OR with immed, store to destination register
  8: XOR   load source register,            XOR with immed, store to destination register
  9: SHL   load source register,  shift left by immed bits, store to destination register
  A: SHR   load source register, shift right by immed bits, store to destination register
  D:
  E:
  F:

 10: BEQ
 11: BNE
 12: BLT
 13: BGT
 14: BLE
 15: BGE
 16:

*/

#endif
