#ifndef JSREMOTE_H
#define JSREMOTE_H

#include <inttypes.h>

#define JS_MESSAGE_LENGTH_MAX  1024u

#define JS_RESPONSE            0x80

#define JS_COMMAND_EVENT       0x01
#define JS_COMMAND_GETAXES     0x02
#define JS_COMMAND_GETBUTTONS  0x03
#define JS_COMMAND_GETNAME     0x04
#define JS_COMMAND_ALIVE       0x08

struct __attribute__((packed)) jsmessage
{
	uint16_t length;
	uint8_t  command;
	uint8_t  data[];
};

struct __attribute__((packed)) jsc_event
{
	uint32_t time;
	int16_t  value;
	uint8_t  type;
	uint8_t  number;
};

struct __attribute__((packed)) jsr_getaxes
{
	uint8_t number;
};

struct __attribute__((packed)) jsr_getbuttons
{
	uint8_t number;
};

struct __attribute__((packed)) jsr_getname
{
	uint8_t length;
	uint8_t name[];
};

#endif // JSREMOTE_H

