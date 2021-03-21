#ifndef LIBCUBESCRIPT_CUBESCRIPT_HH
#define LIBCUBESCRIPT_CUBESCRIPT_HH

#include <cstring>
#include <cstddef>
#include <vector>
#include <optional>
#include <functional>
#include <type_traits>
#include <algorithm>
#include <memory>
#include <utility>
#include <span>

#include "cubescript_conf.hh"

#if defined(__CYGWIN__) || (defined(_WIN32) && !defined(_XBOX_VER))
#  ifdef LIBCUBESCRIPT_DLL
#    ifdef LIBCUBESCRIPT_BUILD
#      define LIBCUBESCRIPT_EXPORT __declspec(dllexport)
#    else
#      define LIBCUBESCRIPT_EXPORT __declspec(dllimport)
#    endif
#  else
#    define LIBCUBESCRIPT_EXPORT
#  endif
#  define LIBCUBESCRIPT_LOCAL
#else
#  if defined(__GNUC__) && (__GNUC__ >= 4)
#    define LIBCUBESCRIPT_EXPORT __attribute__((visibility("default")))
#    define LIBCUBESCRIPT_LOCAL __attribute__((visibility("hidden")))
#  else
#    define LIBCUBESCRIPT_EXPORT
#    define LIBCUBESCRIPT_LOCAL
#  endif
#endif

namespace cscript {

static_assert(std::is_integral_v<cs_int>, "cs_int must be integral");
static_assert(std::is_signed_v<cs_int>, "cs_int must be signed");
static_assert(std::is_floating_point_v<cs_float>, "cs_float must be floating point");

struct cs_internal_error: std::runtime_error {
    using std::runtime_error::runtime_error;
};

template<typename R, typename ...A>
struct cs_callable {
private:
    struct base {
        base(base const &);
        base &operator=(base const &);

    public:
        base() {}
        virtual ~base() {}
        virtual void move_to(base *) = 0;
        virtual R operator()(A &&...args) = 0;
    };

    template<typename F>
    struct store: base {
        explicit store(F &&f): p_stor{std::move(f)} {}

        virtual void move_to(base *p) {
            ::new (p) store{std::move(p_stor)};
        }

        virtual R operator()(A &&...args) {
            return std::invoke(
                *reinterpret_cast<F *>(&p_stor), std::forward<A>(args)...
            );
        }

    private:
        F p_stor;
    };

    using alloc_f = void *(*)(void *, void *, std::size_t, std::size_t);

    struct f_alloc {
        alloc_f af;
        void *ud;
        size_t asize;
    };

    std::aligned_storage_t<sizeof(void *) * 4> p_stor;
    base *p_func;

    static inline base *as_base(void *p) {
        return static_cast<base *>(p);
    }

    template<typename T>
    static inline bool f_not_null(T const &) { return true; }

    template<typename T>
    static inline bool f_not_null(T *p) { return !!p; }

    template<typename CR, typename C>
    static inline bool f_not_null(CR C::*p) { return !!p; }

    template<typename T>
    static inline bool f_not_null(cs_callable<T> const &f) { return !!f; }

    bool small_storage() {
        return (static_cast<void *>(p_func) == &p_stor);
    }

    void cleanup() {
        if (!p_func) {
            return;
        }
        p_func->~base();
        if (!small_storage()) {
            auto &ad = *reinterpret_cast<f_alloc *>(&p_stor);
            ad.af(ad.ud, p_func, ad.asize, 0);
        }
    }

public:
    cs_callable() noexcept: p_func{nullptr} {}
    cs_callable(std::nullptr_t) noexcept: p_func{nullptr} {}
    cs_callable(std::nullptr_t, alloc_f, void *) noexcept: p_func{nullptr} {}

    cs_callable(cs_callable &&f) noexcept {
        if (!f.p_func) {
            p_func = nullptr;
        } else if (f.small_storage()) {
            p_func = as_base(&p_stor);
            f.p_func->move_to(p_func);
        } else {
            p_func = f.p_func;
            f.p_func = nullptr;
        }
    }

