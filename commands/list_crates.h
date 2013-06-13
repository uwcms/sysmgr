#include "../sysmgr.h"
#include "../Crate.h"
#include "../mgmt_protocol.h"

class Command_LIST_CRATES : public Command {
	public:
		Command_LIST_CRATES(std::string rawcmd, std::vector<std::string> cmd) : Command(rawcmd, cmd) {
			if (cmd.size() != 2) {
				this->writebuf += stdsprintf("%u ERROR Incorrect number of arguments\n", this->msgid);
				this->reapable = true;
				return;
			};
		};
		virtual uint8_t get_required_context() { return 0; };
		virtual enum AuthLevel get_required_privilege() { return AUTH_READ; };
		virtual void payload() {
			static const char *mchs[2] = { "VadaTech", "NAT" };

			for (std::vector<threadlocaldata_t>::iterator it = threadlocal.begin(); it != threadlocal.end(); it++)
				this->writebuf += stdsprintf("%u %hhu %hhu %s \"%s\"\n",
						this->msgid,
						it->crate->get_number(),				// const
						(it->crate->ctx.sel ? 1 : 0),			// races acceptable
						mchs[it->crate->get_mch()],
						Command::quote_string(it->crate->get_description()).c_str());

			// We use sel, because ipmi and sdr are active when the crate is still being probed.
		};
		// virtual void finalize(Client& client);
};
