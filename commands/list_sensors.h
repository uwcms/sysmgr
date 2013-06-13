#include "../sysmgr.h"
#include "../Crate.h"
#include "../mgmt_protocol.h"

class Command_LIST_SENSORS : public Command {
	protected:
		uint8_t target_crate;
	public:
		Command_LIST_SENSORS(std::string rawcmd, std::vector<std::string> cmd) : Command(rawcmd, cmd) {
			if (cmd.size() != 4) {
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

			std::vector<Sensor*> sensors = card->get_sensors();
			for (std::vector<Sensor*>::iterator it = sensors.begin(); it != sensors.end(); it++) {
				this->writebuf += stdsprintf("%u \"%s\" %c \"%s\" \"%s\"\n",
						this->msgid,
						(*it)->get_name().c_str(),
						(*it)->get_sensor_reading_type(),
						(*it)->get_long_units().c_str(),
						(*it)->get_short_units().c_str());
			}
		};
		// virtual void finalize(Client& client);
};