    template<typename F>
    cs_callable(F func, alloc_f af, void *ud) {
        if (!f_not_null(func)) {
            return;
        }
        if constexpr (sizeof(store<F>) <= sizeof(p_stor)) {
            auto *p = static_cast<void *>(&p_stor);
            p_func = ::new (p) store<F>{std::move(func)};
        } else {
            auto &ad = *reinterpret_cast<f_alloc *>(&p_stor);
            ad.af = af;
            ad.ud = ud;
            ad.asize = sizeof(store<F>);
            p_func = static_cast<store<F> *>(
                af(ud, nullptr, 0, sizeof(store<F>))
            );
            try {
                new (p_func) store<F>{std::move(func)};
            } catch (...) {
                af(ud, p_func, sizeof(store<F>), 0);
                throw;
            }
        }
    }

    cs_callable &operator=(cs_callable const &) = delete;

    cs_callable &operator=(cs_callable &&f) noexcept {
        cleanup();
        if (f.p_func == nullptr) {
            p_func = nullptr;
        } else if (f.small_storage()) {
            p_func = as_base(&p_stor);
            f.p_func->move_to(p_func);
        } else {
            p_func = f.p_func;
            f.p_func = nullptr;
        }
        return *this;
    }

    cs_callable &operator=(std::nullptr_t) noexcept {
        cleanup();
        p_func = nullptr;
        return *this;
    }

    template<typename F>
    cs_callable &operator=(F &&func) {
        cs_callable{std::forward<F>(func)}.swap(*this);
        return *this;
    }

    ~cs_callable() {
        cleanup();
    }

    void swap(cs_callable &f) noexcept {
        std::aligned_storage_t<sizeof(p_stor)> tmp_stor;
        if (small_storage() && f.small_storage()) {
            auto *t = as_base(&tmp_stor);
            p_func->move_to(t);
            p_func->~base();
            p_func = nullptr;
            f.p_func->move_to(as_base(&p_stor));
            f.p_func->~base();
            f.p_func = nullptr;
            p_func = as_base(&p_stor);
            t->move_to(as_base(&f.p_stor));
            t->~base();
            f.p_func = as_base(&f.p_stor);
        } else if (small_storage()) {
            /* copy allocator address/size */
            memcpy(&tmp_stor, &f.p_stor, sizeof(tmp_stor));
            p_func->move_to(as_base(&f.p_stor));
            p_func->~base();
            p_func = f.p_func;
            f.p_func = as_base(&f.p_stor);
            memcpy(&p_stor, &tmp_stor, sizeof(tmp_stor));
        } else if (f.small_storage()) {
            /* copy allocator address/size */
            memcpy(&tmp_stor, &p_stor, sizeof(tmp_stor));
            f.p_func->move_to(as_base(&p_stor));
            f.p_func->~base();
            f.p_func = p_func;
            p_func = as_base(&p_stor);
            memcpy(&f.p_stor, &tmp_stor, sizeof(tmp_stor));
        } else {
            /* copy allocator address/size */
            memcpy(&tmp_stor, &p_stor, sizeof(tmp_stor));
            memcpy(&p_stor, &f.p_stor, sizeof(tmp_stor));
            memcpy(&f.p_stor, &tmp_stor, sizeof(tmp_stor));
            std::swap(p_func, f.p_func);
        }
    }

    explicit operator bool() const noexcept {
        return !!p_func;
    }

    R operator()(A ...args) {
        return (*p_func)(std::forward<A>(args)...);
    }
};

using cs_alloc_cb = void *(*)(void *, void *, size_t, size_t);

struct cs_state;
struct cs_ident;
struct cs_value;
struct cs_var;

using cs_hook_cb    = cs_callable<void, cs_state &>;
using cs_var_cb     = cs_callable<void, cs_state &, cs_ident &>;
using cs_vprint_cb  = cs_callable<void, cs_state const &, cs_var const &>;
using cs_command_cb = cs_callable<
    void, cs_state &, std::span<cs_value>, cs_value &
>;

enum {
    CS_IDF_PERSIST    = 1 << 0,
    CS_IDF_OVERRIDE   = 1 << 1,
    CS_IDF_HEX        = 1 << 2,
    CS_IDF_READONLY   = 1 << 3,
    CS_IDF_OVERRIDDEN = 1 << 4,
    CS_IDF_UNKNOWN    = 1 << 5,
    CS_IDF_ARG        = 1 << 6
};

struct cs_bcode;
struct cs_shared_state;
struct cs_ident_impl;

struct LIBCUBESCRIPT_EXPORT cs_bcode_ref {
    cs_bcode_ref():
        p_code(nullptr)
    {}
    cs_bcode_ref(cs_bcode *v);
    cs_bcode_ref(cs_bcode_ref const &v);
    cs_bcode_ref(cs_bcode_ref &&v):
        p_code(v.p_code)
    {
        v.p_code = nullptr;
    }

