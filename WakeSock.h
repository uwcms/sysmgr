#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <fcntl.h>

class WakeSock {
	protected:
		int fds[2];
	public:
		WakeSock() {
			socketpair(AF_UNIX, SOCK_STREAM, 0, this->fds);
			fcntl(this->fds[0], F_SETFL, O_NONBLOCK);
			fcntl(this->fds[1], F_SETFL, O_NONBLOCK);
		};
		void wake() {
			write(this->fds[1], "", 1); // Wake the select loop.
			// Nonblocking is fine, as long as ANYTHING is in the queue, select will wake.
		};
		int selectfd() { return this->fds[0]; };
		void clear() {
			char *buf[32];
			while (read(this->fds[0], buf, 32) > 0) {
				// Clear bytes sent to wake us.
			}
		};
};
