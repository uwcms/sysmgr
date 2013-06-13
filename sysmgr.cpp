#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <inttypes.h>
#include <freeipmi/freeipmi.h>
#include <fcntl.h>
#include <pthread.h>
#include <time.h>
#include <exception>
#include <typeinfo>
#include <confuse.h>

#include "sysmgr.h"
#include "scope_lock.h"
#include "Crate.h"

pthread_mutex_t stdout_mutex = PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP;
pthread_key_t threadid_key;
std::vector<threadlocaldata_t> threadlocal;
config_authdata_t config_authdata;

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
		fflush(stdout);
		fiid_iterator_next(iter);
	}
	mprintf("\n");
	fiid_iterator_destroy(iter);
}


void handleflash(void *cb_crate) {
	Crate *crate = (Crate*)cb_crate;

	fiid_obj_t msg_body_handle = fiid_obj_create(tmpl_ipmb_msg);
	fiid_obj_set_all(msg_body_handle, "\x0f\x81\x64", 3);

	//dmprintf("C%d: Sending Handle Command\n", crate->get_number());
	crate->send_bridged(0, 0x82, 7, 0x70+(2*10), 0x32, msg_body_handle, NULL);
	// DI(ipmi_cmd_ipmb(crate->ctx.ipmi, 0, 0x72, 0, 0x32, msg_body_handle, msg_body_rs));

	fiid_obj_destroy(msg_body_handle);

	THREADLOCAL.taskqueue.schedule(time(NULL)+60, callback<void>::create<handleflash>(), crate);
}

void writeout_state(void *cb_crate)
{
	Crate *crate = (Crate*)cb_crate;

	char fnamebuf[40];
	snprintf(fnamebuf, 40, "/tmp/crate%dstate.txt", crate->get_number());
	FILE *outf = fopen(fnamebuf, "w");
	assert(outf);

	if (!crate->ctx.ipmi) {
		fprintf(outf, "Crate %d is currently offline.\n", crate->get_number());
	}
	else {
		std::vector<Sensor*> sensors[256];
		std::vector<Sensor*>::iterator iterators[256];

		bool selected_a_fru = false;

		for (int fru = 0; fru < 256; fru++) {
			Card *card = crate->get_card(fru);
			if (card) {
				HotswapSensor *hotswap = card->get_hotswap_sensor();
				if (hotswap)
					fprintf(outf, " %11s M%hhu ", card->get_slotstring().c_str(), hotswap->get_state());
				else
					fprintf(outf, " %-11s M- ", card->get_slotstring().c_str());
				sensors[fru] = card->get_sensors();
				iterators[fru] = sensors[fru].begin();

				selected_a_fru = true;
			}
			else {
				//fprintf(outf, "       Empty ");
			}
		}
		if (selected_a_fru)
			fprintf(outf, "\n");
		for (int fru = 0; fru < 256; fru++) {
			Card *card = crate->get_card(fru);
			if (card)
				fprintf(outf, "%15.15s ", card->get_name().c_str());
		}

		if (!selected_a_fru) {
			fprintf(outf, "Crate %d has no detected cards.\n", crate->get_number());
		}
		else {
			fprintf(outf, "\n");
			fprintf(outf, "\n");
		}

		bool done;
		do {
			done = true;
			for (int fru = 0; fru < 256; fru++) {
				if (!crate->get_card(fru))
					continue;

				if (iterators[fru] == sensors[fru].end()) {
					fprintf(outf, "%15s ", "");
					continue;
				}
				else {
					fprintf(outf, "%15.15s ", (*iterators[fru])->get_name().c_str());
					iterators[fru]++;
					done = false;
				}
			}
			fprintf(outf, "\n");
		} while (!done);

	}
	fclose(outf);

	THREADLOCAL.taskqueue.schedule(time(NULL)+1, callback<void>::create<writeout_state>(), crate);
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


	//THREADLOCAL.taskqueue.schedule(time(NULL)+10, callback<void>::create<handleflash>(), crate);

	writeout_state(crate);

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
			mprintf("C%d: Caught System Exception: %s.  Restarting Crate %d in %d seconds.\n", crate_no, typeid(typeof(e)).name(), crate_no, (1 << backoff-1));
		}
		catch (Sysmgr_Exception& e) {
			// Local Exception
			if (backoff < 7)
				backoff++;
			if (e.get_message().c_str()[0] != '\0')
				mprintf("C%d: Caught %s on %s:%d in %s(): %s\n", crate_no, e.get_type().c_str(), e.get_file().c_str(), e.get_line(), e.get_func().c_str(), e.get_message().c_str());
			else
				mprintf("C%d: Caught %s on %s:%d in %s()\n", crate_no, e.get_type().c_str(), e.get_file().c_str(), e.get_line(), e.get_func().c_str());

			mprintf("C%d: Restarting Crate %d in %d seconds.\n", crate_no, crate_no, (1 << backoff-1));
		}
		crate->ipmi_disconnect();
		launchserial++;

		time_t next = time(NULL) + (1 << backoff-1);
		THREADLOCAL.taskqueue.schedule(next, callback<void>::create<start_crate>(), crate);

		reset_backoff_data_t *backoffdata = new reset_backoff_data_t;

		backoffdata->backoff = &backoff;
		backoffdata->curlaunchserial = &launchserial;
		backoffdata->launchserial = launchserial;

		THREADLOCAL.taskqueue.schedule(time(NULL)+(1 << backoff-1+1), callback<void>::create<reset_backoff>(), backoffdata);
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

