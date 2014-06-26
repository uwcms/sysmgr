#include "../sysmgr.h"
#include "../Crate.h"
#include "../mgmt_protocol.h"

class Command_RAW_CARD : public Command {
	protected:
		uint8_t target_crate;
	public:
		Command_RAW_CARD(std::string rawcmd, std::vector<std::string> cmd) : Command(rawcmd, cmd) {
			if (cmd.size() < 6) {
				this->writebuf += stdsprintf("%u ERROR Insufficient arguments\n", this->msgid);
				this->reapable = true;
				return;
			};

			if (cmd.size() > 100) {
				this->writebuf += stdsprintf("%u ERROR Command length unsupported\n", this->msgid);
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
		virtual enum AuthLevel get_required_privilege() { return AUTH_RAW; };
		virtual void payload() {
			std::string err = "";
			Card *card = Command::parse_valid_fru(THREADLOCAL.crate, cmd[3], &err);
			if (err.length()) {
				this->writebuf += stdsprintf("%u ERROR %s\n", this->msgid, err.c_str());
				return;
			}

			fiid_obj_t raw_rq = fiid_obj_create(tmpl_ipmb_msg);
			fiid_obj_t raw_rs = fiid_obj_create(tmpl_ipmb_msg);

			uint8_t netfn;

			try {
				netfn = Command::parse_uint8(cmd[4]);

				uint8_t buf[128];
				uint8_t buflen = 0;
				for (std::vector<std::string>::const_iterator it = cmd.begin()+5; it != cmd.end(); it++)
					buf[buflen++] = Command::parse_uint8(*it);

				if (fiid_obj_set_data(raw_rq, "data", buf, buflen) < 0)
					THROWMSG(IPMI_LibraryError, "fiid_obj_set_data() failed");
			}
			catch (ProtocolParseException &e) {
				this->writebuf += stdsprintf("%u ERROR Unparsable argument\n%u\n", this->msgid, this->msgid);
				return;
			}

			int cmpl_code = THREADLOCAL.crate->send_bridged(0, card->get_bridge_addr(), card->get_channel(), card->get_addr(), netfn, raw_rq, raw_rs);
			this->ratelimit();
			if (cmpl_code < 0) {
				this->writebuf += stdsprintf("%u ERROR Error sending raw command\n", this->msgid);
				return;
			}

			std::string retbuf = stdsprintf("%u 0x%02x", this->msgid, cmpl_code);

			if (cmpl_code == 0) {
				uint8_t msgbuf[256];
				int msglen = fiid_obj_get_data(raw_rs, "data", msgbuf, 256);
				if (msglen < 0) {
					this->writebuf += stdsprintf("%u ERROR Command sent.  Unable to parse response data\n", this->msgid);
					return;
				}

				for (int i = 0; i < msglen; i++)
					retbuf += stdsprintf(" 0x%02x", msgbuf[i]);
			}

			this->writebuf += retbuf + "\n";
		};
		// virtual void finalize(Client& client);
};
