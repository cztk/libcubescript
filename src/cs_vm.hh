#ifndef LIBCUBESCRIPT_VM_HH
#define LIBCUBESCRIPT_VM_HH

#include <cubescript/cubescript.hh>

#include "cs_std.hh"
#include "cs_ident.hh"
#include "cs_thread.hh"

#include <utility>

namespace cubescript {

struct break_exception {
};

struct continue_exception {
};

void exec_command(
    thread_state &ts, command_impl *id, ident *self, any_value *args,
    any_value &res, std::size_t nargs, bool lookup = false
);

any_value exec_alias(
        state &cs,
    thread_state &ts, alias *a, any_value *args,
    std::size_t callargs, alias_stack &astack
);

any_value exec_code_with_args(thread_state &ts, bcode_ref const &body);

std::uint32_t *vm_exec(state &mcs,
    thread_state &ts, std::uint32_t *code, any_value &result
);

} /* namespace cubescript */

#endif /* LIBCUBESCRIPT_VM_HH */
