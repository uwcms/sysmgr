#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>


#include "sysmgr.h"
#include "Crate.h"
#include "mgmt_protocol.h"
#include "commandindex.h"

WakeSock wake_socket;

pthread_mutex_t events_mutex = PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP;
std::queue<EventData> events;

/*
 * class Command Methods
 */

void Command::run_here()
{
	{
		scope_lock dlock(&this->lock);
		if (!this->reapable) {
			try {
				this->payload();
			}
			catch (std::exception& e) {
				mprintf("PROTOCOL: Caught System Exception during protocol payload: %s.\n", typeid(typeof(e)).name());
				mprintf("          Line: %s\n", this->rawcmd.c_str());
				this->writebuf = stdsprintf("%u ERROR A system exception occured in command payload execution.\n", this->msgid);
			}
			catch (Sysmgr_Exception& e) {
				if (e.get_message().c_str()[0] != '\0')
					mprintf("PROTOCOL: payload caught %s on %s:%d in %s(): %s\n", e.get_type().c_str(), e.get_file().c_str(), e.get_line(), e.get_func().c_str(), e.get_message().c_str());
				else
					mprintf("PROTOCOL: payload caught %s on %s:%d in %s()\n", e.get_type().c_str(), e.get_file().c_str(), e.get_line(), e.get_func().c_str());
				mprintf("          Line: %s\n", this->rawcmd.c_str());
				this->writebuf = stdsprintf("%u ERROR A system manager exception occured in command payload execution.\n", this->msgid);
			}
		}
		this->reapable = true;
	}
	wake_socket.wake();
}

void Command::payload()
{
	this->writebuf = stdsprintf("%u ERROR \"Unimplemented Command\"\n", this->msgid);
}

void Command::finalize(Client &client)
{
	scope_lock dlock(&this->lock);
	client.write(this->writebuf + stdsprintf("%u\n", this->msgid));
}

uint8_t Command::parse_valid_crate(std::string token, std::string *error)
{
	uint8_t target_crate = 0;
	try {
		target_crate = Command::parse_uint8(token);
	}
	catch (ProtocolParseException &e) {
		if (error)
			*error = "Unable to parse target crate";
		target_crate = 0;
		return 0;
	}

	if (threadlocal.size() < target_crate || !target_crate) {
		if (error)
			*error = "No such target crate";
		target_crate = 0;
		return 0;
	}

	if (!threadlocal[target_crate-1].enabled) {
		target_crate = 0;
		*error = "Target crate is not enabled.  Request will never complete";
		return 0;
	}
	if (error)
		*error = "";

	return target_crate;
}

Card *Command::parse_valid_fru(Crate *crate, std::string token, std::string *error)
{
	uint8_t target_fru = 0;
	try {
		target_fru = Command::parse_uint8(token);
	}
	catch (ProtocolParseException &e) {
		if (error)
			*error = "Unable to parse target FRU";
		return NULL;
	}

	Card *card = THREADLOCAL.crate->get_card(target_fru);

	if (!card) {
		if (error)
			*error = "Target FRU not present";
		return NULL;
	}

	if (error)
		*error = "";
	return card;
}

/*
 * class Client Methods
 */

void Client::finalize_queue()
{
	scope_lock dlock(&this->lock);

	// Finalize all finished commands, so as to fill writequeue before checking its length.

	for (std::vector<Command*>::iterator it = this->command_queue.begin(); it != this->command_queue.end();) {
		if ((*it)->is_reapable()) {
			try {
				(*it)->finalize(*this);
			}
			catch (std::exception& e) {
				mprintf("PROTOCOL: Caught System Exception during protocol finalize: %s.\n", typeid(typeof(e)).name());
				mprintf("          Line: %s\n", (*it)->get_rawcmd().c_str());
				this->write(stdsprintf("%u ERROR A system exception occured\n", (*it)->get_msgid()));
				this->write(stdsprintf("%u\n", (*it)->get_msgid()));
			}
			catch (Sysmgr_Exception& e) {
				if (e.get_message().c_str()[0] != '\0')
					mprintf("PROTOCOL: finalize caught %s on %s:%d in %s(): %s\n", e.get_type().c_str(), e.get_file().c_str(), e.get_line(), e.get_func().c_str(), e.get_message().c_str());
				else
					mprintf("PROTOCOL: finalize caught %s on %s:%d in %s()\n", e.get_type().c_str(), e.get_file().c_str(), e.get_line(), e.get_func().c_str());
				mprintf("          Line: %s\n", (*it)->get_rawcmd().c_str());
				this->write(stdsprintf("%u ERROR A system manager exception occured\n", (*it)->get_msgid()));
				this->write(stdsprintf("%u\n", (*it)->get_msgid()));
			}

			delete *it;
			it = this->command_queue.erase(it);
			continue;
		}
		it++;
	}
}
	
