#include <boost/program_options.hpp>
#include <confuse.h>
#include <dlfcn.h>
#include <errno.h>
#include <exception>
#include <fcntl.h>
#include <freeipmi/freeipmi.h>
#include <grp.h>
#include <inttypes.h>
#include <iostream>
#include <limits.h>
#include <pthread.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include <typeinfo>
#include <unistd.h>
namespace opt = boost::program_options;

#include "sysmgr.h"
#include "scope_lock.h"
#include "Crate.h"

pthread_key_t threadid_key;
std::vector<threadlocaldata_t> threadlocal;
config_authdata_t config_authdata;
std::vector<cardmodule_t> card_modules;
uint32_t config_ratelimit_delay;

void fiid_dump(fiid_obj_t obj)
{
	scope_lock l(&stdout_mutex);

	fiid_iterator_t iter = fiid_iterator_create(obj);
	while (!fiid_iterator_end(iter)) {
		char *field = fiid_iterator_key(iter);
		int fieldlen = fiid_iterator_field_len(iter);
		if (fieldlen <= 64) {
			uint64_t data;
			fiid_iterator_get(iter, &data);
			mprintf("C%d: %10lxh = %-60s (%2ub)\n", CRATE_NO, data, field, fieldlen);
		}
		else {
			uint8_t data[fieldlen/8];
			fiid_iterator_get_data(iter, &data, fieldlen/8);
			mprintf("C%d: DATA: %-60s (%2ub)\n  [ ", CRATE_NO, field, fieldlen);
			for (int i = 0; i < fieldlen/8; i++)
				mprintf("%02x ", data[i]);
			mprintf("]\n");
		}
		fiid_iterator_next(iter);
	}
	mprintf("\n");
	fiid_iterator_destroy(iter);
}


void start_crate(void *cb_crate)
{
	Crate *crate = (Crate*)cb_crate;
	crate->ipmi_connect();
}

typedef struct {
	uint8_t *backoff;
	uint8_t *curlaunchserial;
	uint8_t launchserial;
} reset_backoff_data_t;

void reset_backoff(void *cb_reset_backoff_data_t)
{
	reset_backoff_data_t *data = (reset_backoff_data_t*)cb_reset_backoff_data_t;
	if (*(data->curlaunchserial) == data->launchserial)
		*(data->backoff) = 1;

	delete data;
}

