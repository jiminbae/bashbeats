#ifndef BASHBEATS_PERFORM_H
#define BASHBEATS_PERFORM_H

/* ── Performance Mode ────────────────────────────────────────────────
 *
 * A standalone full-screen piano keyboard UI for live playing.
 * Audio engine integration is intentionally left as stubs —
 * replace the TODO sections in perform.c when connecting a real engine.
 *
 * Entry point: run_performance_mode()
 *   Called from main() when the user selects Performance Mode on the
 *   intro screen. Runs its own ncurses event loop and returns when the
 *   user presses ESC or ^Q.
 * ─────────────────────────────────────────────────────────────────── */

/* Start the performance mode loop (blocking). */
void run_performance_mode(void);

#endif /* BASHBEATS_PERFORM_H */