    ~cs_bcode_ref();

    cs_bcode_ref &operator=(cs_bcode_ref const &v);
    cs_bcode_ref &operator=(cs_bcode_ref &&v);

    operator bool() const { return p_code != nullptr; }
    operator cs_bcode *() const { return p_code; }

private:
    cs_bcode *p_code;
};

LIBCUBESCRIPT_EXPORT bool cs_code_is_empty(cs_bcode *code);

struct LIBCUBESCRIPT_EXPORT cs_strref {
    friend struct cs_value;
    /* FIXME: eliminate this */
    friend inline cs_strref cs_make_strref(char const *p, cs_shared_state &cs);

    cs_strref() = delete;
    cs_strref(cs_shared_state &cs, std::string_view str);
    cs_strref(cs_state &cs, std::string_view str);

    cs_strref(cs_strref const &ref);

    ~cs_strref();

    cs_strref &operator=(cs_strref const &ref);

    operator std::string_view() const;

    std::size_t size() const {
        return std::string_view{*this}.size();
    }
    std::size_t length() const {
        return std::string_view{*this}.length();
    }

    char const *data() const {
        return std::string_view{*this}.data();
    }

    bool operator==(cs_strref const &s) const;

private:
    /* for internal use only */
    cs_strref(char const *p, cs_shared_state &cs);

    cs_shared_state *p_state;
    char const *p_str;
};

enum class cs_value_type {
    NONE = 0, INT, FLOAT, STRING, CODE, IDENT
};

struct LIBCUBESCRIPT_EXPORT cs_value {
    cs_value() = delete;
    ~cs_value();

    cs_value(cs_state &);
    cs_value(cs_shared_state &);

    cs_value(cs_value const &);
    cs_value(cs_value &&v);

    cs_value &operator=(cs_value const &);
    cs_value &operator=(cs_value &&);

    cs_value_type get_type() const;

    void set_int(cs_int val);
    void set_float(cs_float val);
    void set_str(std::string_view val);
    void set_str(cs_strref const &val);
    void set_none();
    void set_code(cs_bcode *val);
    void set_ident(cs_ident *val);

    cs_strref get_str() const;
    cs_int get_int() const;
    cs_float get_float() const;
    cs_bcode *get_code() const;
    cs_ident *get_ident() const;
    void get_val(cs_value &r) const;

    bool get_bool() const;

    void force_none();
    cs_float force_float();
    cs_int force_int();
    std::string_view force_str();

    bool code_is_empty() const;

private:
    template<typename T>
    struct stor_t {
        cs_shared_state *state;
        T val;
    };

    cs_shared_state *state() const {
        return reinterpret_cast<stor_t<void *> const *>(&p_stor)->state;
    }

    std::aligned_union_t<1,
        stor_t<cs_int>,
        stor_t<cs_float>,
        stor_t<void *>,
        cs_strref
    > p_stor;
    cs_value_type p_type;
};

struct cs_ident_stack {
    cs_value val_s;
    cs_ident_stack *next;

    cs_ident_stack(cs_state &cs): val_s{cs}, next{nullptr} {}
};

struct cs_error;
struct cs_gen_state;

enum class cs_ident_type {
    IVAR = 0, FVAR, SVAR, COMMAND, ALIAS, SPECIAL
};

struct cs_var;
struct cs_ivar;
struct cs_fvar;
struct cs_svar;
struct cs_alias;
struct cs_command;

struct LIBCUBESCRIPT_EXPORT cs_ident {
    int get_raw_type() const;
    cs_ident_type get_type() const;
    std::string_view get_name() const;
    int get_flags() const;
    int get_index() const;