void *crate_monitor(void *arg)
{
	uint8_t crate_no = (uintptr_t)arg;
	pthread_setspecific(threadid_key, &crate_no);

	Crate *crate = THREADLOCAL.crate;

#ifdef DEBUG_ONESHOT // NAT DEBUG
	crate->ipmi_connect();


	fiid_template_t tmpl_set_clock_rq =
	{
		{ 8, "cmd", FIID_FIELD_REQUIRED | FIID_FIELD_LENGTH_FIXED},
		{ 32, "time", FIID_FIELD_REQUIRED | FIID_FIELD_LENGTH_FIXED},
		{ 0, "", 0}
	};

	fiid_obj_t set_clock_rq = fiid_obj_create(tmpl_set_clock_rq);
	fiid_obj_set(set_clock_rq, "cmd", 0x29);
	fiid_obj_set(set_clock_rq, "time", 0x01010101);

	mprintf("\n\n\n");
	crate->send_bridged(0, 0x82, 7, 0x70+(2*10), 0x32, set_clock_rq, NULL);

	fiid_obj_destroy(set_clock_rq);



	fiid_template_t tmpl_get_clock_rq =
	{
		{ 8, "cmd", FIID_FIELD_REQUIRED | FIID_FIELD_LENGTH_FIXED},
		{ 0, "", 0}
	};
	fiid_template_t tmpl_get_clock_rs =
	{
		{ 32, "time", FIID_FIELD_REQUIRED | FIID_FIELD_LENGTH_FIXED},
		{ 0, "", 0}
	};

	fiid_obj_t get_clock_rs = fiid_obj_create(tmpl_get_clock_rs);
	fiid_obj_t get_clock_rq = fiid_obj_create(tmpl_get_clock_rq);
	fiid_obj_set(get_clock_rq, "cmd", 0x2a);

	mprintf("\n\n\n");
	crate->send_bridged(0, 0x82, 3, 0x70+(2*10), 0x32, get_clock_rq, get_clock_rs);
	mprintf("\n\n\n");

	fiid_dump(get_clock_rs);

	fiid_obj_destroy(get_clock_rq);
	return NULL;
#endif


	THREADLOCAL.taskqueue.schedule(0, callback<void>::create<start_crate>(), crate);

#ifndef EXCEPTION_GUARD
	THREADLOCAL.taskqueue.run_forever();
#else
	uint8_t launchserial = 0;
	uint8_t backoff = 1;
	while (1) {
		try {
			THREADLOCAL.taskqueue.run_forever();
		}
		catch (std::exception& e) {
			// System Exception: Lower backoff cap.
			if (backoff < 6)
				backoff++;
			mprintf("C%d: Caught System Exception: %s.  Restarting Crate %d monitoring in %d seconds.\n", crate_no, typeid(typeof(e)).name(), crate_no, (1 << (backoff-1)));
		}
		catch (SDRRepositoryNotPopulatedException& e) {
			if (backoff < 8)
				backoff++;
			mprintf("C%d: The SDR repository is not yet populated.  Restarting Crate %d monitoring in %d seconds.\n", crate_no, crate_no, (1 << (backoff-1)));
		}
		catch (IPMI_ConnectionFailedException& e) {
			if (backoff < 8)
				backoff++;
			mprintf("C%d: Unable to connect to Crate %d: \"%s\".  Restarting Crate %d monitoring in %d seconds.\n", crate_no, crate_no, e.get_message().c_str(), crate_no, (1 << (backoff-1)));
		}
		catch (Sysmgr_Exception& e) {
			// Local Exception
			if (backoff < 8)
				backoff++;
			if (e.get_message().c_str()[0] != '\0')
				mprintf("C%d: Caught %s on %s:%d in %s(): %s\n", crate_no, e.get_type().c_str(), e.get_file().c_str(), e.get_line(), e.get_func().c_str(), e.get_message().c_str());
			else
				mprintf("C%d: Caught %s on %s:%d in %s()\n", crate_no, e.get_type().c_str(), e.get_file().c_str(), e.get_line(), e.get_func().c_str());

			mprintf("C%d: Restarting Crate %d monitoring in %d seconds.\n", crate_no, crate_no, (1 << (backoff-1)));
		}
		crate->ipmi_disconnect();
		launchserial++;

		time_t next = time(NULL) + (1 << (backoff-1));
		THREADLOCAL.taskqueue.schedule(next, callback<void>::create<start_crate>(), crate);

		reset_backoff_data_t *backoffdata = new reset_backoff_data_t;

		backoffdata->backoff = &backoff;
		backoffdata->curlaunchserial = &launchserial;
		backoffdata->launchserial = launchserial;

		THREADLOCAL.taskqueue.schedule(time(NULL)+(1 << (backoff-1+1)), callback<void>::create<reset_backoff>(), backoffdata);
	}
#endif

	return NULL;
}

void protocol_server(uint16_t port); // From mgmt_protocol.cpp

static int cfg_validate_hostname(cfg_t *cfg, cfg_opt_t *opt)
{
	const char *host = cfg_opt_getnstr(opt, cfg_opt_size(opt) - 1);

	if (strspn(host, "qwertyuiopasdfghjklzxcvbnm.-_1234567890QWERTYUIOPASDFGHJKLZXCVBNM") != strlen(host)) {
		cfg_error(cfg, "Invalid %s: %s", opt->name, host);
		return -1;
	}
	return 0;
}

static int cfg_validate_port(cfg_t *cfg, cfg_opt_t *opt)
{
	unsigned int port = cfg_opt_getnint(opt, cfg_opt_size(opt) - 1);

	if (port > USHRT_MAX) {
		cfg_error(cfg, "Invalid %s: %s", opt->name, port);
		return -1;
	}
	return 0;
}

static int cfg_parse_authtype(cfg_t *cfg, cfg_opt_t *opt, const char *value, void *result)
{
	if (strcasecmp(value, "NONE") == 0)
		*(long int *)result = 0;
	else if (strcasecmp(value, "MD2") == 0)
		*(long int *)result = 1;
	else if (strcasecmp(value, "MD5") == 0)
		*(long int *)result = 2;
	else if (strcasecmp(value, "STRAIGHT_PASSWORD_KEY") == 0)
		*(long int *)result = 4;
	else {
		cfg_error(cfg, "Invalid MCH: %s", value);
		return -1;
	}
	return 0;
}

static int cfg_parse_MCH(cfg_t *cfg, cfg_opt_t *opt, const char *value, void *result)
{
	if (strcasecmp(value, "VADATECH") == 0)
		*(long int *)result = 0;
	else if (strcasecmp(value, "NAT") == 0)
		*(long int *)result = 1;
	else {
		cfg_error(cfg, "Invalid MCH: %s", value);
		return -1;
	}
	return 0;
}

