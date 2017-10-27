#include "jsremote.h"

#include <fcntl.h>
#include <unistd.h>
#include <getopt.h>

#include <map>
#include <string>
#include <csignal>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <utility>
#include <iostream>

#include <epoller/epoller.h>
#include <epoller/jsepoller.h>
#include <epoller/sigepoller.h>
#include <epoller/timepoller.h>
#include <epoller/tcpcepoller.h>

////////////////////////////////////////////////////////////////////////////////
// macros
////////////////////////////////////////////////////////////////////////////////

#define JSDEV                  "/dev/input/js0"
#define MON_JOYSTICK_PERIOD_MS 1000u
#define MON_SERVER_PERIOD_MS   1000u
#define MON_ALIVE_PERIOD_MS    0u
#define SOCKET_RX_BUFF_LEN     JS_MESSAGE_LENGTH_MAX
#define SOCKET_TX_BUFF_LEN     JS_MESSAGE_LENGTH_MAX

////////////////////////////////////////////////////////////////////////////////
// variables
////////////////////////////////////////////////////////////////////////////////

static epoller      epoller;
static sigepoller   sc(&epoller);
static jsepoller    js(&epoller);
static timepoller   mon(&epoller);
static tcpcepoller  sock(&epoller);
static bool         sockconnected;

static std::string  jsdev = JSDEV;
static std::string  server_addr;
static uint16_t     server_port;
static size_t       mon_joystick_period_ms = MON_JOYSTICK_PERIOD_MS;
static size_t       mon_server_period_ms = MON_SERVER_PERIOD_MS;
static size_t       mon_alive_period_ms = MON_ALIVE_PERIOD_MS;

static std::map<std::pair<uint8_t, uint8_t>, js_event> initev;

static const char* const short_opts = "ha:p:j:x:y:l:";

static const struct option long_opts[] = {
	{"help",      0, NULL, 'h'},
	{"addr",      1, NULL, 'a'},
	{"port",      1, NULL, 'p'},
	{"jsdev",     1, NULL, 'j'},
	{"jsmon",     1, NULL, 'x'},
	{"servermon", 1, NULL, 'y'},
	{"alive",     1, NULL, 'l'},
	{ NULL,       0, NULL,  0 }
};

////////////////////////////////////////////////////////////////////////////////
// prototypes
////////////////////////////////////////////////////////////////////////////////

static struct timespec* ms2timespec(struct timespec *ts, uint64_t ms);

static bool monitor_joystick();
static bool monitor_server();
static bool monitor_alive();
static void monitor_stop();

static bool joystick_open();
static void joystick_close();
static void joystick_print_info();

static bool socket_connect();
static void socket_close();
static void socket_write_dgram(const void *buff, size_t len);
static void socket_write_event(const struct js_event *event);
static void print_help();

static int sighandler(sigepoller &sender, struct signalfd_siginfo *siginfo);
static int monhandler_joystick(timepoller &sender, uint64_t exp);
static int monhandler_server(timepoller &sender, uint64_t exp);
static int monhandler_alive(timepoller &sender, uint64_t exp);
static int jshandler(jsepoller &sender, struct js_event *event);
static int jserr(fdepoller &sender);
static int sockcon(tcpcepoller &sender, bool connected);
static int sockrx(fdepoller &sender, int len);
static int socktx(fdepoller &sender, int len);
static int sockerr(fdepoller &sender);

////////////////////////////////////////////////////////////////////////////////
// aux functions
////////////////////////////////////////////////////////////////////////////////

static struct timespec* ms2timespec(struct timespec *ts, uint64_t ms)
{
	ts->tv_sec  =  ms / 1000ULL;
	ts->tv_nsec = (ms % 1000ULL) * 1000000ULL;
	return ts;
}

static bool monitor_joystick()
{
	struct timespec ts;

	if (!mon.arm_periodic(ms2timespec(&ts, mon_joystick_period_ms)))
		return false;

	mon._timerhandler = &monhandler_joystick;

	//std::cout << "joystick periodic monitoring active" << std::endl;

	return true;
}

static bool monitor_server()
{
	struct timespec ts;

	if (!mon.arm_oneshot(ms2timespec(&ts, mon_server_period_ms)))
		return false;

	mon._timerhandler = &monhandler_server;

	//std::cout << "server oneshot monitoring active" << std::endl;

	return true;
}

static bool monitor_alive()
{
	if (!mon_alive_period_ms)
		return true;

	struct timespec ts;

	if (!mon.arm_periodic(ms2timespec(&ts, mon_alive_period_ms)))
		return false;

	mon._timerhandler = &monhandler_alive;

	return true;
}

