#ifndef JSPEER_H
#define JSPEER_H

#include "jsremote.h"
#include <epoller/sockepoller.h>
#include <map>

/// @brief Receiver for jsremote client application.
class jspeer : private sockepoller
{
public:
	/// @brief Constructor.
	/// @param epoller parent epoller
	jspeer(struct epoller *epoller);

	/// @brief Default constructor.
	jspeer();

	/// @brief Destructor
	~jspeer();

	bool init(int fd);
	void cleanup();
};

#endif // JSPEER_H

