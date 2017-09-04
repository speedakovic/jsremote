#include "jspeer.h"
#include <iostream>

#define SOCKET_RX_BUFF_LEN JS_MESSAGE_LENGTH_MAX
#define SOCKET_TX_BUFF_LEN JS_MESSAGE_LENGTH_MAX

#define DBG_PREFIX "jspeer: "

jspeer::jspeer(struct epoller *epoller) : sockepoller(epoller)
{
}

jspeer::jspeer() : jspeer(0)
{
}

jspeer::~jspeer()
{
	cleanup();
}

bool jspeer::init(int fd)
{
	if (!sockepoller::init(fd, SOCKET_RX_BUFF_LEN, SOCKET_TX_BUFF_LEN, true, false, true))
		return false;

	return true;
}

void jspeer::cleanup()
{
	sockepoller::cleanup();
}

bool jspeer::is_initialized()
{
	return fd != -1;
}

int jspeer::get_fd()
{
	return fd;
}

void jspeer::set_receiver(jspeer::receiver *rcvr)
{
	this->rcvr = rcvr;
}

bool jspeer::get_axes()
{
	uint8_t buff[sizeof(jsmessage)];
	jsmessage *msg  = (jsmessage *) buff;

	msg->length  = sizeof buff;
	msg->command = JS_COMMAND_GETAXES;

	return write_datagram((void *)buff, sizeof buff);
}

bool jspeer::get_buttons()
{
	uint8_t buff[sizeof(jsmessage)];
	jsmessage *msg  = (jsmessage *) buff;

	msg->length  = sizeof buff;
	msg->command = JS_COMMAND_GETBUTTONS;

	return write_datagram((void *)buff, sizeof buff);
}

bool jspeer::get_name()
{
	uint8_t buff[sizeof(jsmessage)];
	jsmessage *msg  = (jsmessage *) buff;

	msg->length  = sizeof buff;
	msg->command = JS_COMMAND_GETNAME;

	return write_datagram((void *)buff, sizeof buff);
}

int jspeer::rx(int len)
{
	if (len < 0) {
		std::cerr << DBG_PREFIX"socket error" << std::endl;

		if (rcvr)
			rcvr->error(this);

	} else if (len == 0) {

		if (rcvr)
			rcvr->disconnected(this);

	} else {

		while (linbuff_tord(&rxbuff)) {

			if (linbuff_tord(&rxbuff) < sizeof(jsmessage))
				break;

			jsmessage *msg = (jsmessage *)LINBUFF_RD_PTR(&rxbuff);

			if (linbuff_tord(&rxbuff) < msg->length)
				break;

			if (msg->command == JS_COMMAND_EVENT) {

				jsc_event *data = (jsc_event *) msg->data;

				if (rcvr)
					rcvr->event(this, data);

			} else if (msg->command == JS_COMMAND_ALIVE) {

				if (rcvr)
					rcvr->alive(this);

			} else if (msg->command == (JS_COMMAND_GETAXES | JS_RESPONSE)) {

				jsr_getaxes *data = (jsr_getaxes *) msg->data;

				if (rcvr)
					rcvr->axes(this, data->number);

			} else if (msg->command == (JS_COMMAND_GETBUTTONS | JS_RESPONSE)) {

				jsr_getbuttons *data = (jsr_getbuttons *) msg->data;

				if (rcvr)
					rcvr->buttons(this, data->number);

			} else if (msg->command == (JS_COMMAND_GETNAME | JS_RESPONSE)) {

				jsr_getname *data = (jsr_getname *) msg->data;

				if (rcvr)
					rcvr->name(this, std::string((char *)data->name, data->length));

			} else {
				std::cerr << DBG_PREFIX"unknown command" << std::endl;

				if (rcvr)
					rcvr->error(this);
			}

			linbuff_skip(&rxbuff, msg->length);
		}

		linbuff_compact(&rxbuff);
	}

	return 0;
}

int jspeer::tx(int len)
{
	if (len < 0) {
		std::cerr << "socket error" << std::endl;

		if (rcvr)
			rcvr->error(this);

	} else if (len == 0) {
		std::cerr << "socket unexpected error" << std::endl;

		if (rcvr)
			rcvr->error(this);
	}

	if (!linbuff_tord(&txbuff))
		linbuff_compact(&txbuff);

	return 0;
}

int jspeer::hup()
{
	std::cerr << DBG_PREFIX"hup" << std::endl;

	if (rcvr)
		rcvr->error(this);

	return 0;
}

int jspeer::err()
{
	std::cerr << DBG_PREFIX"err" << std::endl;

	if (rcvr)
		rcvr->error(this);

	return 0;
}

bool jspeer::write_datagram(const void *buff, size_t len)
{
	ssize_t ret = sockepoller::write_dgram(buff, len);

	if (ret < 0) {
		std::cerr << "writing datagram to socket failed, unknown error" << std::endl;
		return false;

	} else if (ret == 0) {
		std::cerr << "writing datagram to socket failed, not enough space" << std::endl;
		return false;

	} else if ((size_t)ret != len) {
		std::cerr << "writing datagram to socket failed, unexpected error" << std::endl;
		return false;

	} else
		return true;
}