static void monitor_stop()
{
	mon.disarm();
	//std::cout << "monitoring stopped" << std::endl;
}

static bool joystick_open()
{
	if (!js.open(jsdev, 1))
		return false;

	js._err       = &jserr;
	js._hup       = &jserr;
	js._jshandler = &jshandler;

	initev.clear();

	std::cout << "joystick open" << std::endl;

	joystick_print_info();

	return true;
}

static void joystick_close()
{
	if (js.fd == -1)
		return;

	js.close();
	std::cout << "joystick closed" << std::endl;
}

static void joystick_print_info()
{
	size_t   axes = js.get_axes();
	//js_corr *corr = new js_corr[axes];

	std::cout << "axes        : " << axes << std::endl;
	std::cout << "buttons     : " << js.get_buttons() << std::endl;
	std::cout << "version     : " << js.get_version() << std::endl;
	std::cout << "name        : " << js.get_name() << std::endl;
	/*
	std::cout << "corrections : ";

	if (js.get_corr(corr) == 0) {
		printf("\n");
		for (size_t i = 0; i < axes; ++i) {
			printf("corr %lu:\n", i);
			printf("  ");
			for (size_t j = 0; j < 8; ++j)
				printf("%X,", corr[i].coef[j]);
			printf("\n");
			printf("  %X\n", corr[i].prec);
			printf("  %X\n", corr[i].type);
		}
		delete [] corr;
	} else
		printf("<not available>\n");*/
}

static bool socket_connect()
{
	if (!sock.socket(AF_INET, SOCKET_RX_BUFF_LEN, SOCKET_TX_BUFF_LEN)) {
		std::cerr << "creating socket failed" << std::endl;
		return false;
	}

	if (!sock.set_so_tcp_nodelay(true)) {
		std::cerr << "setting socket nodelay failed" << std::endl;
		sock.close();
		return false;
	}

	if (!sock.connect(server_addr, server_port)) {
		std::cerr << "connecting socket failed" << std::endl;
		sock.close();
		return false;
	}

	sock._con   = &sockcon;
	sock._rx    = &sockrx;
	sock._tx    = &socktx;
	sock._hup   = &sockerr;
	sock._err   = &sockerr;

	//std::cout << "socket connecting" << std::endl;

	return true;
}

static void socket_close()
{
	if (sock.fd == -1)
		return;

	sockconnected = false;

	sock.close();
	//std::cout << "socket closed" << std::endl;
}

static void socket_write_dgram(const void *buff, size_t len)
{
	ssize_t ret = sock.write_dgram(buff, len);
	if (ret < 0)
		std::cerr << "writing datagram to socket failed, unknown error" << std::endl;
	else if (ret == 0)
		std::cerr << "writing datagram to socket failed, not enough space" << std::endl;
	else if ((size_t)ret != len)
		std::cerr << "writing datagram to socket failed, unexpected error" << std::endl;
}

static void socket_write_event(const struct js_event *event)
{
	uint8_t buff[sizeof(jsmessage) + sizeof(jsc_event)];
	jsmessage *msg  = (jsmessage *) buff;
	jsc_event *data = (jsc_event *) msg->data;

	msg->length  = sizeof buff;
	msg->command = JS_COMMAND_EVENT;
	data->time   = event->time;
	data->value  = event->value;
	data->type   = event->type;
	data->number = event->number;

	socket_write_dgram(buff, sizeof buff);
}

static void print_help()
{
	std::cout << "usage: jsremote [arguments]"                                                                                                    << std::endl;
	std::cout << "  -h  --help                print this help"                                                                                    << std::endl;
	std::cout << "  -a  --addr <address>      ip address of server"                                                                               << std::endl;
	std::cout << "  -p  --port <port>         tcp port of server"                                                                                 << std::endl;
	std::cout << "  -j  --jsdev <device>      joystick device (default: "                                        << JSDEV                  << ")" << std::endl;
	std::cout << "  -x  --jsmon <period>      joystick monitoring period [ms] (default: "                        << MON_JOYSTICK_PERIOD_MS << ")" << std::endl;
	std::cout << "  -y  --servermon <period>  server monitoring period [ms] (default: "                          << MON_SERVER_PERIOD_MS   << ")" << std::endl;
	std::cout << "  -l  --alive <period>      alive packets period [ms], zero means no alive packets (default: " << MON_ALIVE_PERIOD_MS    << ")" << std::endl;
	std::cout << std::endl;
}

////////////////////////////////////////////////////////////////////////////////
// handlers
////////////////////////////////////////////////////////////////////////////////