void Client::selected_read()
{
	scope_lock dlock(&this->lock);

	if (this->fd == -1)
		return;

	char inbuf[PROTOCOL_MAX_LINE_LENGTH+1];
	memset(inbuf, 0, sizeof(inbuf));
	ssize_t readbytes = read(this->fd, inbuf, PROTOCOL_MAX_LINE_LENGTH);
	if (readbytes == 0 || (readbytes == -1 && errno != EINTR && errno != EAGAIN)) {
		dmprintf("Socket connection %d closed.\n", this->fd);
		close(this->fd);
		this->fd = -1;
		return;
	}
	this->received_bytes += readbytes;
	if (this->privilege == AUTH_NONE && this->received_bytes > PROTOCOL_MAX_UNAUTH_BYTES) {
		dmprintf("Socket connection %d closed: Exceeded unauthorized bytes limit.\n", this->fd);
		close(this->fd);
		this->fd = -1;
		return;
	}
	this->readbuf += inbuf;

	if (this->readbuf.length() > PROTOCOL_MAX_LINE_LENGTH && this->readbuf.find('\n') == std::string::npos) {
		// Protocol Error.  Maximum Line Length Exceeded.
		dmprintf("Socket connection %d closed: Oversided line read.\n", this->fd);
		close(this->fd);
		this->fd = -1;
		return;
	}

	size_t nextnl;
	while ((nextnl = this->readbuf.find('\n')) != std::string::npos) {
		std::string line = this->readbuf.substr(0, nextnl);
		if (nextnl+1 < this->readbuf.length())
			this->readbuf = this->readbuf.substr(nextnl+1);
		else
			this->readbuf = "";


#ifdef DEBUG_SOCKET_FLOW
		dmprintf("Read[%d]: %s\n", this->fd, line.c_str());
#endif
		this->process_command(line);
	}
}

void Client::selected_write()
{
	scope_lock dlock(&this->lock);

	if (this->fd == -1)
		return;

	ssize_t written = ::write(this->fd, this->writebuf.c_str(), this->writebuf.length());
	if (written == -1 && errno != EINTR && errno != EAGAIN) {
		dmprintf("Socket connection %d closed.\n", this->fd);
		close(this->fd);
		this->fd = -1;
		return;
	}
	if ((size_t)written < this->writebuf.length())
		this->writebuf = this->writebuf.substr(written);
	else
		this->writebuf = "";
}

void Client::write(std::string data)
{
	scope_lock dlock(&this->lock);

#ifdef DEBUG_SOCKET_FLOW
	dmprintf("Writing[%d]: %s\n", this->fd, data.c_str());
#endif

	if (this->fd != -1)
		this->writebuf += data;
	else
		this->writebuf = "";

	wake_socket.wake();
};

void Client::process_command(std::string line)
{
	scope_lock dlock(&this->lock);

	uint32_t msgid = 0;
	std::vector<std::string> cmd;
	try {
		cmd = Command::tokenize(line);
	}
	catch (Command::ProtocolParseException &e) {
		msgid = this->get_next_out_msgid();
		this->write(stdsprintf("%d ERROR Unparsable command string: %s\n%d\n", msgid, e.get_message().c_str(), msgid));
		return;
	}

	if (cmd.size() < 2) {
		msgid = this->get_next_out_msgid();
		this->write(stdsprintf("%d ERROR Invalid command string.  No command specified.\n%d\n", msgid, msgid));
		return;
	}

	std::string stage = "parsing";
	Command *command = NULL;

	try {
		msgid = Command::parse_uint32(cmd[0]);
	}
	catch (Command::ProtocolParseException &e) {
		msgid = this->get_next_out_msgid();
		this->write(stdsprintf("%d ERROR Unparsable command string: %s\n%d\n", msgid, e.get_message().c_str(), msgid));
		return;
	}

	try {

		if (0) { }
#define REGISTER_COMMAND(c) else if (cmd[1] == #c) command = new Command_ ## c(line,cmd)
#include "commandindex.inc"
#undef REGISTER_COMMAND
		else {
			this->write(stdsprintf("%u ERROR Unknown Command: %s\n%u\n", msgid, cmd[1].c_str(), msgid));
			return;
		}

		stage = "authorization";
		if (this->privilege < command->get_required_privilege()) {
			this->write(stdsprintf("%u ERROR Unauthorized\n", msgid));
			this->write(stdsprintf("%u\n", msgid));
			delete command;
			return;
		}

		if (!command->is_reapable()) { // Sometimes errors occur in initialization.
			stage = "dispatch";
			uint8_t thread = command->get_required_context();
			if (thread != 0) {
				threadlocal[thread-1].taskqueue.schedule(0, callback<void>::create<Command, &Command::run_here>(command), NULL);
			}
			else {
				stage = "payload";
				dlock.unlock();
				command->run_here();
				dlock.lock();
			}
		}
		this->command_queue.push_back(command);

		// Done, finalization will be handled in another area.
	}
	catch (std::exception& e) {
		mprintf("PROTOCOL: Caught System Exception during protocol %s: %s.\n", stage.c_str(), typeid(typeof(e)).name());
		mprintf("          Line: %s\n", line.c_str());
		this->write(stdsprintf("%u ERROR A system exception occured\n", msgid));
		this->write(stdsprintf("%u\n", msgid));
		if (command)
			delete command;
	}
	catch (Sysmgr_Exception& e) {
		if (e.get_message().c_str()[0] != '\0')
			mprintf("PROTOCOL: %s caught %s on %s:%d in %s(): %s\n", stage.c_str(), e.get_type().c_str(), e.get_file().c_str(), e.get_line(), e.get_func().c_str(), e.get_message().c_str());
		else
			mprintf("PROTOCOL: %s caught %s on %s:%d in %s()\n", stage.c_str(), e.get_type().c_str(), e.get_file().c_str(), e.get_line(), e.get_func().c_str());

		mprintf("          Line: %s\n", line.c_str());
		this->write(stdsprintf("%u ERROR A system manager exception occured\n", msgid));
		this->write(stdsprintf("%u\n", msgid));
		if (command)
			delete command;
	}
}

