#ifndef JSREMOTE_H
#define JSREMOTE_H

#include <inttypes.h>

#define JS_MESSAGE_LENGTH_MAX  1024u
#define JS_COMMAND_EVENT       0x01

struct __attribute__((packed)) jsmessage
{
	uint16_t length;
	uint8_t  command;
	uint8_t  data[];
};

struct __attribute__((packed)) jsevent
{
	uint32_t time;
	int16_t  value;
	uint8_t  type;
	uint8_t  number;
};

#endif // JSREMOTE_H

