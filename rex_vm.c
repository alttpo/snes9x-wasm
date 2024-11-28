
#include <string.h>
#include "rex_vm.h"

void rex_vm_init(struct rex_vm *vm)
{
	vm->p = REX_VM_PRGSZ;
	memset(vm->x, 0, sizeof(vm->x));
	memset(vm->r, 0, sizeof(vm->r));
}

void rex_vm_exec(struct rex_vm *vm)
{
	if (vm->p >= REX_VM_PRGSZ)
	{
		return;
	}

	uint16_t i;

	i = vm->x[vm->p++];
	
}
