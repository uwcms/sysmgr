#include "../sysmgr.h"
#include "../Crate.h"
#include "../mgmt_protocol.h"

class Command_RAW_DIRECT : public Command {
	protected:
		uint8_t target_crate;
	public:
		Command_RAW_DIRECT(std::string rawcmd, std::vector<std::string> cmd) : Command(rawcmd, cmd) {
			dmprintf("Parse RAW_DIRECT\n");
			if (cmd.size() < 7) {
				this->writebuf += stdsprintf("%u ERROR Insufficient arguments\n", this->msgid);
				this->reapable = true;
				return;
			};
			dmprintf("RAW_DIRECT size sufficient\n");

			if (cmd.size() > 101) {
				dmprintf("RAW_DIRECT size large\n");
				this->writebuf += stdsprintf("%u ERROR Command length unsupported\n", this->msgid);
				this->reapable = true;
				return;
			};

			std::string err = "";
			target_crate = Command::parse_valid_crate(cmd[2], &err);
			dmprintf("RAW_DIRECT crate parsed\n");
			if (err.length())
				this->writebuf += stdsprintf("%u ERROR %s\n", this->msgid, err.c_str());

			if (!target_crate)
				this->reapable = true;
			dmprintf("RAW_DIRECT target %d reapable %d\n", target_crate, (this->reapable ? 1 : 0));
		};
		virtual uint8_t get_required_context() { return target_crate; };
		virtual enum AuthLevel get_required_privilege() { return AUTH_RAW; };
		virtual void payload() {
			dmprintf("RAW_DIRECT payload executing\n");
			uint8_t tach;
			uint8_t tata;
			uint8_t netfn;

			uint8_t outbuf[129];
			uint8_t outbuflen = 0;

			uint8_t inbuf[130];
			int     inbuflen = 0;

			try {
				tach = Command::parse_uint8(cmd[3]);
				tata = Command::parse_uint8(cmd[4]);
				netfn = Command::parse_uint8(cmd[5]);

				for (std::vector<std::string>::const_iterator it = cmd.begin()+6; it != cmd.end(); it++)
					outbuf[outbuflen++] = Command::parse_uint8(*it);
			}
			catch (ProtocolParseException &e) {
				this->writebuf += stdsprintf("%u ERROR Unparsable argument\n%u\n", this->msgid, this->msgid);
				return;
			}

			dmprintf("C%d: Sending Raw Direct\n", CRATE_NO);
			if (tata == 0x20)
				inbuflen = ipmi_cmd_raw(THREADLOCAL.crate->ctx.ipmi,
						0,
						netfn,
						outbuf,
						outbuflen,
						inbuf,
						130);
			else
				inbuflen = ipmi_cmd_raw_ipmb(THREADLOCAL.crate->ctx.ipmi,
						tach,
						tata,
						0,
						netfn,
						outbuf,
						outbuflen,
						inbuf,
						130);
			dmprintf("C%d: Sent Raw Direct\n", CRATE_NO);

			if (inbuflen < 0) {
				this->writebuf += stdsprintf("%u ERROR Error sending raw command\n", this->msgid);
				return;
			}

			std::string retbuf = stdsprintf("%u 0x%02x", this->msgid, inbuf[1] /* cmpl_code */);

			if (inbuf[1] /* cmpl_code */ == 0) {
				if (inbuflen < 0) {
					this->writebuf += stdsprintf("%u ERROR Command sent.  Unable to parse response data\n", this->msgid);
					return;
				}

				for (int i = 2; i < inbuflen; i++)
					retbuf += stdsprintf(" 0x%02x", inbuf[i]);
			}

			this->writebuf += retbuf + "\n";

			scope_lock stdout_lock(&stdout_mutex);

			dmprintf("outbuf");
			for (int i = 0; i < outbuflen; i++)
				dmprintf(" %02x", outbuf[i]);
			dmprintf("\n");

			dmprintf("inbuf");
			for (int i = 0; i < inbuflen; i++)
				dmprintf(" %02x", inbuf[i]);
			dmprintf("\n");
		};
		// virtual void finalize(Client& client);
};
