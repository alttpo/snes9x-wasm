
#include <string.h>
#include <stdbool.h>
#include "rex_vm.h"
#include "mpack/mpack-expect.h"

void rex_vm_init(struct rex_vm *vm)
{
	memset(vm->m, 0, sizeof(vm->m));
}

void rex_vm_exec(struct rex_vm *vm)
{
	
}
