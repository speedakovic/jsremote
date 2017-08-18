#include "jspeer.h"

#include <epoller/epoller.h>
#include <epoller/sigepoller.h>
#include <epoller/tcpsepoller.h>

#include <unistd.h>

#include <csignal>

////////////////////////////////////////////////////////////////////////////////
// macros
////////////////////////////////////////////////////////////////////////////////


////////////////////////////////////////////////////////////////////////////////
// variables
////////////////////////////////////////////////////////////////////////////////

static epoller     epoller;
static sigepoller  sc(&epoller);
static tcpsepoller jss(&epoller);

////////////////////////////////////////////////////////////////////////////////
// prototypes
////////////////////////////////////////////////////////////////////////////////

static int sighandler(struct sigepoller *sc, struct signalfd_siginfo *siginfo);
static int jssacc(struct tcpsepoller *tcpsepoller, int fd, const struct sockaddr *addr, const socklen_t *addrlen);

////////////////////////////////////////////////////////////////////////////////
// aux functions
////////////////////////////////////////////////////////////////////////////////


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
	close(fd);

	return 0;
}

////////////////////////////////////////////////////////////////////////////////
// main
////////////////////////////////////////////////////////////////////////////////

int main(int argc, char **argv)
{
	sigset_t    sigset;
	bool        err = false;

	// block signals
	sigemptyset(&sigset);
	sigaddset(&sigset, SIGTERM);
	sigaddset(&sigset, SIGINT);
	sigaddset(&sigset, SIGQUIT);
	sigaddset(&sigset, SIGUSR1);
	sigaddset(&sigset, SIGUSR2);
	sigaddset(&sigset, SIGPIPE);
	sigprocmask(SIG_BLOCK, &sigset, NULL);

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
	if (!jss.socket(AF_INET, "", 55555)) {
		err = true;
		goto unwind_sc;
	}
	jss._acc = &jssacc;

	// enter the loop
	std::cout << "waiting for signal... [TERM, INT, QUIT]" << std::endl;
	err = !epoller.loop();

	// cleanups

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

