#include "../sysmgr.h"
#include "../Crate.h"
#include "../mgmt_protocol.h"

class Command_GET_EVENT_ENABLES : public Command {
	protected:
		uint8_t target_crate;
	public:
		Command_GET_EVENT_ENABLES(std::string rawcmd, std::vector<std::string> cmd) : Command(rawcmd, cmd) {
			if (cmd.size() != 5) {
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

			Sensor *sensor = card->get_sensor(cmd[4]);
			if (!sensor) {
				this->writebuf += stdsprintf("%u ERROR Requested sensor not present.\n", this->msgid);
				return;
			}

			Sensor::event_enable_data_t enables;
#ifdef REMOVE_THIS

			try {
				enables.lnc_set = (cmd[5]  != "-"); if (enables.lnc_set) enables.lnc = Command::parse_uint8(cmd[5]);
				enables.lc_set  = (cmd[6]  != "-"); if (enables.lc_set)  enables.lc  = Command::parse_uint8(cmd[6]);
				enables.lnr_set = (cmd[7]  != "-"); if (enables.lnr_set) enables.lnr = Command::parse_uint8(cmd[7]);

				enables.unc_set = (cmd[8]  != "-"); if (enables.unc_set) enables.unc = Command::parse_uint8(cmd[8]);
				enables.uc_set  = (cmd[9]  != "-"); if (enables.uc_set)  enables.uc  = Command::parse_uint8(cmd[9]);
				enables.unr_set = (cmd[10] != "-"); if (enables.unr_set) enables.unr = Command::parse_uint8(cmd[10]);

				sensor->set_thresholds(enables);
			}
			catch (ProtocolParseException &e) {
				this->writebuf += stdsprintf("%u ERROR Unparsable argument\n%u\n", this->msgid, this->msgid);
				return;
			}
#endif

			enables = sensor->get_event_enables();

			this->writebuf += stdsprintf("%u %u %u 0x%04x 0x%04x\n", this->msgid, (enables.scanning ? 1 : 0), (enables.events ? 1 : 0), enables.assert, enables.deassert);
		};
		// virtual void finalize(Client& client);
};
