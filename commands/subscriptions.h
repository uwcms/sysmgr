#include "../sysmgr.h"
#include "../Crate.h"
#include "../mgmt_protocol.h"

class Command_SUBSCRIPTIONS : public Command {
	protected:
		EventFilter filter;
	public:
		Command_SUBSCRIPTIONS(std::string rawcmd, std::vector<std::string> cmd) : Command(rawcmd, cmd) { };
		virtual uint8_t get_required_context() { return 0; };
		virtual enum AuthLevel get_required_privilege() { return AUTH_READ; };
		virtual void payload() { };
		virtual void finalize(Client& client) {
			scope_lock dlock(&this->lock);

			if (cmd.size() != 2) {
				client.write(stdsprintf("%u ERROR Incorrect number of arguments\n%u\n", this->msgid, this->msgid));
				return;
			};
			// -> SUBSCRIPTIONS

			std::map<uint32_t,EventFilter> filters = client.get_event_filters();
			for (std::map<uint32_t,EventFilter>::iterator it = filters.begin(); it != filters.end(); it++)
				this->writebuf += stdsprintf("%u FILTER %u %hhu %hhu \"%s\" \"%s\" 0x%hx 0x%hx\n",
							this->msgid,
							//
							it->first,
							it->second.crate,
							it->second.fru,
							Command::quote_string(it->second.card).c_str(),
							Command::quote_string(it->second.sensor).c_str(),
							it->second.assertmask,
							it->second.deassertmask
							);

			this->writebuf += stdsprintf("%u\n", this->msgid);
			client.write(this->writebuf);
		}
};