int main(int argc, char *argv[])
{
	uint8_t threadid_main = 0;
	pthread_key_create(&threadid_key, NULL);
	pthread_setspecific(threadid_key, &threadid_main);

	/*
	 * Parse Configuration
	 */

	cfg_opt_t opts_auth[] =
	{
		CFG_STR_LIST("raw", "{}", CFGF_NONE),
		CFG_STR_LIST("manage", "{}", CFGF_NONE),
		CFG_STR_LIST("read", "{}", CFGF_NONE),
		CFG_END()
	};

	cfg_opt_t opts_crate[] =
	{
		CFG_STR("host", "", CFGF_NONE),
		CFG_STR("description", "", CFGF_NONE),
		CFG_STR("username", "", CFGF_NONE),
		CFG_STR("password", "", CFGF_NONE),
		CFG_INT_CB("authtype", 0, CFGF_NONE, cfg_parse_authtype),
		CFG_INT_CB("mch", 0, CFGF_NONE, cfg_parse_MCH),
		CFG_BOOL("enabled", cfg_true, CFGF_NONE),
		CFG_END()
	};

	cfg_opt_t opts[] =
	{
		CFG_SEC("authentication", opts_auth, CFGF_NONE),
		CFG_SEC("crate", opts_crate, CFGF_MULTI),
		CFG_INT("socket_port", 4681, CFGF_NONE),
		CFG_END()
	};
	cfg_t *cfg = cfg_init(opts, CFGF_NONE);
	cfg_set_validate_func(cfg, "crate|host", &cfg_validate_hostname);
	cfg_set_validate_func(cfg, "socket_port", &cfg_validate_port);

	if(cfg_parse(cfg, CONFIG_FILE) == CFG_PARSE_ERROR)
		exit(1);

	cfg_t *cfgauth = cfg_getsec(cfg, "authentication");
	for(unsigned int i = 0; i < cfg_size(cfgauth, "raw"); i++)
		config_authdata.raw.push_back(std::string(cfg_getnstr(cfgauth, "raw", i)));
	for(unsigned int i = 0; i < cfg_size(cfgauth, "manage"); i++)
		config_authdata.manage.push_back(std::string(cfg_getnstr(cfgauth, "manage", i)));
	for(unsigned int i = 0; i < cfg_size(cfgauth, "read"); i++)
		config_authdata.read.push_back(std::string(cfg_getnstr(cfgauth, "read", i)));

	for(unsigned int i = 0; i < cfg_size(cfg, "crate"); i++) {
		cfg_t *cfgcrate = cfg_getnsec(cfg, "crate", i);

		enum Crate::Mfgr MCH;
		switch (cfg_getint(cfgcrate, "mch")) {
			case Crate::VADATECH: MCH = Crate::VADATECH; break;
			case Crate::NAT: MCH = Crate::NAT; break;
		}

		const char *user = cfg_getstr(cfgcrate, "username");
		const char *pass = cfg_getstr(cfgcrate, "password");

		Crate *crate = new Crate(i+1, MCH, cfg_getstr(cfgcrate, "host"), (user[0] ? user : NULL), (pass[0] ? pass : NULL), cfg_getint(cfgcrate, "authtype"), cfg_getstr(cfgcrate, "description"));

		bool enabled = (cfg_getbool(cfgcrate, "enabled") == cfg_true);
		threadlocal.push_back(threadlocaldata_t(crate, enabled));
	}

	uint16_t port = cfg_getint(cfg, "socket_port");

	cfg_free(cfg);

	/*
	 * Instantiate Worker Threads
	 */

	for (std::vector<threadlocaldata_t>::iterator it = threadlocal.begin(); it != threadlocal.end(); it++)
		if (it->enabled)
			pthread_create(&it->thread, NULL, crate_monitor, (void *)it->crate->get_number());

#ifndef DEBUG_ONESHOT
	protocol_server(port);
#endif

	for (std::vector<threadlocaldata_t>::iterator it = threadlocal.begin(); it != threadlocal.end(); it++)
		if (it->enabled)
			pthread_join(it->thread, NULL);
}
