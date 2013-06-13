#include "../sysmgr.h"
#include "../Crate.h"
#include "../mgmt_protocol.h"

class Command_AUTHENTICATE : public Command {
	public:
		Command_AUTHENTICATE(std::string rawcmd, std::vector<std::string> cmd) : Command(rawcmd, cmd) { };
		virtual uint8_t get_required_context() { return 0; };
		virtual enum AuthLevel get_required_privilege() { return AUTH_NONE; };
		virtual void payload() { };
		virtual void finalize(Client& client) {
			scope_lock dlock(&this->lock);

			if (cmd.size() != 3) {
				client.write(stdsprintf("%u ERROR Wrong number of arguments.\n%u\n", this->msgid, this->msgid));
				return;
			}

			for (std::vector<std::string>::iterator it = config_authdata.raw.begin(); it != config_authdata.raw.end(); it++) {
				if (*it == cmd[2])
					client.upgrade_privilege(AUTH_RAW);
			}

			for (std::vector<std::string>::iterator it = config_authdata.manage.begin(); it != config_authdata.manage.end(); it++) {
				if (*it == cmd[2])
					client.upgrade_privilege(AUTH_MANAGE);
			}

			for (std::vector<std::string>::iterator it = config_authdata.read.begin(); it != config_authdata.read.end(); it++) {
				if (*it == cmd[2])
					client.upgrade_privilege(AUTH_READ);
			}

			static const char *privmap[4] = { "NONE", "READ", "MANAGE", "RAW" };
			client.write(stdsprintf("%u PRIVILEGE %s\n%u\n", this->msgid, privmap[client.get_privilege()], this->msgid));
		};
};
