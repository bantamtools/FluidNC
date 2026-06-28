#pragma once

#include "Channel.h"
#include "FileStream.h"

// Returns the number of bytes received on success, or a negative error:
//   -1 canceled by remote, -2 sync error, -3 too many retransmits,
//   -6 write/SD error (e.g. SD full). On any negative result the caller
//   discards the partial file. Write-failure detection lives in
//   XmodemReceiveWriter.
int xmodemReceive(Channel* serial, FileStream* outfile);
int xmodemTransmit(Channel* serial, FileStream* infile);

extern volatile size_t xmodem_bytes_received;
