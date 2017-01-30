#ifdef CS_REPL_USE_READLINE
#ifndef CS_REPL_HAS_EDIT
#define CS_REPL_HAS_EDIT
/* use the GNU readline library */

#include <string.h>

#include <ostd/string.hh>
#include <ostd/maybe.hh>

#include <readline/readline.h>
#include <readline/history.h>

static CsState *rd_cs = nullptr;

static char *ln_complete_list(char const *buf, int state) {
    static ostd::ConstCharRange cmd;
    static ostd::PointerRange<CsIdent *> itr;

    if (!state) {
        cmd = get_complete_cmd(buf);
        itr = rd_cs->get_idents();
    }

    for (; !itr.empty(); itr.pop_front()) {
        CsIdent *id = itr.front();
        if (!id->is_command()) {
            continue;
        }
        ostd::ConstCharRange idname = id->get_name();
        if (idname.size() <= cmd.size()) {
            continue;
        }
        if (idname.slice(0, cmd.size()) == cmd) {
            itr.pop_front();
            return strdup(idname.data());
        }
    }

    return nullptr;
}

static char **ln_complete(char const *buf, int, int) {
    rl_attempted_completion_over = 1;
    return rl_completion_matches(buf, ln_complete_list);
}

void ln_hint() {
    CsCommand *cmd = get_hint_cmd(*rd_cs, rl_line_buffer);
    if (!cmd) {
        rl_redisplay();
        return;
    }
    std::string old = rl_line_buffer;
    std::string args = old;
    args += " [";
    fill_cmd_args(args, cmd->get_args());
    args += "] ";
    rl_extend_line_buffer(args.size());
    rl_replace_line(args.data(), 0);
    rl_redisplay();
    rl_replace_line(old.data(), 0);
}

static void init_lineedit(CsState &cs, ostd::ConstCharRange) {
    rd_cs = &cs;
    rl_attempted_completion_function = ln_complete;
    rl_redisplay_function = ln_hint;
}

static ostd::Maybe<std::string> read_line(CsState &, CsSvar *pr) {
    auto line = readline(pr->get_value().data());
    if (!line) {
        return std::string();
    }
    std::string ret = line;
    free(line);
    return std::move(ret);
}

static void add_history(CsState &, ostd::ConstCharRange line) {
    /* backed by std::string so it's terminated */
    add_history(line.data());
}

#endif
#endif
