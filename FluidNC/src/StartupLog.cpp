#include "StartupLog.h"
#include "StartupLogLines.h"
#include "Protocol.h"  // send_line()

size_t StartupLog::write(uint8_t data) {
    _messages += (char)data;
    return 1;
}
std::string StartupLog::messages() {
    return _messages;
}
void StartupLog::dump(Channel& out) {
    // Walk the log in place: copying all of it into an istringstream needed one
    // heap block the size of the whole log and aborted on bad_alloc when the heap
    // was low (internal tracker). Each line is allocated once and handed to
    // the output task, which frees it.
    forEachStartupLogLine(_messages, [&out](const char* line, size_t len) { send_line(out, new std::string(line, len)); });
}

StartupLog::~StartupLog() {}

StartupLog startupLog("Startup Log");
