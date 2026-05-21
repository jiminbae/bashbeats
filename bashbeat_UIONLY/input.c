#include "input.h"
#include "editor.h"
#include <ncurses.h>

/* ── input_dispatch ──
 * This is a thin wrapper kept separate so input routing logic
 * can be tested independently of the editor draw code.
 * Currently all dispatch is handled inside editor_run(),
 * so this function is a no-op stub that can be expanded later. */
int input_dispatch(int ch)
{
    if (ch == KEY_CTRL_Q) return -1;
    return 0;
}
