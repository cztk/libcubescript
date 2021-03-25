#include "cs_bcode.hh"
#include "cs_state.hh"

namespace cubescript {

/* public API impls */

LIBCUBESCRIPT_EXPORT bcode_ref::bcode_ref(bcode *v): p_code(v) {
    bcode_addref(v->get_raw());
}
LIBCUBESCRIPT_EXPORT bcode_ref::bcode_ref(bcode_ref const &v):
    p_code(v.p_code)
{
    bcode_addref(p_code->get_raw());
}

LIBCUBESCRIPT_EXPORT bcode_ref::~bcode_ref() {
    bcode_unref(p_code->get_raw());
}

LIBCUBESCRIPT_EXPORT bcode_ref &bcode_ref::operator=(
    bcode_ref const &v
) {
    bcode_unref(p_code->get_raw());
    p_code = v.p_code;
    bcode_addref(p_code->get_raw());
    return *this;
}

LIBCUBESCRIPT_EXPORT bcode_ref &bcode_ref::operator=(bcode_ref &&v) {
    bcode_unref(p_code->get_raw());
    p_code = v.p_code;
    v.p_code = nullptr;
    return *this;
}

/* private funcs */

struct bcode_hdr {
    internal_state *cs; /* needed to construct the allocator */
    std::size_t asize; /* alloc size of the bytecode block */
    bcode bc; /* BC_INST_START + refcount */
};

/* returned address is the 'init' member of the header */
std::uint32_t *bcode_alloc(internal_state *cs, std::size_t sz) {
    auto a = std_allocator<std::uint32_t>{cs};
    std::size_t hdrs = sizeof(bcode_hdr) / sizeof(std::uint32_t);
    auto p = a.allocate(sz + hdrs - 1);
    bcode_hdr *hdr = reinterpret_cast<bcode_hdr *>(p);
    hdr->cs = cs;
    hdr->asize = sz + hdrs - 1;
    return p + hdrs - 1;
}

/* bc's address must be the 'init' member of the header */
static inline void bcode_free(std::uint32_t *bc) {
    auto *rp = bc + 1 - (sizeof(bcode_hdr) / sizeof(std::uint32_t));
    bcode_hdr *hdr = reinterpret_cast<bcode_hdr *>(rp);
    std_allocator<std::uint32_t>{hdr->cs}.deallocate(rp, hdr->asize);
}

void bcode_incr(std::uint32_t *bc) {
    *bc += 0x100;
}

void bcode_decr(std::uint32_t *bc) {
    *bc -= 0x100;
    if (std::int32_t(*bc) < 0x100) {
        bcode_free(bc);
    }
}

void bcode_addref(std::uint32_t *code) {
    if (!code) {
        return;
    }
    if ((*code & BC_INST_OP_MASK) == BC_INST_START) {
        bcode_incr(code);
        return;
    }
    switch (code[-1]&BC_INST_OP_MASK) {
        case BC_INST_START:
            bcode_incr(&code[-1]);
            break;
        case BC_INST_OFFSET:
            code -= std::ptrdiff_t(code[-1] >> 8);
            bcode_incr(code);
            break;
    }
}

void bcode_unref(std::uint32_t *code) {
    if (!code) {
        return;
    }
    if ((*code & BC_INST_OP_MASK) == BC_INST_START) {
        bcode_decr(code);
        return;
    }
    switch (code[-1]&BC_INST_OP_MASK) {
        case BC_INST_START:
            bcode_decr(&code[-1]);
            break;
        case BC_INST_OFFSET:
            code -= std::ptrdiff_t(code[-1] >> 8);
            bcode_decr(code);
            break;
    }
}

/* empty fallbacks */

static std::uint32_t emptyrets[VAL_ANY] = {
    BC_RET_NULL, BC_RET_INT, BC_RET_FLOAT, BC_RET_STRING
};

empty_block *bcode_init_empty(internal_state *cs) {
    auto a = std_allocator<empty_block>{cs};
    auto *p = a.allocate(VAL_ANY);
    for (std::size_t i = 0; i < VAL_ANY; ++i) {
        p[i].init.init = BC_INST_START + 0x100;
        p[i].code = BC_INST_EXIT | emptyrets[i];
    }
    return p;
}

void bcode_free_empty(internal_state *cs, empty_block *empty) {
    std_allocator<empty_block>{cs}.deallocate(empty, VAL_ANY);
};

bcode *bcode_get_empty(empty_block *empty, std::size_t val) {
    return &empty[val].init + 1;
}

} /* namespace cubescript */
