// Copyright (c) 2026 Bantam Tools
// Use of this source code is governed by a GPLv3 license (see LICENSE).
#pragma once

// Decision for how InputFile handles reaching end-of-file. Kept as a pure
// function of two inputs so it can be unit-tested away from the file and
// runtime machinery.
//
//   saw_program_end - an M2/M30 program-end marker was parsed before EOF
//   ended_midline   - the final content byte arrived at EOF with no
//                     terminating newline (a truncated last line)
//
// The combined signal "no marker AND ended mid-line" is the reliable
// truncation test: either alone is weak (complete files may lack a trailing
// newline; fragments may lack a marker), but together they indicate a
// truncated file. Both no-marker cases are non-fatal: the file was read and
// every complete line ran. They differ only in completion marking.
struct FileEndOutcome {
    bool        mark_completed;  // completion-mark the file as done on disk
    bool        job_succeeded;   // report the job to the UI as succeeded
    bool        had_error;       // treat as an error for teardown (skip motion sync)
    const char* message;         // OLED notification text, or nullptr for none
};

FileEndOutcome evaluate_file_end(bool saw_program_end, bool ended_midline);
