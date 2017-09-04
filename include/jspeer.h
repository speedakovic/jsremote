#ifndef JSPEER_H
#define JSPEER_H

#include "jsremote.h"
#include <epoller/sockepoller.h>

#include <string>

/// @brief Receiver for jsremote client application.
class jspeer : private sockepoller
{
public:
	/// @brief Receiver for jspeer instance.
	class receiver
	{
	public:
		/// @brief Destructor.
		virtual ~receiver() = default;

		/// @brief Called if socket was disconnected.
		/// @param jsp jspeer instance
		virtual void disconnected(jspeer *jsp) = 0;

		/// @brief Called if an error was occurred.
		/// @param jsp jspeer instance
		virtual void error(jspeer *jsp) = 0;

		/// @brief Called if joystick event was received
		/// @param jsp jspeer instance
		/// @param ev joystick event
		virtual void event(jspeer *jsp, const jsc_event *ev) = 0;

		/// @brief Called if alive packet was received
		/// @param jsp jspeer instance
		virtual void alive(jspeer *jsp) = 0;

		/// @brief Called if response to 'getaxes' command was received
		/// @param jsp jspeer instance
		/// @param axes number of joystick axes
		virtual void axes(jspeer *jsp, uint8_t axes) = 0;

		/// @brief Called if response to 'getbuttons' command was received
		/// @param jsp jspeer instance
		/// @param buttons number of joystick buttons
		virtual void buttons(jspeer *jsp, uint8_t buttons) = 0;

		/// @brief Called if response to 'getname' command was received
		/// @param jsp jspeer instance
		/// @param name joystick name
		virtual void name(jspeer *jsp, const std::string &name) = 0;
	};

private:
	jspeer::receiver *rcvr;

public:
	/// @brief Constructor.
	/// @param epoller parent epoller
	jspeer(struct epoller *epoller);

	/// @brief Default constructor.
	jspeer();

	/// @brief Destructor
	~jspeer();

	/// @brief Initializes jspeer.
	/// @param fd peer file descriptor
	/// @return @c true if initialization was successful, otherwise @c false
	bool init(int fd);

	/// @brief Cleanups jspeer.
	void cleanup();

	/// @brief Checks if jspeer is initialized.
	/// @return @c true if jspeer is initialized, otherwise @c false
	bool is_initialized();

	/// @brief Gets peer file descriptor.
	/// @return peer file descriptor or -1 if peer is not initialized
	int get_fd();

	/// @brief Sets receiver.
	/// @param rcvr pointer to receiver. Set to zero to unset receiver.
	void set_receiver(jspeer::receiver *rcvr);

	/// @brief Sends 'getaxes' command to remote peer.
	///        Response is received per jspeer::receiver::axes.
	/// @return @c true if command was sent successfully, otherwise @c false
	bool get_axes();

	/// @brief Sends 'getbuttons' command to remote peer.
	///        Response is received per jspeer::receiver::buttons.
	/// @return @c true if command was sent successfully, otherwise @c false
	bool get_buttons();

	/// @brief Sends 'getname' command to remote peer.
	///        Response is received per jspeer::receiver::name.
	/// @return @c true if command was sent successfully, otherwise @c false
	bool get_name();

private:
	virtual int rx(int len);
	virtual int tx(int len);
	virtual int hup();
	virtual int err();

	virtual bool write_datagram(const void *buff, size_t len);
};

#endif // JSPEER_H