static int sighandler(sigepoller &sender, struct signalfd_siginfo *siginfo)
{
	std::cerr << "received signal ";
	switch (siginfo->ssi_signo) {
		case SIGTERM:
			std::cerr << "SIGTERM" << std::endl;
			return 1;
		case SIGINT:
			std::cerr << "SIGINT" << std::endl;
			return 1;
		case SIGQUIT:
			std::cerr << "SIGQUIT" << std::endl;
			return 1;
		case SIGUSR1:
			std::cerr << "SIGUSR1" << std::endl;
			return 0;
		case SIGUSR2:
			std::cerr << "SIGUSR2" << std::endl;
			return 0;
		case SIGPIPE:
			std::cerr << "SIGPIPE" << std::endl;
			return 0;
		default:
			std::cerr << "<unknown>" << std::endl;
			return 0;
	}
}

static int monhandler_joystick(timepoller &sender, uint64_t exp)
{
	if (access(jsdev.c_str(), R_OK) == -1)
		return 0;

	std::cout << "joystick connected" << std::endl;

	if (!joystick_open())
		return 0;

	if (!monitor_server())
		return -1;

	return 0;
}

static int monhandler_server(timepoller &sender, uint64_t exp)
{
	if (!socket_connect())
		return -1;

	return 0;
}

static int monhandler_alive(timepoller &sender, uint64_t exp)
{
	uint8_t buff[sizeof(jsmessage)];
	jsmessage *msg = (jsmessage *) buff;

	msg->length  = sizeof buff;
	msg->command = JS_COMMAND_ALIVE;

	socket_write_dgram(buff, sizeof buff);

	return 0;
}

static int jshandler(jsepoller &sender, struct js_event *event)
{
	printf("js: %10u, %6d, %02X, %02d\n", event->time, event->value, event->type, event->number);

	if (sockconnected)
		socket_write_event(event);

	event->type |= JS_EVENT_INIT;
	initev[std::make_pair(event->type, event->number)] = *event;

	return 0;
}

static int jserr(fdepoller &sender)
{
	socket_close();

	joystick_close();

	if (!monitor_joystick())
		return -1;

	return 0;
}

static int sockcon(tcpcepoller &sender, bool connected)
{
	bool err =false;

	if (connected) {
		std::cout << "socket connected" << std::endl;
		sockconnected = true;

		if (!sock.enable_rx()) {
			std::cerr << "enabling reception on socket failed" << std::endl;
			err = true;
		}

		if (!monitor_alive()) {
			std::cerr << "setting alive timer failed" << std::endl;
			err = true;
		}

		for (const auto &item : initev)
			socket_write_event(&item.second);

	} else {
		//perror("socket connecting failed");
		err = true;
	}

	if (err) {
		socket_close();

		if (!monitor_server())
			return -1;
	}

	return 0;
}

static int sockrx(fdepoller &sender, int len)
{
	bool err = false;

	if (len < 0) {
		std::cerr << "socket error" << std::endl;
		err = true;

	} else if (len == 0) {
		std::cout << "socket disconnected" << std::endl;
		err = true;

	} else {

		while (linbuff_tord(&sock.rxbuff)) {

			if (linbuff_tord(&sock.rxbuff) < sizeof(jsmessage))
				break;

			jsmessage *msg = (jsmessage *)LINBUFF_RD_PTR(&sock.rxbuff);

			if (linbuff_tord(&sock.rxbuff) < msg->length)
				break;

			if (msg->command == JS_COMMAND_GETAXES) {


				uint8_t buff[sizeof(jsmessage) + sizeof(jsr_getaxes)];
				jsmessage   *msg  = (jsmessage *) buff;
				jsr_getaxes *data = (jsr_getaxes *) msg->data;

				msg->length  = sizeof buff;
				msg->command = JS_COMMAND_GETAXES | JS_RESPONSE;
				data->number = js.get_axes();

				socket_write_dgram(buff, sizeof buff);

			} else if (msg->command == JS_COMMAND_GETBUTTONS) {

				uint8_t buff[sizeof(jsmessage) + sizeof(jsr_getbuttons)];
				jsmessage      *msg  = (jsmessage *) buff;
				jsr_getbuttons *data = (jsr_getbuttons *) msg->data;

				msg->length  = sizeof buff;
				msg->command = JS_COMMAND_GETBUTTONS | JS_RESPONSE;
				data->number = js.get_buttons();

				socket_write_dgram(buff, sizeof buff);

			} else if (msg->command == JS_COMMAND_GETNAME) {

				std::string name = js.get_name();

				uint8_t buff[sizeof(jsmessage) + sizeof(jsr_getname) + name.length()];
				jsmessage   *msg  = (jsmessage *) buff;
				jsr_getname *data = (jsr_getname *) msg->data;

				msg->length  = sizeof buff;
				msg->command = JS_COMMAND_GETNAME | JS_RESPONSE;
				data->length = name.length();
				memcpy(data->name, name.c_str(), name.length());

				socket_write_dgram(buff, sizeof buff);

			} else {
				std::cerr << "unkown command" << std::endl;
				err= true;
			}

			linbuff_skip(&sock.rxbuff, msg->length);
		}

		linbuff_compact(&sock.rxbuff);
	}

finish:

	if (err) {
		socket_close();

		if (!monitor_server())
			return -1;
	}

	return 0;
}

