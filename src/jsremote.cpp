#include "jsremote.h"

#include <fcntl.h>
#include <unistd.h>

#include <map>
#include <string>
#include <csignal>
#include <cstdlib>
#include <cstdio>
#include <utility>
#include <iostream>

#include <epoller/epoller.h>
#include <epoller/jsepoller.h>
#include <epoller/sigepoller.h>
#include <epoller/timepoller.h>
#include <epoller/sockepoller.h>

////////////////////////////////////////////////////////////////////////////////
// macros
////////////////////////////////////////////////////////////////////////////////

#define JSDEV_DEFAULT          "/dev/input/js0"
#define MON_JOYSTICK_PERIOD_MS 1000u
#define MON_SERVER_PERIOD_MS   1000u
#define SERVER_IP              "127.0.0.1"
#define SERVER_PORT            55555
#define SOCKET_RX_BUFF_LEN     JS_MESSAGE_LENGTH_MAX
#define SOCKET_TX_BUFF_LEN     JS_MESSAGE_LENGTH_MAX

////////////////////////////////////////////////////////////////////////////////
// variables
////////////////////////////////////////////////////////////////////////////////

static epoller      epoller;
static sigepoller   sc(&epoller);
static jsepoller    js(&epoller);
static timepoller   mon(&epoller);
static sockepoller  sock(&epoller);
static std::string  jsdev;
static uint32_t    *sockevents;
static bool         sockconnected;

static std::map<std::pair<uint8_t, uint8_t>, js_event> initev;

////////////////////////////////////////////////////////////////////////////////
// prototypes
////////////////////////////////////////////////////////////////////////////////

static struct timespec* ms2timespec(struct timespec *ts, uint64_t ms);

static bool monitor_joystick();
static bool monitor_server();
static void monitor_stop();

static bool joystick_open();
static void joystick_close();
static void joystick_print_info();

static bool socket_connect();
static void socket_close();
static void socket_write_dgram(const void *buff, size_t len);
static void socket_write_event(const struct js_event *event);

static int sighandler(struct sigepoller *sc, struct signalfd_siginfo *siginfo);
static int monhandler_joystick(struct timepoller *timepoller, uint64_t exp);
static int monhandler_server(struct timepoller *timepoller, uint64_t exp);
static int jshandler(struct jsepoller *js, struct js_event *event);
static int jserr(struct fdepoller *fdepoller);
static int sockenter(struct fdepoller *fdepoller, struct epoll_event *revent);
static int sockexit(struct fdepoller *fdepoller, struct epoll_event *revent);
static int sockrx(struct fdepoller *fdepoller, int len);
static int socktx(struct fdepoller *fdepoller, int len);
static int sockerr(struct fdepoller *fdepoller);

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

	if (!mon.arm_periodic(ms2timespec(&ts, MON_JOYSTICK_PERIOD_MS)))
		return false;

	mon._timerhandler = &monhandler_joystick;

	std::cout << "joystick periodic monitoring active" << std::endl;

	return true;
}

static bool monitor_server()
{
	struct timespec ts;

	if (!mon.arm_oneshot(ms2timespec(&ts, MON_SERVER_PERIOD_MS)))
		return false;

	mon._timerhandler = &monhandler_server;

	std::cout << "server oneshot monitoring active" << std::endl;

	return true;
}

static void monitor_stop()
{
	mon.disarm();
	std::cout << "monitoring stopped" << std::endl;
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
	if (!sock.socket(AF_INET, SOCK_STREAM | O_NONBLOCK, 0, SOCKET_RX_BUFF_LEN, SOCKET_TX_BUFF_LEN, false, false, false)) {
		std::cerr << "creating socket failed" << std::endl;
		return false;
	}

	if (!sock.set_so_tcp_nodelay(true)) {
		std::cerr << "setting socket nodelay failed" << std::endl;
		return false;
	}

	if (!sock.connect(SERVER_IP, SERVER_PORT)) {
		std::cerr << "connecting socket failed" << std::endl;
		sock.close();
		return false;
	}

	if (!sock.enable(false, true, true)) {
		std::cerr << "enabling socket failed" << std::endl;
		sock.close();
		return false;
	}

	sock._enter = &sockenter;
	sock._exit  = &sockexit;
	sock._rx    = &sockrx;
	sock._tx    = &socktx;
	sock._hup   = &sockerr;
	sock._err   = &sockerr;

	std::cout << "socket connecting" << std::endl;

	return true;
}

