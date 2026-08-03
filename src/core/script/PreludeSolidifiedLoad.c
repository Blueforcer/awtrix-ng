
#include "berry.h"

#include "be_exec.h"
#include "be_object.h"
#include "be_vm.h"

#include "core/script/PreludeSolidified.h"

// Pushes the prelude's precompiled closure ready for be_pcall, skipping the parser: the
// bytecode is generated at build time by scripts/gen_prelude_solidified.py from Prelude.h.
void awtrix_push_solidified_prelude(bvm *vm)
{
    var_setclosure(vm->top, (void *)&awtrix_prelude_closure);
    be_stackpush(vm);
}
