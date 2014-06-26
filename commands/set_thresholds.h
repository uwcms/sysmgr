#include "../sysmgr.h"
#include "../Crate.h"
#include "../mgmt_protocol.h"

class Command_SET_THRESHOLDS : public Command {
	protected:
		uint8_t target_crate;
	public:
		Command_SET_THRESHOLDS(std::string rawcmd, std::vector<std::string> cmd) : Command(rawcmd, cmd) {
			if (cmd.size() != 11) {
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
		virtual enum AuthLevel get_required_privilege() { return AUTH_MANAGE; };
		virtual void payload() {
			std::string err = "";
			Card *card = Command::parse_valid_fru(THREADLOCAL.crate, cmd[3], &err);
			if (err.length()) {
				this->writebuf += stdsprintf("%u ERROR %s\n", this->msgid, err.c_str());
				return;
			}

			Sensor *sensor = card->get_sensor(cmd[4]);
			if (!sensor) {
				this->writebuf += stdsprintf("%u ERROR Requested sensor not present.\n", this->msgid);
				return;
			}

			Sensor::threshold_data_t thresholds;

			try {
				thresholds.lnc_set = (cmd[5]  != "-"); if (thresholds.lnc_set) thresholds.lnc = Command::parse_uint8(cmd[5]);
				thresholds.lc_set  = (cmd[6]  != "-"); if (thresholds.lc_set)  thresholds.lc  = Command::parse_uint8(cmd[6]);
				thresholds.lnr_set = (cmd[7]  != "-"); if (thresholds.lnr_set) thresholds.lnr = Command::parse_uint8(cmd[7]);

				thresholds.unc_set = (cmd[8]  != "-"); if (thresholds.unc_set) thresholds.unc = Command::parse_uint8(cmd[8]);
				thresholds.uc_set  = (cmd[9]  != "-"); if (thresholds.uc_set)  thresholds.uc  = Command::parse_uint8(cmd[9]);
				thresholds.unr_set = (cmd[10] != "-"); if (thresholds.unr_set) thresholds.unr = Command::parse_uint8(cmd[10]);

				sensor->set_thresholds(thresholds);
				this->ratelimit();
			}
			catch (ProtocolParseException &e) {
				this->writebuf += stdsprintf("%u ERROR Unparsable argument\n%u\n", this->msgid, this->msgid);
				return;
			}

			// GET THRESHOLDS

			thresholds = sensor->get_thresholds();

			this->writebuf += stdsprintf("%u", this->msgid);
			this->writebuf += (thresholds.lnc_set ? stdsprintf(" 0x%02x", thresholds.lnc) : " -");
			this->writebuf += (thresholds.lc_set  ? stdsprintf(" 0x%02x", thresholds.lc ) : " -");
			this->writebuf += (thresholds.lnr_set ? stdsprintf(" 0x%02x", thresholds.lnr) : " -");

			this->writebuf += (thresholds.unc_set ? stdsprintf(" 0x%02x", thresholds.unc) : " -");
			this->writebuf += (thresholds.uc_set  ? stdsprintf(" 0x%02x", thresholds.uc ) : " -");
			this->writebuf += (thresholds.unr_set ? stdsprintf(" 0x%02x", thresholds.unr) : " -");
			this->writebuf += "\n";
		};
		// virtual void finalize(Client& client);
};