static void socket_close()
{
	if (sock.fd == -1)
		return;

	if (sockevents)
		*sockevents = 0;

	sockconnected = false;

	sock.close();
	std::cout << "socket closed" << std::endl;
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
	uint8_t buff[offsetof(jsmessage, data) + sizeof(jsevent)];
	jsmessage *msg = (jsmessage *) buff;
	jsevent   *evt = (jsevent   *) msg->data;

	msg->length = sizeof buff;
	msg->command = JS_COMMAND_EVENT;
	evt->time    = event->time;
	evt->value   = event->value;
	evt->type    = event->type;
	evt->number  = event->number;

	socket_write_dgram(buff, sizeof buff);
}

////////////////////////////////////////////////////////////////////////////////
// handlers
////////////////////////////////////////////////////////////////////////////////

static int sighandler(struct sigepoller *sc, struct signalfd_siginfo *siginfo)
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

static int monhandler_joystick(struct timepoller *timepoller, uint64_t exp)
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

static int monhandler_server(struct timepoller *timepoller, uint64_t exp)
{
	if (!socket_connect())
		return -1;

	return 0;
}

static int jshandler(struct jsepoller *js, struct js_event *event)
{
	printf("js: %10u, %6d, %02X, %02d\n", event->time, event->value, event->type, event->number);

	if (sockconnected)
		socket_write_event(event);

	event->type |= JS_EVENT_INIT;
	initev[std::make_pair(event->type, event->number)] = *event;

	return 0;
}

static int jserr(struct fdepoller *fdepoller)
{
	socket_close();

	joystick_close();

	if (!monitor_joystick())
		return -1;

	return 0;
}

static int sockenter(struct fdepoller *fdepoller, struct epoll_event *revent)
{
	sockevents = &revent->events;
	return 0;
}

static int sockexit(struct fdepoller *fdepoller, struct epoll_event *revent)
{
	sockevents = 0;
	return 0;
}

static int sockrx(struct fdepoller *fdepoller, int len)
{
	bool err = false;

	if (len < 0) {
		std::cerr << "socket error" << std::endl;
		err = true;

	} else if (len == 0) {
		std::cout << "socket disconnected" << std::endl;
		err = true;

	} else {
		std::cout << "sockrx, len = " << len << std::endl;
	}

	if (err) {
		socket_close();

		if (!monitor_server())
			return -1;
	}

	return 0;
}

static int socktx(struct fdepoller *fdepoller, int len)
{
	bool err = false;

	if (sock.epoll_out_cnt == 1) {
		if (len == 0) {
			std::cout << "socket connected" << std::endl;
			sockconnected = true;

			if (!sock.enable_rx()) {
				std::cerr << "enabling reception on socket failed" << std::endl;
				err = true;
			}

			for (const auto &item : initev)
				socket_write_event(&item.second);

		} else if (len < 0) {
			std::cerr << "socket connecting failed" << std::endl;
			err = true;

		} else {
			std::cerr << "socket unexpected error" << std::endl;
			err = true;
		}

	} else  {
		if (len < 0) {
			std::cerr << "socket error" << std::endl;
			err = true;

		} else if (len == 0) {
			std::cerr << "socket unexpected error" << std::endl;
			err = true;
		}
	}

	if (err) {
		socket_close();

		if (!monitor_server())
			return -1;
	}

	return 0;
}

static int sockerr(struct fdepoller *fdepoller)
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
	bool       err = false;
	sigset_t   sigset;

	// block signals
	sigemptyset(&sigset);
	sigaddset(&sigset, SIGTERM);
	sigaddset(&sigset, SIGINT);
	sigaddset(&sigset, SIGQUIT);
	sigaddset(&sigset, SIGUSR1);
	sigaddset(&sigset, SIGUSR2);
	sigaddset(&sigset, SIGPIPE);
	sigprocmask(SIG_BLOCK, &sigset, NULL);

	// initialize joystick device name
	jsdev = JSDEV_DEFAULT;

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

