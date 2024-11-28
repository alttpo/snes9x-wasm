
#ifndef _REX_H_
#define _REX_H_

#include <stdint.h>
#include "rex_vm.h"

#define REX_VM_COUNT 2

struct rex_state {
	int cycles;
	int next_exec_cycle;
	struct rex_vm vm[REX_VM_COUNT];
};

extern struct rex_state rex;

static void rex_advance_clock(int cycles) {
	rex.cycles += cycles;
	if (rex.cycles < rex.next_exec_cycle) {
		return;
	}

	// execute each VM:
	for (int i = 0; i < REX_VM_COUNT; i++) {
		rex_vm_exec(&rex.vm[i]);
	}

	rex.cycles -= rex.next_exec_cycle;
	// TODO: measure FX Pak MCU clock relative to SNES clock
	rex.next_exec_cycle = 24;
}

#endif
