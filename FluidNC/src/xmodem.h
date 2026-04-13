#pragma once

#include "Channel.h"
#include "FileStream.h"

int xmodemReceive(Channel* serial, FileStream* outfile);
int xmodemTransmit(Channel* serial, FileStream* infile);

extern volatile size_t xmodem_bytes_received;
