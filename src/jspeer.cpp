#include "jspeer.h"
#include <iostream>

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
	// TODO:
}

void jspeer::cleanup()
{
	// TODO:
}