    bool is_alias() const;
    cs_alias *get_alias();
    cs_alias const *get_alias() const;

    bool is_command() const;
    cs_command *get_command();
    cs_command const *get_command() const;

    bool is_special() const;

    bool is_var() const;
    cs_var *get_var();
    cs_var const *get_var() const;

    bool is_ivar() const;
    cs_ivar *get_ivar();
    cs_ivar const *get_ivar() const;

    bool is_fvar() const;
    cs_fvar *get_fvar();
    cs_fvar const *get_fvar() const;

    bool is_svar() const;
    cs_svar *get_svar();
    cs_svar const *get_svar() const;

protected:
    cs_ident() = default;

private:
    friend struct cs_state;

    cs_ident_impl *p_impl{};
};

struct LIBCUBESCRIPT_EXPORT cs_var: cs_ident {
protected:
    cs_var() = default;
};

struct LIBCUBESCRIPT_EXPORT cs_ivar: cs_var {
    cs_int get_val_min() const;
    cs_int get_val_max() const;

    cs_int get_value() const;
    void set_value(cs_int val);

protected:
    cs_ivar() = default;
};

struct LIBCUBESCRIPT_EXPORT cs_fvar: cs_var {
    cs_float get_val_min() const;
    cs_float get_val_max() const;

    cs_float get_value() const;
    void set_value(cs_float val);

protected:
    cs_fvar() = default;
};

struct LIBCUBESCRIPT_EXPORT cs_svar: cs_var {
    cs_strref get_value() const;
    void set_value(cs_strref val);

protected:
    cs_svar() = default;
};

struct LIBCUBESCRIPT_EXPORT cs_alias: cs_ident {
    cs_value get_value() const;
    void get_cval(cs_value &v) const;

protected:
    cs_alias() = default;
};

struct cs_command: cs_ident {
    std::string_view get_args() const;
    int get_num_args() const;

protected:
    cs_command() = default;
};

struct cs_ident_link;

enum {
    CS_LIB_MATH   = 1 << 0,
    CS_LIB_STRING = 1 << 1,
    CS_LIB_LIST   = 1 << 2,
    CS_LIB_ALL    = 0b111
};

enum class cs_loop_state {
    NORMAL = 0, BREAK, CONTINUE
};

struct LIBCUBESCRIPT_EXPORT cs_state {
    friend struct cs_error;
    friend struct cs_strman;
    friend struct cs_strref;
    friend struct cs_value;
    friend struct cs_gen_state;
    friend inline cs_shared_state *cs_get_sstate(cs_state &);

    cs_shared_state *p_state;
    cs_ident_link *p_callstack = nullptr;

    int identflags = 0;

    cs_state();
    cs_state(cs_alloc_cb func, void *data);
    virtual ~cs_state();

    cs_state(cs_state const &) = delete;
    cs_state(cs_state &&s) {
        swap(s);
    }

    cs_state &operator=(cs_state const &) = delete;
    cs_state &operator=(cs_state &&s) {
        swap(s);
        s.destroy();
        return *this;
    }

    void destroy();

    void swap(cs_state &s) {
        std::swap(p_state, s.p_state);
        std::swap(p_callstack, s.p_callstack);
        std::swap(identflags, s.identflags);
        std::swap(p_pstate, s.p_pstate);
        std::swap(p_inloop, s.p_inloop);
        std::swap(p_owner, s.p_owner);
        std::swap(p_callhook, s.p_callhook);
    }

    cs_state new_thread();

    template<typename F>
    cs_hook_cb set_call_hook(F &&f) {
        return std::move(set_call_hook(
            cs_hook_cb{std::forward<F>(f), callable_alloc, this}
        ));
    }
    cs_hook_cb const &get_call_hook() const;
    cs_hook_cb &get_call_hook();

    template<typename F>
    cs_vprint_cb set_var_printer(F &&f) {
        return std::move(set_var_printer(
            cs_vprint_cb{std::forward<F>(f), callable_alloc, this}
        ));
    }
    cs_vprint_cb const &get_var_printer() const;

    void init_libs(int libs = CS_LIB_ALL);

    void clear_override(cs_ident &id);
    void clear_overrides();

