// Copyright (c) 2026 Bantam Tools
// Use of this source code is governed by a GPLv3 license (see LICENSE).
#include "FileEndOutcome.h"

FileEndOutcome evaluate_file_end(bool saw_program_end, bool ended_midline) {
    if (saw_program_end) {
        // Proper M2/M30 terminator: normal success, nothing to tell the user.
        return FileEndOutcome{ /*mark_completed=*/true, /*job_succeeded=*/true,
                               /*had_error=*/false, /*message=*/nullptr };
    }
    if (ended_midline) {
        // Truncated: ended mid-line with no end-of-program marker. A trailing
        // command may have been dropped, so the file is not marked complete and
        // the job is reported as not succeeded. Still non-fatal.
        return FileEndOutcome{ /*mark_completed=*/false, /*job_succeeded=*/false,
                               /*had_error=*/true,
                               "File ended unexpectedly" };
    }
    // Clean end on a line boundary, but no M2/M30 marker. The drawing ran to
    // completion; only the conventional terminator is missing.
    return FileEndOutcome{ /*mark_completed=*/true, /*job_succeeded=*/true,
                           /*had_error=*/false,
                           "File ended without end marker" };
}
