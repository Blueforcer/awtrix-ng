#include "berry.h"

#include "be_map.h"
#include "be_module.h"
#include "be_string.h"
#include "be_vm.h"

// Files the stack top in Berry's loaded-module table and pops it, so a later `import name`
// resolves without a filesystem. No public API does this, hence the internal headers.
void awtrix_module_cache_set(bvm *vm, const char *name)
{
    be_cache_module(vm, be_newstr(vm, name));
    be_pop(vm, 1);
}

// Evicts a cached module so the next import re-runs its source. Berry never invalidates the
// table itself, so reinstalling a module would otherwise keep serving the old one forever.
void awtrix_module_cache_drop(bvm *vm, const char *name)
{
    if (vm->module.loaded) {
        be_map_removestr(vm, vm->module.loaded, be_newstr(vm, name));
    }
}