    cs_ident *new_ident(std::string_view name, int flags = CS_IDF_UNKNOWN);
    cs_ident *force_ident(cs_value &v);

    template<typename F>
    cs_ivar *new_ivar(
        std::string_view name, cs_int m, cs_int x, cs_int v,
        F &&f, int flags = 0
    ) {
        return new_ivar(
            name, m, x, v,
            cs_var_cb{std::forward<F>(f), callable_alloc, this}, flags
        );
    }
    cs_ivar *new_ivar(std::string_view name, cs_int m, cs_int x, cs_int v) {
        return new_ivar(name, m, x, v, cs_var_cb{}, 0);
    }

    template<typename F>
    cs_fvar *new_fvar(
        std::string_view name, cs_float m, cs_float x, cs_float v,
        F &&f, int flags = 0
    ) {
        return new_fvar(
            name, m, x, v,
            cs_var_cb{std::forward<F>(f), callable_alloc, this}, flags
        );
    }
    cs_fvar *new_fvar(
        std::string_view name, cs_float m, cs_float x, cs_float v
    ) {
        return new_fvar(name, m, x, v, cs_var_cb{}, 0);
    }

    template<typename F>
    cs_svar *new_svar(
        std::string_view name, std::string_view v, F &&f, int flags = 0
    ) {
        return new_svar(
            name, v,
            cs_var_cb{std::forward<F>(f), callable_alloc, this}, flags
        );
    }
    cs_svar *new_svar(std::string_view name, std::string_view v) {
        return new_svar(name, v, cs_var_cb{}, 0);
    }

    template<typename F>
    cs_command *new_command(
        std::string_view name, std::string_view args, F &&f
    ) {
        return new_command(
            name, args,
            cs_command_cb{std::forward<F>(f), callable_alloc, this}
        );
    }

    cs_ident *get_ident(std::string_view name);
    cs_alias *get_alias(std::string_view name);
    bool have_ident(std::string_view name);

    std::span<cs_ident *> get_idents();
    std::span<cs_ident const *> get_idents() const;

    void reset_var(std::string_view name);
    void touch_var(std::string_view name);

    void run(cs_bcode *code, cs_value &ret);
    void run(std::string_view code, cs_value &ret);
    void run(std::string_view code, cs_value &ret, std::string_view source);
    void run(cs_ident *id, std::span<cs_value> args, cs_value &ret);

    cs_value run(cs_bcode *code);
    cs_value run(std::string_view code);
    cs_value run(std::string_view code, std::string_view source);
    cs_value run(cs_ident *id, std::span<cs_value> args);

    cs_loop_state run_loop(cs_bcode *code, cs_value &ret);
    cs_loop_state run_loop(cs_bcode *code);

    bool is_in_loop() const {
        return p_inloop;
    }

    void set_alias(std::string_view name, cs_value v);

    void set_var_int(
        std::string_view name, cs_int v,
        bool dofunc = true, bool doclamp = true
    );
    void set_var_float(
        std::string_view name, cs_float v,
        bool dofunc  = true, bool doclamp = true
    );
    void set_var_str(
        std::string_view name, std::string_view v, bool dofunc = true
    );

    void set_var_int_checked(cs_ivar *iv, cs_int v);
    void set_var_int_checked(cs_ivar *iv, std::span<cs_value> args);
    void set_var_float_checked(cs_fvar *fv, cs_float v);
    void set_var_str_checked(cs_svar *fv, std::string_view v);

    std::optional<cs_int> get_var_int(std::string_view name);
    std::optional<cs_float> get_var_float(std::string_view name);
    std::optional<cs_strref> get_var_str(std::string_view name);

    std::optional<cs_int> get_var_min_int(std::string_view name);
    std::optional<cs_int> get_var_max_int(std::string_view name);

    std::optional<cs_float> get_var_min_float(std::string_view name);
    std::optional<cs_float> get_var_max_float(std::string_view name);

    std::optional<cs_strref> get_alias_val(std::string_view name);

    void print_var(cs_var const &v) const;

private:
    cs_hook_cb set_call_hook(cs_hook_cb func);
    cs_vprint_cb set_var_printer(cs_vprint_cb func);