static int socktx(fdepoller &sender, int len)
{
	bool err = false;

	if (len < 0) {
		std::cerr << "socket error" << std::endl;
		err = true;

	} else if (len == 0) {
		std::cerr << "socket unexpected error" << std::endl;
		err = true;
	}

	if (!linbuff_tord(&sock.txbuff))
		linbuff_compact(&sock.txbuff);

	if (err) {
		socket_close();

		if (!monitor_server())
			return -1;
	}

	return 0;
}

static int sockerr(fdepoller &sender)
{
	std::cerr << "socket error" << std::endl;
	socket_close();
	return 0;
}

////////////////////////////////////////////////////////////////////////////////
// main
////////////////////////////////////////////////////////////////////////////////

int main(int argc, char **argv)
{
	bool     err = false;
	int      next_opt;
	sigset_t sigset;

	// block signals
	sigemptyset(&sigset);
	sigaddset(&sigset, SIGTERM);
	sigaddset(&sigset, SIGINT);
	sigaddset(&sigset, SIGQUIT);
	sigaddset(&sigset, SIGUSR1);
	sigaddset(&sigset, SIGUSR2);
	sigaddset(&sigset, SIGPIPE);
	sigprocmask(SIG_BLOCK, &sigset, NULL);

	// parse options
	do {
		next_opt = getopt_long(argc, argv, short_opts, long_opts, NULL);
		switch (next_opt) {
			case 'h':
				print_help();
				goto unwind;
			case 'a':
				server_addr = optarg;
				break;
			case 'p':
				server_port = atoi(optarg);
				break;
			case 'j':
				jsdev = optarg;
				break;
			case 'x':
				mon_joystick_period_ms = strtoul(optarg, NULL, 10);
				break;
			case 'y':
				mon_server_period_ms = strtoul(optarg, NULL, 10);
				break;
			case 'l':
				mon_alive_period_ms = strtoul(optarg, NULL, 10);
				break;
			case -1:
				break;
			default:
				std::cerr << "an arguments parsing error encountered" << std::endl;
				print_help();
				err = true;
				goto unwind;
		}
	} while (next_opt != -1);

	// check options
	if (server_addr.empty()) {
		std::cerr << "invalid ip address" << std::endl;
		print_help();
		err = true;
		goto unwind;
	}
	if (!server_port) {
		std::cerr << "invalid port" << std::endl;
		print_help();
		err = true;
		goto unwind;
	}
	if (jsdev.empty()) {
		std::cerr << "invalid joystick device" << std::endl;
		print_help();
		err = true;
		goto unwind;
	}
	if (!mon_joystick_period_ms) {
		std::cerr << "invalid joystick monitoring period" << std::endl;
		print_help();
		err = true;
		goto unwind;
	}
	if (!mon_server_period_ms) {
		std::cerr << "invalid server monitoring period" << std::endl;
		print_help();
		err = true;
		goto unwind;
	}

	// initialize epoller
	if (!epoller.init()) {
		err = true;
		goto unwind;
	}

	// initialize signal catcher
	if (!sc.init(&sigset)) {
		err = true;
		goto unwind_epoller;
	}
	sc._sighandler = &sighandler;

	// initialize joystick/server monitor
	if (!mon.init()) {
		err = true;
		goto unwind_sc;
	}
	if (!monitor_joystick()) {
		err = true;
		goto unwind_mon;
	}

	// enter the loop
	std::cout << "waiting for signal... [TERM, INT, QUIT]" << std::endl;
	err = !epoller.loop();

	// cleanups

	socket_close();
	joystick_close();

unwind_mon:
	mon.cleanup();

unwind_sc:
	sc.cleanup();

unwind_epoller:
	epoller.cleanup();

unwind:
	if (err) {
		std::cout << "finished with error" << std::endl;
		return EXIT_FAILURE;
	} else {
		std::cout << "finished with success" << std::endl;
		return EXIT_SUCCESS;
	}
}

