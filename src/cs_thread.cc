#include "cs_thread.hh"

#include "cs_gen.hh"

namespace cubescript {

thread_state::thread_state(internal_state *cs):
    vmstack{cs}, errbuf{cs}
{
    vmstack.reserve(MAX_ARGUMENTS + MAX_RESULTS);
}

hook_func thread_state::set_hook(hook_func f) {
    auto hk = std::move(call_hook);
    call_hook = std::move(f);
    return hk;
}

} /* namespace cubescript */