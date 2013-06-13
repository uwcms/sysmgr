#include "../sysmgr.h"
#include "../Crate.h"
#include "../mgmt_protocol.h"

class Command_UNSUBSCRIBE : public Command {
	protected:
		EventFilter filter;
	public:
		Command_UNSUBSCRIBE(std::string rawcmd, std::vector<std::string> cmd) : Command(rawcmd, cmd) { };
		virtual uint8_t get_required_context() { return 0; };
		virtual enum AuthLevel get_required_privilege() { return AUTH_READ; };
		virtual void payload() { };
		virtual void finalize(Client& client) {
			scope_lock dlock(&this->lock);

			if (cmd.size() != 3) {
				client.write(stdsprintf("%u ERROR Incorrect number of arguments\n%u\n", this->msgid, this->msgid));
				return;
			};
			// -> UNSUBSCRIBE 1

			try {
				client.unregister_event_filter(Command::parse_uint32(cmd[2]));
			}
			catch (ProtocolParseException &e) {
				client.write(stdsprintf("%u ERROR Unparsable argument\n%u\n", this->msgid, this->msgid));
				return;
			}
			client.write(stdsprintf("%u UNSUBSCRIBED\n%u\n", this->msgid, this->msgid));
		}
};
