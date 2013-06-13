#include "../sysmgr.h"
#include "../Crate.h"
#include "../mgmt_protocol.h"

class Command_GET_SDR : public Command {
	protected:
		uint8_t target_crate;
	public:
		Command_GET_SDR(std::string rawcmd, std::vector<std::string> cmd) : Command(rawcmd, cmd) {
			if (cmd.size() != 4 && cmd.size() != 5) {
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
			std::string err = "";
			Card *card = Command::parse_valid_fru(THREADLOCAL.crate, cmd[3], &err);
			if (err.length()) {
				this->writebuf += stdsprintf("%u ERROR %s\n", this->msgid, err.c_str());
				return;
			}

			uint8_t sdrbuf[64];
			int sdrbuflen;

			if (cmd.size() == 5) {
				// Get Sensor SDR

				Sensor *sensor = card->get_sensor(cmd[4]);
				if (!sensor) {
					this->writebuf += stdsprintf("%u ERROR Requested sensor not present.\n", this->msgid);
					return;
				}

				sdrbuflen = sensor->get_sdr(sdrbuf, 64);
			}
			else {
				// Get Card SDR

				sdrbuflen = card->get_sdr(sdrbuf, 64);
			}

			if (sdrbuflen < 0) {
				this->writebuf += stdsprintf("%u ERROR Unable to access cached SDR data.\n", this->msgid);
				return;
			}

			this->writebuf += stdsprintf("%u", this->msgid);
			for (int i = 0; i < sdrbuflen; i++)
				this->writebuf += stdsprintf(" %02x", sdrbuf[i]);
			this->writebuf += "\n";
		};
		// virtual void finalize(Client& client);
};
