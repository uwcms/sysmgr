#include "../sysmgr.h"
#include "../Crate.h"
#include "../mgmt_protocol.h"

class Command_LIST_CARDS : public Command {
	protected:
		uint8_t target_crate;
	public:
		Command_LIST_CARDS(std::string rawcmd, std::vector<std::string> cmd) : Command(rawcmd, cmd) {
			if (cmd.size() != 3) {
				this->writebuf += stdsprintf("%u ERROR Incorrect number of arguments\n", this->msgid);
				this->reapable = true;
				return;
			};

			std::string err = "";
			target_crate = Command::parse_valid_crate(cmd[2], &err);
			if (err.length())
				this->writebuf += stdsprintf("%u ERROR %s\n", this->msgid, err.c_str());

			if (!target_crate)
				this->reapable = true;
		};
		virtual uint8_t get_required_context() { return target_crate; };
		virtual enum AuthLevel get_required_privilege() { return AUTH_READ; };
		virtual void payload() {
			Crate *crate = THREADLOCAL.crate;

			for (int fru = 0; fru < 256; fru++) {
				Card *card = crate->get_card((uint8_t)fru);
				if (!card)
					continue;

				uint8_t hotswap_state = 0xff;

				/*
				 * Presently, we will simply go with 'unreadable' rather than falsifying the result.
				 *

				// NAT WORKAROUND: Hotswap Sensors on MCH are fake and unreadable.
				if (crate->get_mch() == Crate::NAT && card->get_name() == "NAT-MCH-MCMC")
					hotswap_state = 4;
				*/

				HotswapSensor *hotswap = card->get_hotswap_sensor();
				if (hotswap)
					hotswap_state = hotswap->get_state();

				this->writebuf += stdsprintf("%u %hhu %hhu \"%s\"\n",
						this->msgid,
						fru,
						hotswap_state,
						Command::quote_string(card->get_name()).c_str());
			}
		};
		// virtual void finalize(Client& client);
};