void do_fork() {
	if (fork())
		exit(0);
	setsid();
	if (fork())
		exit(0);
}                                                                                                                                                              

extern const char *GIT_BRANCH, *GIT_COMMIT, *GIT_DIRTY;

int main(int argc, char *argv[])
{
	uint8_t threadid_main = 0;
	pthread_key_create(&threadid_key, NULL);
	pthread_setspecific(threadid_key, &threadid_main);

	mprintf("University of Wisconsin IPMI MicroTCA System Manager\n");

	std::string configfile;
	std::string setuser;
	std::string setgroup;
	bool print_version;

	opt::options_description option_all("Options");
	option_all.add_options()
		("help,h", "help")
		("config,c", opt::value<std::string>(&configfile)->default_value(CONFIG_PATH "/" CONFIG_FILE), "config file path")
		("user,u", opt::value<std::string>(&setuser)->default_value(""), "setuid user or uid")
		("group,g", opt::value<std::string>(&setgroup)->default_value(""), "setgid group or gid")
		("version,V", opt::bool_switch(&print_version)->default_value(false), "show version info");
	opt::positional_options_description option_pos;
	option_pos.add("config", 1);
	try {
		opt::variables_map vm;
		opt::store(opt::command_line_parser(argc, argv).options(option_all).positional(option_pos).run(), vm);
		opt::notify(vm);

		if (vm.count("help")) {
			printf("%s [options]\n", argv[0]);
			printf("\n");
			std::cout << option_all << "\n";
			exit(0);
		}
		}
	catch (std::exception &e) {
		mprintf("Argument Error %s\n\n", e.what());
		mprintf("Try --help\n");
		return -1;
	}

	/*
	 * Print Version & Exit
	 */
	if (print_version) {
		mprintf("\nCompiled from %s@%s\n", (GIT_BRANCH[0] ? GIT_BRANCH : "git-archive"), (GIT_COMMIT[0] ? GIT_COMMIT : "$Format:%H$"));
		if (strlen(GIT_DIRTY) > 1)
			mprintf("%s\nBuilt %s %s\n", GIT_DIRTY, __DATE__, __TIME__);
		mprintf("\n");
		return 0;
	}

	/*
	 * Set UID / GID
	 */
	gid_t gid;
	if (setgroup.size()) {
		const char *setgroup_parse = setgroup.c_str();
		char *endptr = NULL;
		gid = strtoull(setgroup_parse, &endptr, 10);
		if (!endptr || endptr == setgroup_parse) {
			// Non-numeric.
			struct group *grp = getgrnam(setgroup_parse);
			if (!grp) {
				mprintf("Unknown group for setgid.  Aborting.\n");
				exit(1);
			}
			gid = grp->gr_gid;
		}
		else if (gid == ULONG_MAX || !endptr || *endptr) {
			mprintf("Failed to parse group ID.  Aborting.\n");
			exit(1);
		}
	}

	uid_t uid;
	if (setuser.size()) {
		const char *setuser_parse = setuser.c_str();
		char *endptr = NULL;
		uid = strtoull(setuser_parse, &endptr, 10);
		if (!endptr || endptr == setuser_parse) {
			// Non-numeric.
			struct passwd *usr = getpwnam(setuser_parse);
			if (!usr) {
				mprintf("Unknown user for setuid.  Aborting.\n");
				exit(1);
			}
			uid = usr->pw_uid;
		}
		else if (uid == ULONG_MAX || !endptr || *endptr) {
			mprintf("Failed to parse user ID.  Aborting.\n");
			exit(1);
		}
	}

	if (setgroup.size()) {
		if (getgroups(0, NULL) != 0) {
			perror("Unable to clear supplementary groups list");
			exit(1);
		}
		if (setresgid(gid, gid, gid) != 0) {
			perror("Unable to set GID");
			exit(1);
		}
	}

	if (setuser.size()) {
		if (setresuid(uid, uid, uid) != 0) {
			perror("Unable to set UID");
			exit(1);
		}
	}

	/*
	 * Parse Configuration
	 */

	cfg_opt_t opts_auth[] =
	{
		CFG_STR_LIST(const_cast<char *>("raw"), const_cast<char *>("{}"), CFGF_NONE),
		CFG_STR_LIST(const_cast<char *>("manage"), const_cast<char *>("{}"), CFGF_NONE),
		CFG_STR_LIST(const_cast<char *>("read"), const_cast<char *>("{}"), CFGF_NONE),
		CFG_END()
	};

	cfg_opt_t opts_crate[] =
	{
		CFG_STR(const_cast<char *>("host"), const_cast<char *>(""), CFGF_NONE),
		CFG_STR(const_cast<char *>("description"), const_cast<char *>(""), CFGF_NONE),
		CFG_STR(const_cast<char *>("username"), const_cast<char *>(""), CFGF_NONE),
		CFG_STR(const_cast<char *>("password"), const_cast<char *>(""), CFGF_NONE),
		CFG_INT_CB(const_cast<char *>("authtype"), 0, CFGF_NONE, cfg_parse_authtype),
		CFG_INT_CB(const_cast<char *>("mch"), 0, CFGF_NONE, cfg_parse_MCH),
		CFG_BOOL(const_cast<char *>("enabled"), cfg_true, CFGF_NONE),
		CFG_BOOL(const_cast<char *>("log_sel"), cfg_false, CFGF_NONE),
		CFG_END()
	};

	cfg_opt_t opts_cardmodule[] =
	{
		CFG_STR(const_cast<char *>("module"), const_cast<char *>(""), CFGF_NONE),
		CFG_STR_LIST(const_cast<char *>("config"), const_cast<char *>("{}"), CFGF_NONE),
		CFG_END()
	};

	cfg_opt_t opts[] =
	{
		CFG_SEC(const_cast<char *>("authentication"), opts_auth, CFGF_NONE),
		CFG_SEC(const_cast<char *>("crate"), opts_crate, CFGF_MULTI),
		CFG_SEC(const_cast<char *>("cardmodule"), opts_cardmodule, CFGF_MULTI),
		CFG_INT(const_cast<char *>("socket_port"), 4681, CFGF_NONE),
		CFG_INT(const_cast<char *>("ratelimit_delay"), 0, CFGF_NONE),
		CFG_BOOL(const_cast<char *>("daemonize"), cfg_false, CFGF_NONE),
		CFG_END()
	};
	cfg_t *cfg = cfg_init(opts, CFGF_NONE);
	cfg_set_validate_func(cfg, "crate|host", &cfg_validate_hostname);
	cfg_set_validate_func(cfg, "socket_port", &cfg_validate_port);

	if (access(configfile.c_str(), R_OK) == 0) {
		if(cfg_parse(cfg, configfile.c_str()) == CFG_PARSE_ERROR)
			exit(1);
	}
	else {
		mprintf("Config file %s not found.\n", configfile.c_str());
		mprintf("Try: %s -c sysmgr.conf\n", argv[0]);
		exit(1);
	}

	bool crate_found = false;
	bool crate_enabled = false;

	cfg_t *cfgauth = cfg_getsec(cfg, "authentication");
	for(unsigned int i = 0; i < cfg_size(cfgauth, "raw"); i++)
		config_authdata.raw.push_back(std::string(cfg_getnstr(cfgauth, "raw", i)));
	for(unsigned int i = 0; i < cfg_size(cfgauth, "manage"); i++)
		config_authdata.manage.push_back(std::string(cfg_getnstr(cfgauth, "manage", i)));
	for(unsigned int i = 0; i < cfg_size(cfgauth, "read"); i++)
		config_authdata.read.push_back(std::string(cfg_getnstr(cfgauth, "read", i)));

	for(unsigned int i = 0; i < cfg_size(cfg, "crate"); i++) {
		cfg_t *cfgcrate = cfg_getnsec(cfg, "crate", i);
		crate_found = true;

		enum Crate::Mfgr MCH;
		switch (cfg_getint(cfgcrate, "mch")) {
			case Crate::VADATECH: MCH = Crate::VADATECH; break;
			case Crate::NAT: MCH = Crate::NAT; break;
		}

		const char *user = cfg_getstr(cfgcrate, "username");
		const char *pass = cfg_getstr(cfgcrate, "password");

		Crate *crate = new Crate(i+1, MCH, cfg_getstr(cfgcrate, "host"), (user[0] ? user : NULL), (pass[0] ? pass : NULL), cfg_getint(cfgcrate, "authtype"), cfg_getstr(cfgcrate, "description"), (cfg_getbool(cfgcrate, "log_sel") == cfg_true));

		bool enabled = (cfg_getbool(cfgcrate, "enabled") == cfg_true);
		if (enabled)
			crate_enabled = true;
		threadlocal.push_back(threadlocaldata_t(crate, enabled));
	}

	for(unsigned int i = 0; i < cfg_size(cfg, "cardmodule"); i++) {
		cfg_t *cfgmodule = cfg_getnsec(cfg, "cardmodule", i);

		const char *module = cfg_getstr(cfgmodule, "module");

		std::vector<std::string> configdata;
		for(unsigned int i = 0; i < cfg_size(cfgmodule, "config"); i++)
			configdata.push_back(std::string(cfg_getnstr(cfgmodule, "config", i)));

		std::string default_module_path = DEFAULT_MODULE_PATH;
	   	if (getenv("SYSMGR_MODULE_PATH") != NULL)
			default_module_path = getenv("SYSMGR_MODULE_PATH");

		std::string modulepath = module;
		if (modulepath.find("/") == std::string::npos)
			modulepath = default_module_path +"/"+ modulepath;

		cardmodule_t cm;
		cm.dl_addr = dlopen(modulepath.c_str(), RTLD_NOW|RTLD_GLOBAL);
		if (cm.dl_addr == NULL) {
			printf("Error loading module %s:\n\t%s\n", module, dlerror());
			exit(2);
		}

		void *sym;
#define LOAD_SYM(name, type) \
		sym = dlsym(cm.dl_addr, #name); \
		if (sym == NULL) { \
			mprintf("Error loading module %s " type " " #name ":\n\t%s\n", module, dlerror()); \
			exit(2); \
		}

		LOAD_SYM(APIVER, "variable");
		cm.APIVER = *reinterpret_cast<uint32_t*>(sym);

		LOAD_SYM(MIN_APIVER, "variable");
		cm.MIN_APIVER = *reinterpret_cast<uint32_t*>(sym);

		if (cm.APIVER < CARD_MODULE_API_VERSION || cm.MIN_APIVER > CARD_MODULE_API_VERSION) {
			mprintf("Error loading module %s: Incompatible API version %u\n", module, cm.APIVER);
		}

		LOAD_SYM(initialize_module, "function");
		cm.initialize_module = reinterpret_cast<bool (*)(std::vector<std::string>)>(sym);

		LOAD_SYM(instantiate_card, "function");
		cm.instantiate_card = reinterpret_cast<Card* (*)(Crate*, std::string, void*, uint8_t)>(sym);

#undef LOAD_SYM

		if (!cm.initialize_module(configdata)) {
			printf("Error loading module %s: initialize_module() returned false\n", module);
			exit(2);
		}

		card_modules.insert(card_modules.begin(), cm);
	}

	uint16_t port = cfg_getint(cfg, "socket_port");
	config_ratelimit_delay = cfg_getint(cfg, "ratelimit_delay");
	bool daemonize = (cfg_getbool(cfg, "daemonize") == cfg_true);

	cfg_free(cfg);

	if (!crate_found) {
		printf("No crate specified in the configuration file.\n");
		exit(1);
	}
	if (!crate_enabled) {
		printf("No crates are enabled in the configuration file.\n");
		printf("No crates to service.\n");
		exit(1);
	}

	if (daemonize) {
		do_fork();
		stdout_use_syslog = true;
		mprintf("University of Wisconsin IPMI MicroTCA System Manager\n");
	}

	/*
	 * Initialize library crypto routines before spawning threads.
	 * This connect will fail due to hostname too long, after running the crypt init functions.
	 *
	 * Max Hostname Limit: 64
	 */
	ipmi_ctx_t dummy_ipmi_ctx = ipmi_ctx_create();
	if (ipmi_ctx_open_outofband_2_0(dummy_ipmi_ctx,
				".................................................................",			// hostname
				NULL,					// username
				NULL,					// password
				NULL,						// k_g
				0,							// k_g_len,
				4,							// privilege_level
				0,							// cipher_suite_id
				0,							// session_timeout
				5,							// retransmission_timeout
				IPMI_WORKAROUND_FLAGS_OUTOFBAND_2_0_OPEN_SESSION_PRIVILEGE,	// workaround_flags
				IPMI_FLAGS_DEFAULT			// flags
				) == 0) {
		ipmi_ctx_close(dummy_ipmi_ctx);
	}
	ipmi_ctx_destroy(dummy_ipmi_ctx);

	/*
	 * Instantiate Worker Threads
	 */

	for (std::vector<threadlocaldata_t>::iterator it = threadlocal.begin(); it != threadlocal.end(); it++)
		if (it->enabled)
			pthread_create(&it->thread, NULL, crate_monitor, reinterpret_cast<void *>(it->crate->get_number()));

#ifndef DEBUG_ONESHOT
	protocol_server(port);
#endif

	for (std::vector<threadlocaldata_t>::iterator it = threadlocal.begin(); it != threadlocal.end(); it++)
		if (it->enabled)
			pthread_join(it->thread, NULL);
}
