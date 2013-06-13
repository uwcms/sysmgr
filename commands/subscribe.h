#include "../sysmgr.h"
#include "../Crate.h"
#include "../mgmt_protocol.h"

class Command_SUBSCRIBE : public Command {
	protected:
		EventFilter filter;
	public:
		Command_SUBSCRIBE(std::string rawcmd, std::vector<std::string> cmd) : Command(rawcmd, cmd) { };
		virtual uint8_t get_required_context() { return 0; };
		virtual enum AuthLevel get_required_privilege() { return AUTH_READ; };
		virtual void payload() { };
		virtual void finalize(Client& client) {
			scope_lock dlock(&this->lock);

			if (cmd.size() != 8) {
				client.write(stdsprintf("%u ERROR Incorrect number of arguments\n%u\n", this->msgid, this->msgid));
				return;
			};
			// -> SUBSCRIBE 0xff 0xff "" "" 0x7fff 0x7fff

			try {
				filter.crate = Command::parse_uint8(cmd[2]);
				filter.fru = Command::parse_uint8(cmd[3]);
				filter.card = cmd[4];
				filter.sensor = cmd[5];
				filter.assertmask = Command::parse_uint16(cmd[6]);
				filter.deassertmask = Command::parse_uint16(cmd[7]);
			}
			catch (ProtocolParseException &e) {
				client.write(stdsprintf("%u ERROR Unparsable argument\n%u\n", this->msgid, this->msgid));
				return;
			}
			uint32_t filterid = client.register_event_filter(filter);
			client.write(stdsprintf("%u FILTER %u %hhu %hhu \"%s\" \"%s\" 0x%hx 0x%hx\n%u\n",
						this->msgid,
						//
						filterid,
						filter.crate,
						filter.fru,
						Command::quote_string(filter.card).c_str(),
						Command::quote_string(filter.sensor).c_str(),
						filter.assertmask,
						filter.deassertmask,
						//
						this->msgid));
		}
};
