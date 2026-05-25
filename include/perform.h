#ifndef BASHBEATS_PERFORM_H
#define BASHBEATS_PERFORM_H

/* ── Performance Mode ────────────────────────────────────────────────
 *
 * A standalone full-screen piano keyboard UI for live playing.
 * Uses the shared audio engine for live keyboard playback.
 *
 * Entry point: run_performance_mode()
 *   Called from main() when the user selects Performance Mode on the
 *   intro screen. Runs its own ncurses event loop and returns when the
 *   user presses ESC or Ctrl+C.
 * ─────────────────────────────────────────────────────────────────── */

/* Start the performance mode loop (blocking). */
void run_performance_mode(void);

#endif /* BASHBEATS_PERFORM_H */
