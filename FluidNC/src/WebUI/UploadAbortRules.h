// Copyright (c) 2026 Bantam Tools
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

// The arduino-esp32 fork's multipart parser (bantamtools/arduino-esp32 24ac5642,
// fix/webserver-multipart-exit) sends UPLOAD_FILE_ABORTED on every failed parse
// exit, so ABORTED can now arrive AFTER a successful UPLOAD_FILE_END when a
// later part of the same request fails to parse. These decide when that
// ABORTED is only an echo of a part that has already finished and must not
// undo it. Kept free of Arduino types so the host tests can cover them.

namespace WebUI {
    // v1 /upload: ABORTED with no file open means the file was already closed
    // and kept by END. There is nothing to remove; marking the upload FAILED
    // would make the next unrelated v1 request report "Upload failed".
    inline bool v1AbortFollowsEnd(bool fileOpen) { return !fileOpen; }

    // /updatefw: ABORTED after Update.end(true) succeeded (the Updater is
    // disarmed and the new image is already the boot image) must keep the
    // SUCCESSFUL status, so the client is told the truth and the device
    // restarts into the new firmware .
    inline bool updateAbortFollowsSuccess(bool updateSucceeded, bool updaterArmed) { return updateSucceeded && !updaterArmed; }
}