    cs_ivar *new_ivar(
        std::string_view n, cs_int m, cs_int x, cs_int v,
        cs_var_cb f, int flags
    );
    cs_fvar *new_fvar(
        std::string_view n, cs_float m, cs_float x, cs_float v,
        cs_var_cb f, int flags
    );
    cs_svar *new_svar(
        std::string_view n, std::string_view v, cs_var_cb f, int flags
    );

    cs_command *new_command(
        std::string_view name, std::string_view args, cs_command_cb func
    );

    static void *callable_alloc(
        void *data, void *p, std::size_t os, std::size_t ns
    ) {
        return static_cast<cs_state *>(data)->alloc(p, os, ns);
    }

    LIBCUBESCRIPT_LOCAL cs_state(cs_shared_state *s);

    cs_ident *add_ident(cs_ident *id, cs_ident_impl *impl);

    void *alloc(void *ptr, size_t olds, size_t news);

    cs_gen_state *p_pstate = nullptr;
    void *p_errbuf = nullptr;
    int p_inloop = 0;
    bool p_owner = false;

    cs_hook_cb p_callhook;
};

struct cs_stack_state_node {
    cs_stack_state_node const *next;
    cs_ident const *id;
    int index;
};

struct cs_stack_state {
    cs_stack_state() = delete;
    cs_stack_state(cs_state &cs, cs_stack_state_node *nd = nullptr, bool gap = false);
    cs_stack_state(cs_stack_state const &) = delete;
    cs_stack_state(cs_stack_state &&st);
    ~cs_stack_state();

    cs_stack_state &operator=(cs_stack_state const &) = delete;
    cs_stack_state &operator=(cs_stack_state &&);

    cs_stack_state_node const *get() const;
    bool gap() const;

private:
    cs_state &p_state;
    cs_stack_state_node *p_node;
    bool p_gap;
};

struct LIBCUBESCRIPT_EXPORT cs_error {
    friend struct cs_state;

    cs_error() = delete;
    cs_error(cs_error const &) = delete;
    cs_error(cs_error &&v):
        p_errmsg(v.p_errmsg), p_stack(std::move(v.p_stack))
    {}

    std::string_view what() const {
        return p_errmsg;
    }

    cs_stack_state &get_stack() {
        return p_stack;
    }

    cs_stack_state const &get_stack() const {
        return p_stack;
    }

    cs_error(cs_state &cs, std::string_view msg):
        p_errmsg(), p_stack(cs)
    {
        char *sp;
        char *buf = request_buf(cs, msg.size(), sp);
        std::memcpy(buf, msg.data(), msg.size());
        buf[msg.size()] = '\0';
        p_errmsg = std::string_view{sp, buf + msg.size()};
        p_stack = save_stack(cs);
    }

    template<typename ...A>
    cs_error(cs_state &cs, std::string_view msg, A const &...args):
        p_errmsg(), p_stack(cs)
    {
        std::size_t sz = msg.size() + 64;
        char *buf, *sp;
        for (;;) {
            buf = request_buf(cs, sz, sp);
            int written = std::snprintf(buf, sz, msg.data(), args...);
            if (written <= 0) {
                throw cs_internal_error{"format error"};
            } else if (std::size_t(written) <= sz) {
                break;
            }
            sz = std::size_t(written);
        }
        p_errmsg = std::string_view{sp, buf + sz};
        p_stack = save_stack(cs);
    }

private:
    cs_stack_state save_stack(cs_state &cs);
    char *request_buf(cs_state &cs, std::size_t bufs, char *&sp);

    std::string_view p_errmsg;
    cs_stack_state p_stack;
};

struct LIBCUBESCRIPT_EXPORT cs_stacked_value: cs_value {
    cs_stacked_value(cs_state &cs, cs_ident *id = nullptr);
    ~cs_stacked_value();

    cs_stacked_value(cs_stacked_value const &) = delete;
    cs_stacked_value(cs_stacked_value &&) = delete;

    cs_stacked_value &operator=(cs_stacked_value const &) = delete;
    cs_stacked_value &operator=(cs_stacked_value &&v) = delete;

    cs_stacked_value &operator=(cs_value const &v);
    cs_stacked_value &operator=(cs_value &&v);