void Client::dispatch_event(EventData event) {
	for (std::map<uint32_t,EventFilter>::iterator it = this->event_filters.begin(); it != this->event_filters.end(); it++) {
		if (it->second.match(event)) {
			uint32_t msgid = this->get_next_out_msgid();
			this->write(stdsprintf("%u EVENT %u %hhu %hhu \"%s\" \"%s\" %hhu %hhu\n%u\n",
						msgid,
						it->first,
						event.crate,
						event.fru,
						Command::quote_string(event.card).c_str(),
						Command::quote_string(event.sensor).c_str(),
						(event.assertion ? 1 : 0),
						event.offset,
						msgid
						));
		}
	}
}

/*
 * Protocol Server Reactor
 */

void protocol_server(uint16_t port)
{
	int listenfd;
	std::vector<Client*> clients;
	struct sockaddr_in serv_addr;

	listenfd = socket(AF_INET, SOCK_STREAM, 0);
	if (listenfd < 0) {
		mprintf("Unable to open socket: %d (%s)\n", errno, strerror(errno));
		return;
	}

	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr = INADDR_ANY;
	serv_addr.sin_port = htons(port);

	int optval = 1;
	setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(int));

	if (bind(listenfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
		mprintf("Unable to bind address: %d (%s)\n", errno, strerror(errno));
		return;
	}

	listen(listenfd,5);

	while (1) {
		int maxfd = 0;
		fd_set readfds, writefds;
		FD_ZERO(&readfds);
		FD_ZERO(&writefds);
		FD_SET(listenfd, &readfds);
		if (listenfd > maxfd)
			maxfd = listenfd;
		FD_SET(wake_socket.selectfd(), &readfds);
		if (wake_socket.selectfd() > maxfd)
			maxfd = wake_socket.selectfd();


		for (std::vector<Client*>::iterator it = clients.begin(); it != clients.end(); it++) {
			int fd = (*it)->get_fd();
			if ((*it)->selectable_read()) {
				FD_SET(fd, &readfds);
				if (fd > maxfd)
					maxfd = fd;
			}
			if ((*it)->selectable_write()) {
				FD_SET(fd, &writefds);
				if (fd > maxfd)
					maxfd = fd;
			}
		}

		if (select(maxfd+1, &readfds, &writefds, NULL, NULL) < 1)
			continue;

		wake_socket.clear();

		if (FD_ISSET(listenfd, &readfds)) {
			int clientfd = accept(listenfd, NULL, NULL);
			if (clientfd < 0) {
				mprintf("Accept Error: %d (%s)\n", errno, strerror(errno));
			}
			else {
				clients.push_back(new Client(clientfd));
			}
		}

		{
			scope_lock lock_events(&events_mutex);
			while (!events.empty()) {
				EventData event = events.front();
				events.pop();
				/*
				dmprintf("Event[Q=%u] %02x %02x \"%s\" \"%s\" %u %02x\n",
						events.size(),
						event.crate,
						event.fru,
						event.card.c_str(),
						event.sensor.c_str(),
						event.assertion ? 1 : 0,
						event.offset
						);
				*/
				for (std::vector<Client*>::iterator it = clients.begin(); it != clients.end(); it++)
					(*it)->dispatch_event(event);
			}
		}

		for (std::vector<Client*>::iterator it = clients.begin(); it != clients.end(); ) {
			if ((*it)->selectable_read() && FD_ISSET((*it)->get_fd(), &readfds))
				(*it)->selected_read();
			if ((*it)->selectable_write() && FD_ISSET((*it)->get_fd(), &writefds))
				(*it)->selected_write();

			if ((*it)->closed())
				it = clients.erase(it);
			else
				it++;
		}
	}

	close(listenfd);
}
