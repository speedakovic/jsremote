#include "jspeer.h"

#include <epoller/epoller.h>
#include <epoller/sigepoller.h>
#include <epoller/tcpsepoller.h>

#include <getopt.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <csignal>

////////////////////////////////////////////////////////////////////////////////
// macros
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
// types
////////////////////////////////////////////////////////////////////////////////

class jsp_receiver : public jspeer::receiver
{
public:
	virtual void disconnected(jspeer *jsp)
	{
		std::cout << "peer disconnected" << std::endl;
		int fd = jsp->get_fd();
		jsp->cleanup();
		close(fd);
	};

	virtual void error(jspeer *jsp)
	{
		std::cout << "peer error" << std::endl;
		int fd = jsp->get_fd();
		jsp->cleanup();
		close(fd);
	};

	virtual void event(jspeer *jsp, const jsc_event *ev)
	{
		printf("peer event: %10u, %6d, %02X, %02d\n", ev->time, ev->value, ev->type, ev->number);
	};

	virtual void alive(jspeer *jsp)
	{
		std::cout << "alive" << std::endl;
	};

	virtual void axes(jspeer *jsp, uint8_t axes)
	{
		std::cout << "peer axes: " << (int) axes << std::endl;
	};

	virtual void buttons(jspeer *jsp, uint8_t buttons)
	{
		std::cout << "peer buttons: " << (int) buttons << std::endl;
	};

	virtual void name(jspeer *jsp, const std::string &name)
	{
		std::cout << "peer name: " << name << std::endl;
	};
};

////////////////////////////////////////////////////////////////////////////////
// variables
////////////////////////////////////////////////////////////////////////////////

static epoller      epoller;
static sigepoller   sc(&epoller);
static tcpsepoller  jss(&epoller);
static jspeer       jsp(&epoller);
static jsp_receiver jspr;

static std::string  server_addr;
static uint16_t     server_port;

static const char* const short_opts = "ha:p:";

static const struct option long_opts[] = {
	{"help",      0, NULL, 'h'},
	{"addr",      1, NULL, 'a'},
	{"port",      1, NULL, 'p'},
	{ NULL,       0, NULL,  0 }
};

////////////////////////////////////////////////////////////////////////////////
// prototypes
////////////////////////////////////////////////////////////////////////////////

static void print_help();

static int sighandler(struct sigepoller *sc, struct signalfd_siginfo *siginfo);
static int jssacc(struct tcpsepoller *tcpsepoller, int fd, const struct sockaddr *addr, const socklen_t *addrlen);

////////////////////////////////////////////////////////////////////////////////
// aux functions
////////////////////////////////////////////////////////////////////////////////

static void print_help()
{
	std::cout << "usage: jspeertest [arguments]"                                                      << std::endl;
	std::cout << "  -h  --help                print this help"                                        << std::endl;
	std::cout << "  -a  --addr <address>      ip address to listen on (leave empty to listen on any)" << std::endl;
	std::cout << "  -p  --port <port>         tcp port to listen on"                                  << std::endl;
	std::cout << std::endl;
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

static int jssacc(struct tcpsepoller *tcpsepoller, int fd, const struct sockaddr *addr, const socklen_t *addrlen)
{
	if (fd < 0) {
		std::cout << "client accepting failed" << std::endl;
		return 0;
	}

	std::cout << "client accepted" << std::endl;

	if (jsp.is_initialized()) {
		std::cout << "initialized peer already exists, client closed" << std::endl;
		close(fd);
		return 0;
	}

	if (!jsp.init(fd)) {
		std::cerr << "initializing peer failed" << std::endl;
		close(fd);
		return 0;
	}

	jsp.set_receiver(&jspr);

	std::cout << "peer initialized" << std::endl;

	jsp.get_axes();
	jsp.get_buttons();
	jsp.get_name();

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
	int      fd;

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
	if (!server_port) {
		std::cerr << "invalid port" << std::endl;
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

	// initialize server for jsremote applicatin
	if (!jss.socket(AF_INET, server_addr, server_port)) {
		err = true;
		goto unwind_sc;
	}
	jss._acc = &jssacc;

	// enter the loop
	std::cout << "waiting for signal... [TERM, INT, QUIT]" << std::endl;
	err = !epoller.loop();

	// cleanups

	fd = jsp.get_fd();
	jsp.cleanup();
	close(fd);

//unwind_jss:
	jss.close();

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