    bool set_alias(cs_ident *id);
    cs_alias *get_alias() const;
    bool has_alias() const;

    bool push();
    bool pop();

private:
    cs_alias *p_a;
    cs_ident_stack p_stack;
    bool p_pushed;
};

struct LIBCUBESCRIPT_EXPORT cs_list_parser {
    cs_list_parser(cs_state &cs, std::string_view s = std::string_view{}):
        p_state{&cs}, p_input_beg{s.data()}, p_input_end{s.data() + s.size()}
     {}

    void set_input(std::string_view s) {
        p_input_beg = s.data();
        p_input_end = s.data() + s.size();
    }

    std::string_view get_input() const {
        return std::string_view{p_input_beg, p_input_end};
    }

    bool parse();
    std::size_t count();

    cs_strref get_item() const;

    std::string_view get_raw_item() const { return p_item; }
    std::string_view get_quoted_item() const { return p_quoted_item; }

    void skip_until_item();

private:
    cs_state *p_state;
    char const *p_input_beg, *p_input_end;

    std::string_view p_item{};
    std::string_view p_quoted_item{};
};

LIBCUBESCRIPT_EXPORT cs_strref value_list_concat(
    cs_state &cs, std::span<cs_value> vals,
    std::string_view sep = std::string_view{}
);

namespace util {
    template<typename R>
    inline R escape_string(R writer, std::string_view str) {
        *writer++ = '"';
        for (auto c: str) {
            switch (c) {
                case '\n': *writer++ = '^'; *writer++ = 'n'; break;
                case '\t': *writer++ = '^'; *writer++ = 't'; break;
                case '\f': *writer++ = '^'; *writer++ = 'f'; break;
                case  '"': *writer++ = '^'; *writer++ = '"'; break;
                case  '^': *writer++ = '^'; *writer++ = '^'; break;
                default: *writer++ = c; break;
            }
        }
        *writer++ = '"';
        return writer;
    }

    template<typename R>
    inline R  unescape_string(R writer, std::string_view str) {
        for (auto it = str.begin(); it != str.end(); ++it) {
            if (*it == '^') {
                ++it;
                if (it == str.end()) {
                    break;
                }
                switch (*it) {
                    case 'n': *writer++ = '\n'; break;
                    case 't': *writer++ = '\r'; break;
                    case 'f': *writer++ = '\f'; break;
                    case '"': *writer++ = '"'; break;
                    case '^': *writer++ = '^'; break;
                    default: *writer++ = *it; break;
                }
            } else if (*it == '\\') {
                ++it;
                if (it == str.end()) {
                    break;
                }
                char c = *it;
                if ((c == '\r') || (c == '\n')) {
                    if ((c == '\r') && ((it + 1) != str.end())) {
                        if (it[1] == '\n') {
                            ++it;
                        }
                    }
                    continue;
                }
                *writer++ = '\\';
            } else {
                *writer++ = *it;
            }
        }
        return writer;
    }

    LIBCUBESCRIPT_EXPORT char const *parse_string(
        cs_state &cs, std::string_view str, size_t &nlines
    );

    inline char const *parse_string(
        cs_state &cs, std::string_view str
    ) {
        size_t nlines;
        return parse_string(cs, str, nlines);
    }

    LIBCUBESCRIPT_EXPORT char const *parse_word(cs_state &cs, std::string_view str);

    template<typename R>
    inline R print_stack(R writer, cs_stack_state const &st) {
        char buf[32] = {0};
        auto nd = st.get();
        while (nd) {
            auto name = nd->id->get_name();
            *writer++ = ' ';
            *writer++ = ' ';
            if ((nd->index == 1) && st.gap()) {
                *writer++ = '.';
                *writer++ = '.';
            }
            snprintf(buf, sizeof(buf), "%d", nd->index);
            char const *p = buf;
            std::copy(p, p + strlen(p), writer);
            *writer++ = ')';
            std::copy(name.begin(), name.end(), writer);
            nd = nd->next;
            if (nd) {
                *writer++ = '\n';
            }
        }
        return writer;
    }
} /* namespace util */

} /* namespace cscript */

#endif /* LIBCUBESCRIPT_CUBESCRIPT_HH */
