#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdexcept>
#include <unistd.h>

#include "sysmgr.h"
#include "Crate.h"
#include "Callback.h"

void Crate::add_card(Card *card)
{
	uint8_t fru = card->get_fru();
	if (this->cards[fru]) {

		dmprintf("C%d: Card replaced in %s\n", this->number, card->get_slotstring().c_str());
		delete this->cards[fru];
	}
	mprintf("C%d: Card added in %s: %s\n", this->number, card->get_slotstring().c_str(), card->get_name().c_str());
	this->cards[fru] = card;
}

Card *Crate::get_card(uint8_t fru)
{
	return this->cards[fru];
}

Card *Crate::get_card(uint8_t entity_id, uint8_t entity_instance)
{
	for (int fru = 0; fru < 256; fru++)
		if (this->cards[fru]
				&& this->cards[fru]->get_entity_id() == entity_id
				&& this->cards[fru]->get_entity_instance() == entity_instance)
			return this->cards[fru];
	return NULL;
}

void Crate::destroy_card(uint8_t fru)
{
	if (this->cards[fru]) {
		mprintf("C%d: Card removed from %s\n", this->number, this->cards[fru]->get_slotstring().c_str());
		delete this->cards[fru];
			this->cards[fru] = NULL;
	}
};

bool Crate::update_sdr_cache()
{
	int rv = 0;

	fiid_obj_t sdr_repository_info_rs = fiid_obj_create(tmpl_cmd_get_sdr_repository_info_rs);
	if(ipmi_cmd_get_sdr_repository_info(this->ctx.ipmi, sdr_repository_info_rs) < 0)
		THROWMSG(IPMI_LibraryError, "ipmi_cmd_get_sdr_repository_info() failed: (%d) %s", ipmi_ctx_errnum(this->ctx.ipmi), ipmi_ctx_strerror(ipmi_ctx_errnum(this->ctx.ipmi)));

	uint64_t rec_count = 0;
	if (fiid_obj_get(sdr_repository_info_rs, "record_count", &rec_count) < 0)
		THROWMSG(IPMI_LibraryError, "fiid_obj_get() failed");

	fiid_obj_destroy(sdr_repository_info_rs);

	//dmprintf("C%d: Found %lu SDR Records\n", this->number, rec_count);
	if (rec_count <= 32)
		THROW(SDRRepositoryNotPopulatedException);


	if (this->ctx.sel) {
		ipmi_sel_ctx_destroy(this->ctx.sel);
		this->ctx.sel = NULL;
	}

	if (this->ctx.sdr == NULL) {
		this->ctx.sdr = ipmi_sdr_ctx_create();
		if (!this->ctx.sdr) {
			mprintf("C%d: ipmi_sdr_ctx_create() failed\n", this->number);
			THROWMSG(IPMI_LibraryError, "ipmi_sdr_ctx_create() failed");
		}
	}
	else {
		rv = ipmi_sdr_cache_close(this->ctx.sdr);
		if (rv) {
			int sdr_errnum = ipmi_sdr_ctx_errnum(this->ctx.sdr);
			char *sdr_errstr = ipmi_sdr_ctx_strerror(sdr_errnum);
			ipmi_sdr_ctx_destroy(this->ctx.sdr);
			this->ctx.sdr = NULL;
			THROWMSG(IPMI_LibraryError, "ipmi_sdr_cache_close() failed: (%d) %s", sdr_errnum, sdr_errstr);
		}
	}

	if (this->sdrfile[0]) {
		// A cache file is already established.

		rv = ipmi_sdr_cache_open(this->ctx.sdr, this->ctx.ipmi, this->sdrfile);
		if (rv)
			truncate(this->sdrfile, 0);
		else {
			this->ctx.sel = ipmi_sel_ctx_create(this->ctx.ipmi, this->ctx.sdr);
			if (!this->ctx.sel) {
				ipmi_sdr_ctx_destroy(this->ctx.sdr);
				this->ctx.sdr = NULL;
				THROWMSG(IPMI_LibraryError, "ipmi_sel_ctx_create() failed");
			}
#ifdef INTERPRET_CTX
			if (this->ctx.interpret)
				ipmi_sel_ctx_set_parameter(this->ctx.sel, IPMI_SEL_PARAMETER_INTERPRET_CONTEXT, this->ctx.interpret);
#endif

			return false;
		}
	}
	else {
		// Establish a cache filename.

		snprintf(this->sdrfile, 40, "/tmp/freeipmi.sdr.%hhu.XXXXXX", this->number);
		int sdrfd = mkstemp(this->sdrfile);
		if (sdrfd == -1) {
			mprintf("C%d: Unable to create sdrfile! (%d: %s)\n", this->number, errno, strerror(errno));
			ipmi_sdr_ctx_destroy(this->ctx.sdr);
			this->ctx.sdr = NULL;
			THROWMSG(Sysmgr_Exception, "Unable to create sdrfile! (%d: %s)", errno, strerror(errno));
		}
		close(sdrfd);
	}

	rv = ipmi_sdr_cache_create(this->ctx.sdr, this->ctx.ipmi, this->sdrfile, IPMI_SDR_CACHE_CREATE_FLAGS_OVERWRITE, NULL, NULL);
	if (rv) {
		int sdr_errnum = ipmi_sdr_ctx_errnum(this->ctx.sdr);
		char *sdr_errstr = ipmi_sdr_ctx_strerror(sdr_errnum);
		ipmi_sdr_ctx_destroy(this->ctx.sdr);
		this->ctx.sdr = NULL;
		THROWMSG(IPMI_LibraryError, "ipmi_sdr_cache_create() failed: (%d) %s", sdr_errnum, sdr_errstr);
	}
	rv = ipmi_sdr_cache_open(this->ctx.sdr, this->ctx.ipmi, this->sdrfile);
	if (rv) {
		int sdr_errnum = ipmi_sdr_ctx_errnum(this->ctx.sdr);
		char *sdr_errstr = ipmi_sdr_ctx_strerror(sdr_errnum);
		ipmi_sdr_ctx_destroy(this->ctx.sdr);
		this->ctx.sdr = NULL;
		THROWMSG(IPMI_LibraryError, "ipmi_sdr_cache_open() failed: (%d) %s", sdr_errnum, sdr_errstr);
	}

	this->ctx.sel = ipmi_sel_ctx_create(ctx.ipmi, ctx.sdr);
	if (!this->ctx.sel) {
		ipmi_sdr_ctx_destroy(this->ctx.sdr);
		this->ctx.sdr = NULL;
		THROWMSG(IPMI_LibraryError, "ipmi_sel_ctx_create() failed");
	}
#ifdef INTERPRET_CTX
	if (this->ctx.interpret)
		ipmi_sel_ctx_set_parameter(this->ctx.sel, IPMI_SEL_PARAMETER_INTERPRET_CONTEXT, this->ctx.interpret);
#endif

	return true;
}

int crate__parse_sel_cb(ipmi_sel_ctx_t ctx, void *cb_crate)
{
	Crate *crate = (Crate*)cb_crate;

	int rv;

#ifdef PRINT_SEL
	/*
	 * Print out Event String for debug/testing
	 */

	// FreeIPMI Library TRACE build option override
	//
	// Really, lets just not get this stupid error message.
	// We NEVER care.
	//int olderr = dup(2);
	//close(2);
	//open("/dev/null", O_WRONLY);

	char eventstring[1024];
	rv = ipmi_sel_parse_read_record_string(ctx,
				"%i: %d %t: %T sensor %s %k: %e",
				NULL,
				0,
				eventstring,
				1024,
				IPMI_SEL_STRING_FLAGS_VERBOSE|IPMI_SEL_STRING_FLAGS_OUTPUT_NOT_AVAILABLE|IPMI_SEL_STRING_FLAGS_IGNORE_UNAVAILABLE_FIELD);

	// And restore stderr before going ANY farther.
	//dup2(olderr, 2);
	//close(olderr);

	if (rv < 0)
		RETMSG(-1, "ipmi_sel_parse_read_record_string() failed: (%d) %s", ipmi_sel_ctx_errnum(ctx), ipmi_sel_ctx_strerror(ipmi_sel_ctx_errnum(ctx)));
	if (rv >= 0)
		mprintf("C%d: %s\n", CRATE_NO, eventstring);
#endif

	uint16_t record_id;
	rv = ipmi_sel_parse_read_record_id(ctx, NULL, 0, &record_id);
	if (rv < 0)
		RETMSG(-1, "ipmi_sel_parse_read_record_id() failed: (%d) %s", ipmi_sel_ctx_errnum(ctx), ipmi_sel_ctx_strerror(ipmi_sel_ctx_errnum(ctx)));

	/*
	 * Collect data to access the sensor.
	 */
	uint8_t sensor_number;
	rv = ipmi_sel_parse_read_sensor_number(ctx, NULL, 0, &sensor_number);
	if (rv < 0)
		RETMSG(-1, "ipmi_sel_parse_read_sensor_number() failed: (%d) %s", ipmi_sel_ctx_errnum(ctx), ipmi_sel_ctx_strerror(ipmi_sel_ctx_errnum(ctx)));

	uint8_t generator_id;
	rv = ipmi_sel_parse_read_generator_id(ctx, NULL, 0, &generator_id);
	if (rv < 0)
		RETMSG(-1, "ipmi_sel_parse_read_generator_id() failed: (%d) %s", ipmi_sel_ctx_errnum(ctx), ipmi_sel_ctx_strerror(ipmi_sel_ctx_errnum(ctx)));

	rv = ipmi_sdr_cache_search_sensor(crate->ctx.sdr, sensor_number, generator_id);
	if (rv < 0) {
		if (ipmi_sdr_ctx_errnum(crate->ctx.sdr) == IPMI_SDR_ERR_NOT_FOUND) {
			crate->selscan_nextent = record_id+1; // Give up on this event.
			return 0; // Continue
		}
		RETMSG(-1, "ipmi_sdr_cache_search_sensor() failed: (%d) %s", ipmi_sdr_ctx_errnum(crate->ctx.sdr), ipmi_sdr_ctx_strerror(ipmi_sdr_ctx_errnum(crate->ctx.sdr)));
	}

	uint8_t entity_id;
	uint8_t entity_instance;
	uint8_t entity_instance_type;
	rv = ipmi_sdr_parse_entity_id_instance_type(crate->ctx.sdr, NULL, 0,
			&entity_id, &entity_instance, &entity_instance_type);
	if (rv < 0)
		RETMSG(-1, "ipmi_sdr_parse_entity_id_instance_type() failed: (%d) %s", ipmi_sdr_ctx_errnum(crate->ctx.sdr), ipmi_sdr_ctx_strerror(ipmi_sdr_ctx_errnum(crate->ctx.sdr)));

	/*
	 * Collect event data for the sensor_event callouts.
	 */
	uint8_t event_direction;
	rv = ipmi_sel_parse_read_event_direction(crate->ctx.sel, NULL, 0, &event_direction);
	if (rv < 0)
		RETMSG(-1, "ipmi_sel_parse_read_event_direction() failed: (%d) %s", ipmi_sel_ctx_errnum(crate->ctx.sel), ipmi_sel_ctx_strerror(ipmi_sel_ctx_errnum(crate->ctx.sel)));

	bool assertion = (event_direction == IPMI_SEL_RECORD_ASSERTION_EVENT);

	uint8_t event_offset;
	rv = ipmi_sel_parse_read_event_data1(crate->ctx.sel, NULL, 0, &event_offset);
	if (rv < 0)
		RETMSG(-1, "ipmi_sel_parse_read_event_data1() failed: (%d) %s", ipmi_sel_ctx_errnum(crate->ctx.sel), ipmi_sel_ctx_strerror(ipmi_sel_ctx_errnum(crate->ctx.sel)));
	event_offset &= 0x0f;

	char selbuf[64];
	uint8_t selbuflen;
	selbuflen = ipmi_sel_parse_read_record(ctx, &selbuf, 64);

	// If we got this far, we'll call it cleanly processed.
	crate->selscan_nextent = record_id+1;

	/*
	 * Distribute the event.
	 */
	Card *card = crate->get_card(entity_id, entity_instance);
	if (!card)
		return 0;
	Sensor *sensor = card->get_sensor(sensor_number);
	if (sensor) {
		{
			scope_lock lock_events(&events_mutex);
			events.push(EventData(
						crate->get_number(),
						card->get_fru(),
						card->get_name(),
						sensor->get_name(),
						assertion,
						event_offset
						));
			wake_socket.wake();
		}
		try {
			sensor->sensor_event(assertion, event_offset, selbuf, selbuflen);
			card->sensor_event(sensor, assertion, event_offset, selbuf, selbuflen);
		}
		catch (std::exception& e) {
			mprintf("C%d: SEL: Caught System Exception: %s.\n", crate->get_number(), typeid(typeof(e)).name());
			return -2;
		}
		catch (Sysmgr_Exception& e) {
			if (e.get_message().c_str()[0] != '\0')
				mprintf("C%d: SEL: Caught %s on %s:%d in %s(): %s\n", crate->get_number(), e.get_type().c_str(), e.get_file().c_str(), e.get_line(), e.get_func().c_str(), e.get_message().c_str());
			else
				mprintf("C%d: SEL: Caught %s on %s:%d in %s()\n", crate->get_number(), e.get_type().c_str(), e.get_file().c_str(), e.get_line(), e.get_func().c_str());
			return -2;
		}
	}

	return 0;
}

void Crate::scan_sel(void *cb_ignore)
{
	if (!this->ctx.sel) {
		this->selscan_id = THREADLOCAL.taskqueue.schedule(time(NULL)+1, callback<void>::create<Crate,&Crate::scan_sel>(this), NULL);
		return;
	}

	uint16_t reservationid;
	ipmi_sel_ctx_register_reservation_id(this->ctx.sel, &reservationid);

	fiid_obj_t obj_cmd_rs = fiid_obj_create(tmpl_cmd_get_sel_info_rs);
	int rv = ipmi_cmd_get_sel_info(this->ctx.ipmi, obj_cmd_rs);
	if (rv)
		THROWMSG(IPMI_LibraryError, "C%d: ipmi_cmd_get_sel_info: errno %d (%s)", CRATE_NO, ipmi_ctx_errnum(this->ctx.ipmi), ipmi_ctx_errormsg(this->ctx.ipmi));

	uint64_t newlastclr;
	uint64_t entrycount;
	fiid_obj_get(obj_cmd_rs, "most_recent_erase_timestamp", &newlastclr);
	fiid_obj_get(obj_cmd_rs, "entries", &entrycount);

	fiid_obj_destroy(obj_cmd_rs);

	//dmprintf("C%d: %lu.%lu - %lu.%lu\n", CRATE_NO, this->selscan_lastclr, this->selscan_nextent, newlastclr, entrycount-1);
	if (newlastclr > this->selscan_lastclr) {
		this->selscan_nextent = 0;
	}

	this->selscan_lastclr = newlastclr;

	if (entrycount && entrycount-1 >= this->selscan_nextent) {
		rv = ipmi_sel_parse(this->ctx.sel, this->selscan_nextent, entrycount-1, crate__parse_sel_cb, this);
		if (rv < 0)
			THROWMSG(IPMI_LibraryError, "ipmi_sel_parse() retuned %d", rv);
	}

	if (this->selscan_nextent > 100) {
		ipmi_sel_clear_sel(this->ctx.sel);
	}

	ipmi_sel_ctx_clear_reservation_id(this->ctx.sel);

	this->selscan_id = THREADLOCAL.taskqueue.schedule(time(NULL)+1, callback<void>::create<Crate,&Crate::scan_sel>(this), NULL);
}

typedef struct {
	bool activeslots[256]; // 14: Don't bother reindexing to 0
	Crate *crate;
	int errors;
} identify_slots_cb_data_t;

int crate__identify_slots_cb(ipmi_sdr_ctx_t ctx,
		uint8_t record_type,
		const void *sdr_record,
		unsigned int sdr_record_len,
		void *cb_data)
{
	identify_slots_cb_data_t *data = (identify_slots_cb_data_t*)cb_data;

	if (record_type != IPMI_SDR_FORMAT_FRU_DEVICE_LOCATOR_RECORD)
		return 0;

	char sdrbuf[64];
	uint8_t sdrbuflen;
	sdrbuflen = ipmi_sdr_cache_record_read(ctx, &sdrbuf, 64);

	char device_id[32];
	if (ipmi_sdr_parse_device_id_string(ctx, NULL, 0, device_id, 32) < 0)
		RETMSG(-1, "ipmi_sdr_parse_device_id_string() failed");

	uint8_t fru_entity_id;
	uint8_t fru_entity_instance;
	if (ipmi_sdr_parse_fru_entity_id_and_instance(ctx, NULL, 0,
				&fru_entity_id, &fru_entity_instance))
		RETMSG(-1, "ipmi_sdr_parse_fru_entity_id_and_instance() failed");

	uint8_t device_access_address;
	uint8_t logical_fru_device_device_slave_address;
	uint8_t private_bus_id;
	uint8_t lun_for_master_write_read_fru_command;
	uint8_t logical_physical_fru_device;
	uint8_t channel_number;
	if (ipmi_sdr_parse_fru_device_locator_parameters(ctx, NULL, 0,
			&device_access_address,
			&logical_fru_device_device_slave_address,
			&private_bus_id,
			&lun_for_master_write_read_fru_command,
			&logical_physical_fru_device,
			&channel_number) < 0)
		RETMSG(-1, "ipmi_sdr_parse_fru_device_locator_parameters() failed");

	uint8_t &fru = logical_fru_device_device_slave_address;

	if (logical_physical_fru_device) {
		/*
		 * Limit to interesting FRUs
		 */
		if (!(
				(fru == 2 || fru == 3) // MCH
				|| (fru >= 5 && fru <= 16) // AMC 1-12
				|| (fru == 30 || fru == 29) // AMC 13/14
				|| (fru == 40 || fru == 41) // CU
				|| (fru >= 50 && fru <= 53) // PM
			 ))
			return 0; // If !in_list, disinterested.

		if (!data->crate->get_card(fru)) {
			try {
				bool found = false;
				for (std::vector<cardmodule_t>::iterator it = card_modules.begin(); it != card_modules.end(); it++) {
					Card *newcard = it->instantiate_card(data->crate, device_id, sdrbuf, sdrbuflen);
					if (newcard) {
						data->crate->add_card(newcard);
						found = true;
						break;
					}
				}
				if (!found)
					data->crate->add_card(new Card(data->crate, device_id, sdrbuf, sdrbuflen));
			}
			catch (std::exception& e) {
				data->errors++;
				mprintf("C%d: SDR FRU: Caught System Exception: %s.\n", data->crate->get_number(), typeid(typeof(e)).name());
				return 0;
			}
			catch (Sysmgr_Exception& e) {
				data->errors++;
				if (e.get_message().c_str()[0] != '\0')
					mprintf("C%d: SDR FRU: Caught %s on %s:%d in %s(): %s\n", data->crate->get_number(), e.get_type().c_str(), e.get_file().c_str(), e.get_line(), e.get_func().c_str(), e.get_message().c_str());
				else
					mprintf("C%d: SDR FRU: Caught %s on %s:%d in %s()\n", data->crate->get_number(), e.get_type().c_str(), e.get_file().c_str(), e.get_line(), e.get_func().c_str());
				return 0;
			}
		}
		data->activeslots[fru] = true;
	}

	return 0;
}

typedef struct {
	std::map< uint8_t, std::map<uint8_t, std::string> > oldsensors;
	Crate *crate;
	int errors;
} identify_sensors_cb_data_t;

int crate__identify_sensors_cb(ipmi_sdr_ctx_t ctx,
		uint8_t record_type,
		const void *sdr_record,
		unsigned int sdr_record_len,
		void *cb_data)
{
	identify_sensors_cb_data_t *data = (identify_sensors_cb_data_t*)cb_data;
	int rv;

	if (record_type != IPMI_SDR_FORMAT_FULL_SENSOR_RECORD
			&& record_type != IPMI_SDR_FORMAT_COMPACT_SENSOR_RECORD
			&& record_type != IPMI_SDR_FORMAT_EVENT_ONLY_RECORD)
		return 0;

	uint8_t entity_id;
	uint8_t entity_instance;
	uint8_t entity_instance_type;
	rv = ipmi_sdr_parse_entity_id_instance_type(ctx, NULL, 0,
			&entity_id, &entity_instance, &entity_instance_type);
	if (rv < 0)
		RETMSG(-1, "ipmi_sdr_parse_entity_id_instance_type() failed");

	uint8_t sensor_owner_id_type;
	uint8_t sensor_owner_id;
	rv = ipmi_sdr_parse_sensor_owner_id(ctx, NULL, 0, &sensor_owner_id_type, &sensor_owner_id);
	if (rv < 0)
		RETMSG(-1, "ipmi_sdr_parse_sensor_owner_id() failed");

	uint8_t sensor_number;
	rv = ipmi_sdr_parse_sensor_number(ctx, NULL, 0, &sensor_number);
	if (rv < 0)
		RETMSG(-1, "ipmi_sdr_parse_sensor_number() failed");

	char namebuf[64] = "";
	rv = ipmi_sdr_parse_sensor_name(ctx, NULL, 0,
				sensor_number, 0, namebuf, 64);
	if (rv < 0)
		RETMSG(rv, "ipmi_sdr_parse_sensor_name() failed: %d", rv);

	uint8_t share_count = 1;
	if (record_type == IPMI_SDR_FORMAT_COMPACT_SENSOR_RECORD) {
		rv = ipmi_sdr_parse_sensor_record_sharing(ctx, NULL, 0, &share_count, NULL, NULL, NULL);
		if (rv < 0)
			RETMSG(rv, "ipmi_sdr_parse_sensor_record_sharing() failed: (%d) %s", ipmi_sdr_ctx_errnum(ctx), ipmi_sdr_ctx_strerror(ipmi_sdr_ctx_errnum(ctx)));
		if (share_count < 1)
			share_count = 1; // RETMSG(rv, "ipmi_sdr_parse_sensor_record_sharing() returned strange share count: %d", share_count);
	}

	Card *card = data->crate->get_card(entity_id, entity_instance);
	if (!card)
		return 0;

	try {
		for (int i = 0; i < share_count; i++) {
			std::string oldname = data->oldsensors[card->get_fru()][sensor_number+i];
			if (!card->get_sensor(sensor_number+i) || card->get_sensor(sensor_number+i)->get_name() != oldname)
				card->add_sensor(sensor_number+i, sdr_record, sdr_record_len);

			data->oldsensors[card->get_fru()].erase(sensor_number+i);
		}
	}
	catch (std::exception& e) {
		data->errors++;
		mprintf("C%d: SDR Sensor: Caught System Exception: %s.\n", data->crate->get_number(), typeid(typeof(e)).name());
		return 0;
	}
	catch (Sysmgr_Exception& e) {
		data->errors++;
		if (e.get_message().c_str()[0] != '\0')
			mprintf("C%d: SDR Sensor: Caught %s on %s:%d in %s(): %s\n", data->crate->get_number(), e.get_type().c_str(), e.get_file().c_str(), e.get_line(), e.get_func().c_str(), e.get_message().c_str());
		else
			mprintf("C%d: SDR Sensor: Caught %s on %s:%d in %s()\n", data->crate->get_number(), e.get_type().c_str(), e.get_file().c_str(), e.get_line(), e.get_func().c_str());
		return 0;
	}

	return 0;
}

void Crate::scan_sdr(void *cb_null)
{
	int rv;

	bool do_work = false;
	try {
		if (this->update_sdr_cache() || this->force_sdr_scan)
			this->sdr_scan_retries = 5;

		if (this->sdr_scan_retries) {
			do_work = true;
			this->sdr_scan_retries--;
		}
	}
	catch (SDRRepositoryNotPopulatedException& e) {
		dmprintf("C%d: Retrying SDR Scan: Not Populated\n", this->number);
		do_work = false;
	}
	if (!do_work) {
		this->sdrscan_id = THREADLOCAL.taskqueue.schedule(time(NULL)+1, callback<void>::create<Crate,&Crate::scan_sdr>(this), NULL);
		return;
	}
	this->force_sdr_scan = false;

	identify_slots_cb_data_t carddata;
	for (int i = 0; i < 256; i++)
		carddata.activeslots[i] = false;
	carddata.crate = this;
	carddata.errors = 0;

	rv = ipmi_sdr_cache_iterate(this->ctx.sdr, crate__identify_slots_cb, &carddata);
	if (rv == -1)
		THROWMSG(IPMI_LibraryError, "ipmi_sdr_cache_iterate() returned %d", rv);
	if (rv < -1)
		THROWMSG(Crate_Exception, "ipmi_sdr_cache_iterate() returned %d", rv);

	for (int i = 0; i < 256; i++)
		if (!carddata.activeslots[i])
			this->destroy_card(i);

	identify_sensors_cb_data_t sensordata;
	for (int i = 0; i < 256; i++) {
		if (!this->cards[i])
			continue;

		std::vector<Sensor*> sensors = this->cards[i]->get_sensors();
		for (std::vector<Sensor*>::iterator it = sensors.begin(); it != sensors.end(); it++) {
			sensordata.oldsensors[i][(*it)->get_sensor_number()] = (*it)->get_name();
		}
	}

	sensordata.crate = this;
	sensordata.errors = 0;
	rv = ipmi_sdr_cache_iterate(this->ctx.sdr, crate__identify_sensors_cb, &sensordata);
	if (rv == -1)
		THROWMSG(IPMI_LibraryError, "ipmi_sdr_cache_iterate() returned %d", rv);
	if (rv < -1)
		THROWMSG(Crate_Exception, "ipmi_sdr_cache_iterate() returned %d", rv);

	for (std::map< uint8_t, std::map<uint8_t, std::string> >::iterator cit = sensordata.oldsensors.begin(); cit != sensordata.oldsensors.end(); cit++) {
		for (std::map<uint8_t, std::string>::iterator sit = cit->second.begin(); sit != cit->second.end(); sit++) {
			this->cards[cit->first]->destroy_sensor(sit->first);
		}
	}

	if (!carddata.errors && !sensordata.errors)
		this->sdr_scan_retries = 0;
	else if (this->sdr_scan_retries == 0)
		mprintf("C%d: Out of retries parsing the SDR entries.  %d errors in card processing, %d errors in sensor processing.\n", this->number, carddata.errors, sensordata.errors);
	else
		mprintf("C%d: Issues parsing the SDR entries.  %d errors in card processing, %d errors in sensor processing.  %d retries remaining.\n", this->number, carddata.errors, sensordata.errors, this->sdr_scan_retries);

	this->sdrscan_id = THREADLOCAL.taskqueue.schedule(time(NULL)+1, callback<void>::create<Crate,&Crate::scan_sdr>(this), NULL);
}

void Crate::ipmi_connect()
{
	int rv;

	if (ctx.ipmi)
		this->ipmi_disconnect();

	ctx.ipmi = NULL;
	ctx.sdr = NULL;
	ctx.sel = NULL;
#ifdef INTERPRET_CTX
	ctx.interpret = NULL;
#endif
	ctx.sensor_read = NULL;

	ctx.ipmi = ipmi_ctx_create();
	if (!ctx.ipmi) {
		this->ipmi_disconnect();
		THROWMSG(IPMI_LibraryError, "ipmi_ctx_create() failed");
	}

	if (MCH != NAT) {
		rv = ipmi_ctx_open_outofband_2_0(ctx.ipmi,
				this->ip.c_str(),			// hostname
				this->user,					// username
				this->pass,					// password
				NULL,						// k_g
				0,							// k_g_len,
				4,							// privilege_level
				0,							// cipher_suite_id
				0,							// session_timeout
				5,							// retransmission_timeout
				IPMI_WORKAROUND_FLAGS_OUTOFBAND_2_0_OPEN_SESSION_PRIVILEGE,	// workaround_flags
#ifdef IPMI_TRACE
				IPMI_FLAGS_DEBUG_DUMP		// flags
#else
				IPMI_FLAGS_DEFAULT			// flags
#endif
				);

		if (rv) {
			this->ipmi_disconnect();
			THROWMSG(IPMI_ConnectionFailedException, "ipmi_ctx_open_outofband_2_0: rv = %d; errno = %d (%s)", rv, ipmi_ctx_errnum(ctx.ipmi), ipmi_ctx_errnum(ctx.ipmi) ? ipmi_ctx_strerror(ipmi_ctx_errnum(ctx.ipmi)) : "");
		}
	}
	else {
		rv = ipmi_ctx_open_outofband(ctx.ipmi,
				this->ip.c_str(),					// hostname
				this->user,							// username
				this->pass,							// password
				this->ipmi15_authentication_type,	// authentication_type
				4,									// privilege_level
				0,									// session_timeout
				5,									// retransmission_timeout
				IPMI_WORKAROUND_FLAGS_DEFAULT,		// workaround_flags
#ifdef IPMI_TRACE
				IPMI_FLAGS_DEBUG_DUMP				// flags
#else
				IPMI_FLAGS_DEFAULT					// flags
#endif
				);
		if (rv) {
			this->ipmi_disconnect();
			THROWMSG(IPMI_ConnectionFailedException, "ipmi_ctx_open_outofband: rv = %d; errno = %d (%s)", rv, ipmi_ctx_errnum(ctx.ipmi), ipmi_ctx_errnum(ctx.ipmi) ? ipmi_ctx_strerror(ipmi_ctx_errnum(ctx.ipmi)) : "");
		}
	}

#ifdef DEBUG_ONESHOT
	return;
#endif

	this->update_sdr_cache();

#ifdef INTERPRET_CTX
	ctx.interpret = ipmi_interpret_ctx_create();
	if (!ctx.interpret) {
		this->ipmi_disconnect();
		THROWMSG(IPMI_LibraryError, "ipmi_interpret_ctx_create() failed");
	}
	ipmi_sel_ctx_set_parameter(ctx.sel, IPMI_SEL_PARAMETER_INTERPRET_CONTEXT, ctx.interpret);
#endif

	ctx.sensor_read = ipmi_sensor_read_ctx_create(ctx.ipmi);
	if (!ctx.sensor_read) {
		this->ipmi_disconnect();
		THROWMSG(IPMI_LibraryError, "ipmi_sensor_read_ctx_create() failed");
	}
	ipmi_sensor_read_ctx_set_flags(ctx.sensor_read, IPMI_SENSOR_READ_FLAGS_BRIDGE_SENSORS|IPMI_SENSOR_READ_FLAGS_IGNORE_SCANNING_DISABLED);


	/* Initialize SEL scanner, to not rescan old events */

	fiid_obj_t obj_cmd_rs = fiid_obj_create(tmpl_cmd_get_sel_info_rs);
	if (ipmi_cmd_get_sel_info(this->ctx.ipmi, obj_cmd_rs))
		THROWMSG(IPMI_LibraryError, "ipmi_cmd_get_sel_info: errno %d (%s)", ipmi_ctx_errnum(this->ctx.ipmi), ipmi_ctx_errormsg(this->ctx.ipmi));

	fiid_obj_get(obj_cmd_rs, "most_recent_erase_timestamp", &this->selscan_lastclr);
	fiid_obj_get(obj_cmd_rs, "entries", &this->selscan_nextent);
	fiid_obj_destroy(obj_cmd_rs);

	this->force_sdr_scan = true;
	this->scan_sel(NULL);
	this->scan_sdr(NULL);
	if (this->force_sdr_scan) {
		mprintf("C%d: Crate not yet ready.  Monitoring.\n", this->number);
	}

	// Notify cards and sensors of the new connection.
	for (int i = 0; i < 256; i++) {
		if (!this->cards[i])
			continue;
		this->cards[i]->crate_connected();
		std::vector<Sensor*> sensors = this->cards[i]->get_sensors();
		for (std::vector<Sensor*>::iterator it = sensors.begin(); it != sensors.end(); it++)
			(*it)->crate_connected();
	}
}

void Crate::ipmi_disconnect()
{
	if (this->selscan_id) {
		THREADLOCAL.taskqueue.cancel(this->selscan_id);
		this->selscan_id = 0;
	}

	if (this->sdrscan_id) {
		THREADLOCAL.taskqueue.cancel(this->sdrscan_id);
		this->sdrscan_id = 0;
	}

	if (ctx.sensor_read)
		ipmi_sensor_read_ctx_destroy(ctx.sensor_read);

#ifdef INTERPRET_CTX
	if (ctx.interpret)
		ipmi_interpret_ctx_destroy(ctx.interpret);
#endif

	if (ctx.sel)
		ipmi_sel_ctx_destroy(ctx.sel);

	if (ctx.sdr) {
		ipmi_sdr_cache_close(ctx.sdr);
		ipmi_sdr_ctx_destroy(ctx.sdr);
	}

	if (ctx.ipmi) {
		ipmi_ctx_close(ctx.ipmi);
		ipmi_ctx_destroy(ctx.ipmi);
	}

	ctx.ipmi = NULL;
	ctx.sdr = NULL;
	ctx.sel = NULL;
#ifdef INTERPRET_CTX
	ctx.interpret = NULL;
#endif
	ctx.sensor_read = NULL;
}

int Crate::send_bridged(uint8_t br_channel, uint8_t br_addr, uint8_t channel, uint8_t addr, uint8_t netfn, fiid_obj_t msg_rq, fiid_obj_t msg_rs)
{
	this->sendmessage_seq++;
	this->sendmessage_seq %= 64;

	int rv = 0;

	if (!fiid_obj_valid(msg_rq))
		return -1;
	if (msg_rs && !fiid_obj_valid(msg_rs))
		return -1;

	fiid_obj_t obj_true_hdr_rq = fiid_obj_create(tmpl_ipmb_msg_hdr_rq);
	fiid_obj_t obj_true_rq = fiid_obj_create(tmpl_ipmb_msg);

	/* BEGIN HEADERS */

	fiid_obj_clear(obj_true_hdr_rq);
	fiid_obj_set(obj_true_hdr_rq, "rs_addr", addr);
	fiid_obj_set(obj_true_hdr_rq, "net_fn", netfn);
	fiid_obj_set(obj_true_hdr_rq, "rs_lun", 0);

	int checksum_len;
	uint8_t checksum_buf[1024];
	checksum_len = fiid_obj_get_block(obj_true_hdr_rq, "rs_addr", "net_fn", checksum_buf, 1024);

	fiid_obj_set(obj_true_hdr_rq, "checksum1", ipmi_checksum(checksum_buf, checksum_len));
	fiid_obj_set(obj_true_hdr_rq, "rq_addr", 0x20);
	fiid_obj_set(obj_true_hdr_rq, "rq_lun", 0);
	fiid_obj_set(obj_true_hdr_rq, "rq_seq", this->sendmessage_seq);

	/* END HEADERS */

	assemble_ipmi_ipmb_msg(obj_true_hdr_rq, msg_rq, obj_true_rq, IPMI_INTERFACE_FLAGS_DEFAULT);

	/* END ASSEMBLY */

	uint8_t msgbuf[1024];
	int msglen = fiid_obj_get_all(obj_true_rq, msgbuf, 1024);

	/* BEGIN SendMessage COMMAND */

	fiid_obj_t obj_sm_cmd_rq = fiid_obj_create(tmpl_cmd_send_message_rq);
	fiid_obj_t obj_sm_cmd_rs = fiid_obj_create(tmpl_cmd_send_message_rs);

	fill_cmd_send_message(
			7,
			IPMI_SEND_MESSAGE_AUTHENTICATION_NOT_REQUIRED,
			IPMI_SEND_MESSAGE_ENCRYPTION_NOT_REQUIRED,
			IPMI_SEND_MESSAGE_TRACKING_OPERATION_TRACKING_REQUEST,
			msgbuf,
			msglen,
			obj_sm_cmd_rq);

	/* SEND! */

	if ((rv = ipmi_cmd_ipmb(this->ctx.ipmi,
			br_channel,
			br_addr,
			0,
			IPMI_NET_FN_APP_RQ,
			obj_sm_cmd_rq,
			obj_sm_cmd_rs))) {
		fiid_obj_destroy(obj_true_hdr_rq);
		fiid_obj_destroy(obj_true_rq);
		fiid_obj_destroy(obj_sm_cmd_rq);
		fiid_obj_destroy(obj_sm_cmd_rs);
		return rv;
	}

	fiid_template_t tmpl_ipmb_hdr_rs =
	{
		{ 8, "rs_addr", FIID_FIELD_REQUIRED | FIID_FIELD_LENGTH_FIXED},
		{ 2, "rs_lun", FIID_FIELD_REQUIRED | FIID_FIELD_LENGTH_FIXED},
		{ 6, "net_fn", FIID_FIELD_REQUIRED | FIID_FIELD_LENGTH_FIXED},
		{ 8, "checksum1", FIID_FIELD_REQUIRED | FIID_FIELD_LENGTH_FIXED},
		{ 8, "rq_addr", FIID_FIELD_REQUIRED | FIID_FIELD_LENGTH_FIXED},
		{ 2, "rq_lun", FIID_FIELD_REQUIRED | FIID_FIELD_LENGTH_FIXED},
		{ 6, "rq_seq", FIID_FIELD_REQUIRED | FIID_FIELD_LENGTH_FIXED},
		{ 8, "cmd", FIID_FIELD_REQUIRED | FIID_FIELD_LENGTH_FIXED},
		{ 8, "completion_code", FIID_FIELD_REQUIRED | FIID_FIELD_LENGTH_FIXED},
		{ 0, "", 0}
	};

	fiid_obj_t msg_rs_hdr = fiid_obj_create(tmpl_ipmb_hdr_rs);
	fiid_obj_clear(msg_rs_hdr);
	msglen = fiid_obj_get_data(obj_sm_cmd_rs, "response_data", msgbuf, 1024);

	uint8_t *msgstart = msgbuf;

	if (msglen < 7 || ipmi_checksum(msgstart, 3) || ipmi_checksum(msgstart, msglen)) {
		fiid_obj_destroy(obj_true_hdr_rq);
		fiid_obj_destroy(obj_true_rq);
		fiid_obj_destroy(obj_sm_cmd_rq);
		fiid_obj_destroy(obj_sm_cmd_rs);
		fiid_obj_destroy(msg_rs_hdr);
		return -1;
	}

	if (this->MCH == NAT) {
		msgstart += 7;
		msglen -= 7;
		msglen--; // Outer Message Checksum
		if (msglen < 7 || ipmi_checksum(msgstart, 3) || ipmi_checksum(msgstart, msglen)) {
			fiid_obj_destroy(obj_true_hdr_rq);
			fiid_obj_destroy(obj_true_rq);
			fiid_obj_destroy(obj_sm_cmd_rq);
			fiid_obj_destroy(obj_sm_cmd_rs);
			fiid_obj_destroy(msg_rs_hdr);
			return -1;
		}
	}


	fiid_obj_set_all(msg_rs_hdr, msgstart, 7); // Up to final checksum

	uint64_t cmpl_code;
	fiid_obj_get(msg_rs_hdr, "completion_code", &cmpl_code);
	if (cmpl_code) {
		fiid_obj_destroy(obj_true_hdr_rq);
		fiid_obj_destroy(obj_true_rq);
		fiid_obj_destroy(obj_sm_cmd_rq);
		fiid_obj_destroy(obj_sm_cmd_rs);
		fiid_obj_destroy(msg_rs_hdr);
		return cmpl_code;
	}

	if (msg_rs) {
		fiid_obj_clear(msg_rs);
		fiid_obj_set_all(msg_rs, msgstart+7, msglen-8);
	}

	fiid_obj_destroy(obj_true_hdr_rq);
	fiid_obj_destroy(obj_true_rq);
	fiid_obj_destroy(obj_sm_cmd_rq);
	fiid_obj_destroy(obj_sm_cmd_rs);
	fiid_obj_destroy(msg_rs_hdr);

	return 0;
}

Crate::~Crate()
{
	this->ipmi_disconnect();

	unlink(this->sdrfile);

	for (int i = 1; i < 14; i++)
		this->destroy_card(i);

	if (this->user)
		delete this->user;

	if (this->pass)
		delete this->pass;
}

Card::Card(Crate *crate, std::string name, void *sdrbuf, uint8_t sdrbuflen)
	: crate(crate), name(name)
{
	int rv;

	assert(sdrbuflen && sdrbuflen <= 64);
	memcpy(this->sdrbuf, sdrbuf, sdrbuflen);
	this->sdrbuflen = sdrbuflen;

	uint8_t fru_entity_id;
	uint8_t fru_entity_instance;
	rv = ipmi_sdr_parse_fru_entity_id_and_instance(crate->ctx.sdr, sdrbuf, sdrbuflen,
			&fru_entity_id, &fru_entity_instance);
	if (rv < 0)
		THROWMSG(IPMI_LibraryError, "ipmi_sdr_parse_fru_entity_id_and_instance() failed");

	uint8_t device_access_address;
	uint8_t logical_fru_device_device_slave_address;
	uint8_t private_bus_id;
	uint8_t lun_for_master_write_read_fru_command;
	uint8_t logical_physical_fru_device;
	uint8_t channel_number;

	rv = ipmi_sdr_parse_fru_device_locator_parameters(crate->ctx.sdr, sdrbuf, sdrbuflen,
			&device_access_address,
			&logical_fru_device_device_slave_address,
			&private_bus_id,
			&lun_for_master_write_read_fru_command,
			&logical_physical_fru_device,
			&channel_number);
	if (rv < 0)
		THROWMSG(IPMI_LibraryError, "ipmi_sdr_parse_fru_device_locator_parameters() failed");

	this->fru = logical_fru_device_device_slave_address;
	this->entity_id = fru_entity_id;
	this->entity_instance = fru_entity_instance;
	this->access_addr = device_access_address << 1;
	this->channel = channel_number;
	if (this->crate->get_mch() == Crate::NAT) {
		// Work around incorrect reporting by NAT MCH
		this->channel = 7;
		this->access_addr = 0x82;
	}
}


Card::~Card() {
	for (std::map<uint8_t,Sensor*>::iterator it = this->sensors.begin(); it != this->sensors.end(); it++)
		delete it->second;
	this->sensors.erase(this->sensors.begin(), this->sensors.end());
}

Sensor *Card::get_sensor(uint8_t number)
{
	if (this->sensors.find(number) != this->sensors.end())
		return this->sensors[number];
	else
		return NULL;
}

Sensor *Card::get_sensor(const std::string name)
{
	for (std::map<uint8_t,Sensor*>::iterator it = this->sensors.begin(); it != this->sensors.end(); it++) {
		if (it->second->get_name() == name)
			return it->second;
	}
	return NULL;
}

HotswapSensor *Card::get_hotswap_sensor()
{
	for (std::map<uint8_t,Sensor*>::iterator it = this->sensors.begin(); it != this->sensors.end(); it++) {
		if (it->second->get_sensor_type() == 0xf0)
			return dynamic_cast<HotswapSensor*>(it->second);
	}
	return NULL;
}

std::vector<Sensor*> Card::get_sensors()
{
	std::vector<Sensor*> sensors;

	for (std::map<uint8_t,Sensor*>::iterator it = this->sensors.begin(); it != this->sensors.end(); it++)
		sensors.push_back(it->second);

	return sensors;
}

void Card::add_sensor(uint8_t sensor_number, const void *sdr_record, uint8_t sdr_record_len)
{
	Sensor *sensor = this->instantiate_sensor(sensor_number, sdr_record, sdr_record_len);
	assert(sensor);

	if (this->sensors.find(sensor->get_sensor_number()) != this->sensors.end())
		this->destroy_sensor(sensor->get_sensor_number());

	// mprintf("C%d: Sensor added to the %s in %s: %s\n", this->crate->get_number(), this->name.c_str(), this->get_slotstring().c_str(), sensor->get_name().c_str());
	this->sensors[sensor->get_sensor_number()] = sensor;
}

Sensor *Card::instantiate_sensor(uint8_t sensor_number, const void *sdr, uint8_t sdrlen)
{
	/* This function allows each card to instantiate sensors of appropriate
	 * subclasses for that card's function.
	 *
	 * By default, we only have hotswap, and basic sensors.
	 */

	int rv;

	uint8_t sensor_type;
	rv = ipmi_sdr_parse_sensor_type(this->crate->ctx.sdr, NULL,0,
			&sensor_type);
	if (rv < 0)
		THROWMSG(IPMI_LibraryError, "ipmi_sdr_parse_sensor_type() failed");

	if (sensor_type == 0xf0

			// NAT WORKAROUND: Hotswap Sensors on MCH are fake and unreadable.
			&& !(this->crate->get_mch() == Crate::NAT
				&& this->name == "NAT-MCH-MCMC")
	   )
		return new HotswapSensor(this, sensor_number, sdr, sdrlen);

	return new Sensor(this, sensor_number, sdr, sdrlen);
}

void Card::destroy_sensor(uint8_t number)
{
	std::map<uint8_t,Sensor*>::iterator it = this->sensors.find(number);
	if (it != this->sensors.end()) {
		delete it->second;
		this->sensors.erase(it);
	}
}

void Card::set_led_state(uint8_t led_id, uint8_t function, uint8_t ontime)
{
	fiid_template_t tmpl_picmg_set_fru_led_state_rq =
	{ 
		{ 8, "cmd", FIID_FIELD_REQUIRED | FIID_FIELD_LENGTH_FIXED},
		{ 8, "picmg_zero", FIID_FIELD_REQUIRED | FIID_FIELD_LENGTH_FIXED},
		{ 8, "picmg_fru", FIID_FIELD_REQUIRED | FIID_FIELD_LENGTH_FIXED},
		{ 8, "led_id", FIID_FIELD_REQUIRED | FIID_FIELD_LENGTH_FIXED},
		{ 8, "function", FIID_FIELD_REQUIRED | FIID_FIELD_LENGTH_FIXED},
		{ 8, "on_duration", FIID_FIELD_REQUIRED | FIID_FIELD_LENGTH_FIXED},
		{ 8, "color", FIID_FIELD_REQUIRED | FIID_FIELD_LENGTH_FIXED},
		{ 0, "", 0}
	};

	fiid_obj_t setled_rq = fiid_obj_create(tmpl_picmg_set_fru_led_state_rq);
	fiid_obj_set(setled_rq, "cmd", 0x07);
	fiid_obj_set(setled_rq, "picmg_zero", 0);
	fiid_obj_set(setled_rq, "picmg_fru", this->fru);
	fiid_obj_set(setled_rq, "led_id", led_id);
	fiid_obj_set(setled_rq, "function", function);
	fiid_obj_set(setled_rq, "on_duration", ontime);
	fiid_obj_set(setled_rq, "color", 0x0f);

	//dmprintf("C%d: Sending LED Control to %s: %02x %02x %02x\n", this->crate->get_number(), this->get_slotstring().c_str(), led_id, function, ontime);
	this->crate->send_bridged(0, this->get_bridge_addr(), this->get_channel(), this->get_addr(), 0x2c, setled_rq, NULL);

	fiid_obj_destroy(setled_rq);
}

Sensor::Sensor(Card *card, uint8_t sensor_number, const void *sdrbuf, uint8_t sdrbuflen) : card(card), sensor_number(sensor_number)
{
	int rv;

	assert(sdrbuflen && sdrbuflen <= 64);
	memcpy(this->sdrbuf, sdrbuf, sdrbuflen);
	this->sdrbuflen = sdrbuflen;

	char namebuf[64] = "";
	rv = ipmi_sdr_parse_sensor_name(card->get_crate()->ctx.sdr, NULL, 0,
				sensor_number, 0, namebuf, 64);
	assert(rv >= 0);

	this->name = namebuf;
}

class ExceptionSafeFreer {
	public:
		void *mem;
		ExceptionSafeFreer(void *mem) : mem(mem) { };
		~ExceptionSafeFreer() { if (mem) free(mem); };
};

void Sensor::get_readings(uint8_t *raw, double **threshold, uint16_t *bitmask)
{
	Crate *crate = this->card->get_crate();
	int rv;

	uint8_t sdr_sensor_number;
	rv = ipmi_sdr_parse_sensor_number(crate->ctx.sdr, sdrbuf, sdrbuflen, &sdr_sensor_number);
	if (rv < 0)
		THROWMSG(IPMI_LibraryError, "ipmi_sdr_parse_sensor_number() failed: (%d) %s", ipmi_sdr_ctx_errnum(crate->ctx.sdr), ipmi_sdr_ctx_strerror(ipmi_sdr_ctx_errnum(crate->ctx.sdr)));

	rv = ipmi_sensor_read(crate->ctx.sensor_read, sdrbuf, sdrbuflen,
			this->sensor_number - sdr_sensor_number, // share offset
			raw,
			threshold,
			bitmask);
	*bitmask &= 0x7fff;

	if (rv < 0)
		THROWMSG(SensorReadingException, "ipmi_sensor_read(\"%s\" in %s, \"%s\") failed: (%d) %s", this->card->get_name().c_str(), this->card->get_slotstring().c_str(), this->name.c_str(), ipmi_sensor_read_ctx_errnum(crate->ctx.sensor_read), ipmi_sensor_read_ctx_strerror(ipmi_sensor_read_ctx_errnum(crate->ctx.sensor_read)));

	//dmprintf("C%u: %s in %s: Readings: %-16s   raw:%4hhu  read:%9.3lf  event:%04hx\n", CRATE_NO, this->card->get_name().c_str(), this->card->get_slotstring().c_str(), this->name.c_str(), *raw, (*threshold ? **threshold : 0), *bitmask);

	// I so wish C++ had finally{} blocks.
	ExceptionSafeFreer freer(threshold ? *threshold : NULL);
	this->values_read(*raw, *threshold, *bitmask);
	freer.mem = NULL; // Turns out we DO want to return it.
}

uint8_t Sensor::get_raw_reading()
{
	uint8_t raw;
	double *threshold;
	uint16_t bitmask;
	this->get_readings(&raw, &threshold, &bitmask);

	if (threshold)
		free(threshold);

	return raw;
}

double Sensor::get_threshold_reading()
{
	uint8_t raw;
	double *threshold;
	uint16_t bitmask;
	this->get_readings(&raw, &threshold, &bitmask);

	if (!threshold)
		THROW(SensorNotThresholdException);

	double val = *threshold;
	free(threshold);

	return val;
}

uint16_t Sensor::get_event_reading()
{
	uint8_t raw;
	double *threshold;
	uint16_t bitmask;
	this->get_readings(&raw, &threshold, &bitmask);

	if (threshold)
		free(threshold);

	return bitmask;
}

uint8_t Sensor::get_sensor_type()
{
	uint8_t sensor_type;
	if (ipmi_sdr_parse_sensor_type(this->card->get_crate()->ctx.sdr, this->sdrbuf, this->sdrbuflen,
			&sensor_type))
		THROWMSG(IPMI_LibraryError, "ipmi_sdr_parse_sensor_type() failed");
	return sensor_type;
}

char Sensor::get_sensor_reading_type()
{
	int rv;
	uint16_t record_id;
	uint8_t record_type;
	rv = ipmi_sdr_parse_record_id_and_type(this->card->get_crate()->ctx.sdr,
			this->sdrbuf, this->sdrbuflen,
			&record_id, &record_type);
	if (rv < 0)
		THROWMSG(IPMI_LibraryError, "ipmi_sdr_parse_record_id_and_type(\"%s\" (%s), \"%s\") failed: (%d) %s", this->card->get_name().c_str(), this->card->get_slotstring().c_str(), this->name.c_str(), ipmi_sdr_ctx_errnum(this->card->get_crate()->ctx.sdr), ipmi_sdr_ctx_strerror(ipmi_sdr_ctx_errnum(this->card->get_crate()->ctx.sdr)));

	uint8_t event_reading_type_code;
	rv = ipmi_sdr_parse_event_reading_type_code(this->card->get_crate()->ctx.sdr,
			this->sdrbuf, this->sdrbuflen,
			&event_reading_type_code);
	if (rv < 0)
		THROWMSG(IPMI_LibraryError, "ipmi_sdr_parse_event_reading_type_code(\"%s\" (%s), \"%s\") failed: (%d) %s", this->card->get_name().c_str(), this->card->get_slotstring().c_str(), this->name.c_str(), ipmi_sdr_ctx_errnum(this->card->get_crate()->ctx.sdr), ipmi_sdr_ctx_strerror(ipmi_sdr_ctx_errnum(this->card->get_crate()->ctx.sdr)));

	if (IPMI_EVENT_READING_TYPE_CODE_IS_OEM(event_reading_type_code))
		return 'O';
	else if (record_type == IPMI_SDR_FORMAT_EVENT_ONLY_RECORD)
		return 'E';
	else if (IPMI_EVENT_READING_TYPE_CODE_IS_THRESHOLD(event_reading_type_code))
		return 'T';
	else
		return 'D';
}

std::string Sensor::get_long_units()
{
	int rv;

	uint8_t base_units;
	rv = ipmi_sdr_parse_sensor_units(this->card->get_crate()->ctx.sdr,
			this->sdrbuf, this->sdrbuflen,
			NULL, NULL, NULL, &base_units, NULL);
	if (rv < 0)
		return "";
	return ipmi_sensor_units[base_units];
}

std::string Sensor::get_short_units()
{
	int rv;

	uint8_t base_units;
	rv = ipmi_sdr_parse_sensor_units(this->card->get_crate()->ctx.sdr,
			this->sdrbuf, this->sdrbuflen,
			NULL, NULL, NULL, &base_units, NULL);
	if (rv < 0)
		return "";
	return ipmi_sensor_units_abbreviated[base_units];
}

void Sensor::set_thresholds(threshold_data_t thresholds)
{
	int rv;

	uint8_t sensor_owner_id_type;
	uint8_t sensor_owner_id;
	rv = ipmi_sdr_parse_sensor_owner_id(this->card->get_crate()->ctx.sdr, this->sdrbuf, this->sdrbuflen, &sensor_owner_id_type, &sensor_owner_id);
	if (rv < 0)
		THROWMSG(IPMI_LibraryError, "ipmi_sdr_parse_sensor_owner_id(\"%s\" (%s), \"%s\") failed: (%d) %s", this->card->get_name().c_str(), this->card->get_slotstring().c_str(), this->name.c_str(), ipmi_sdr_ctx_errnum(this->card->get_crate()->ctx.sdr), ipmi_sdr_ctx_strerror(ipmi_sdr_ctx_errnum(this->card->get_crate()->ctx.sdr)));

	uint8_t sensor_owner_lun;
	uint8_t channel_number;
	rv = ipmi_sdr_parse_sensor_owner_lun(this->card->get_crate()->ctx.sdr, this->sdrbuf, this->sdrbuflen, &sensor_owner_lun, &channel_number);
	if (rv < 0)
		THROWMSG(IPMI_LibraryError, "ipmi_sdr_parse_sensor_owner_lun(\"%s\" (%s), \"%s\") failed: (%d) %s", this->card->get_name().c_str(), this->card->get_slotstring().c_str(), this->name.c_str(), ipmi_sdr_ctx_errnum(this->card->get_crate()->ctx.sdr), ipmi_sdr_ctx_strerror(ipmi_sdr_ctx_errnum(this->card->get_crate()->ctx.sdr)));

	fiid_obj_t set_thresholds_rq = fiid_obj_create(tmpl_cmd_set_sensor_thresholds_rq);
	fiid_obj_t set_thresholds_rs = fiid_obj_create(tmpl_cmd_set_sensor_thresholds_rs);

	if (fill_cmd_set_sensor_thresholds(this->sensor_number,
				(thresholds.lnc_set ? &thresholds.lnc : NULL),
				(thresholds.lc_set  ? &thresholds.lc  : NULL),
				(thresholds.lnr_set ? &thresholds.lnr : NULL),
				(thresholds.unc_set ? &thresholds.unc : NULL),
				(thresholds.uc_set  ? &thresholds.uc  : NULL),
				(thresholds.unr_set ? &thresholds.unr : NULL),
				set_thresholds_rq) < 0) {
		fiid_obj_destroy(set_thresholds_rq);
		fiid_obj_destroy(set_thresholds_rs);
		THROWMSG(IPMI_LibraryError, "fill_cmd_set_sensor_thresholds() failed");
	}

	rv = ipmi_cmd_ipmb(this->card->get_crate()->ctx.ipmi,
			sensor_owner_lun,
			sensor_owner_id << 1,
			0,
			IPMI_NET_FN_SENSOR_EVENT_RQ,
			set_thresholds_rq,
			set_thresholds_rs);
	if (rv < 0) {
		fiid_obj_destroy(set_thresholds_rq);
		fiid_obj_destroy(set_thresholds_rs);
		THROWMSG(IPMI_LibraryError, "ipmi_cmd_ipmb(\"%s\" (%s), \"%s\") failed: (%d) %s", this->card->get_name().c_str(), this->card->get_slotstring().c_str(), this->name.c_str(), ipmi_ctx_errnum(this->card->get_crate()->ctx.ipmi), ipmi_ctx_strerror(ipmi_ctx_errnum(this->card->get_crate()->ctx.ipmi)));
	}

	threshold_data_t ret;
	memset(&ret, 0, sizeof(ret));
	uint64_t val;

	fiid_obj_get(set_thresholds_rs, "comp_code", &val);
	if (val != 0) {
		fiid_obj_destroy(set_thresholds_rq);
		fiid_obj_destroy(set_thresholds_rs);
		THROWMSG(Crate_Exception, "ipmi_cmd_ipmb(\"%s\" (%s), \"%s\") returned completion code %02Xh", this->card->get_name().c_str(), this->card->get_slotstring().c_str(), this->name.c_str(), static_cast<uint32_t>(val));
	}

	fiid_obj_destroy(set_thresholds_rq);
	fiid_obj_destroy(set_thresholds_rs);
}

Sensor::threshold_data_t Sensor::get_thresholds()
{
	int rv;

	uint8_t sensor_owner_id_type;
	uint8_t sensor_owner_id;
	rv = ipmi_sdr_parse_sensor_owner_id(this->card->get_crate()->ctx.sdr, this->sdrbuf, this->sdrbuflen, &sensor_owner_id_type, &sensor_owner_id);
	if (rv < 0)
		THROWMSG(IPMI_LibraryError, "ipmi_sdr_parse_sensor_owner_id(\"%s\" (%s), \"%s\") failed: (%d) %s", this->card->get_name().c_str(), this->card->get_slotstring().c_str(), this->name.c_str(), ipmi_sdr_ctx_errnum(this->card->get_crate()->ctx.sdr), ipmi_sdr_ctx_strerror(ipmi_sdr_ctx_errnum(this->card->get_crate()->ctx.sdr)));

	uint8_t sensor_owner_lun;
	uint8_t channel_number;
	rv = ipmi_sdr_parse_sensor_owner_lun(this->card->get_crate()->ctx.sdr, this->sdrbuf, this->sdrbuflen, &sensor_owner_lun, &channel_number);
	if (rv < 0)
		THROWMSG(IPMI_LibraryError, "ipmi_sdr_parse_sensor_owner_lun(\"%s\" (%s), \"%s\") failed: (%d) %s", this->card->get_name().c_str(), this->card->get_slotstring().c_str(), this->name.c_str(), ipmi_sdr_ctx_errnum(this->card->get_crate()->ctx.sdr), ipmi_sdr_ctx_strerror(ipmi_sdr_ctx_errnum(this->card->get_crate()->ctx.sdr)));

	fiid_obj_t get_thresholds_rq = fiid_obj_create(tmpl_cmd_get_sensor_thresholds_rq);
	fiid_obj_t get_thresholds_rs = fiid_obj_create(tmpl_cmd_get_sensor_thresholds_rs);

	if (fill_cmd_get_sensor_thresholds(this->sensor_number, get_thresholds_rq) < 0) {
		fiid_obj_destroy(get_thresholds_rq);
		fiid_obj_destroy(get_thresholds_rs);
		THROWMSG(IPMI_LibraryError, "fill_cmd_get_sensor_thresholds() failed");
	}

	if (this->card->get_crate()->get_mch() == Crate::NAT) {
		rv = ipmi_cmd(this->card->get_crate()->ctx.ipmi,
				sensor_owner_lun,
				IPMI_NET_FN_SENSOR_EVENT_RQ,
				get_thresholds_rq,
				get_thresholds_rs);
	}
	else {
		rv = ipmi_cmd_ipmb(this->card->get_crate()->ctx.ipmi,
				sensor_owner_lun,
				sensor_owner_id << 1,
				0,
				IPMI_NET_FN_SENSOR_EVENT_RQ,
				get_thresholds_rq,
				get_thresholds_rs);
	}
	if (rv < 0) {
		fiid_obj_destroy(get_thresholds_rq);
		fiid_obj_destroy(get_thresholds_rs);
		THROWMSG(IPMI_LibraryError, "ipmi_cmd_ipmb(\"%s\" (%s), \"%s\") failed: (%d) %s", this->card->get_name().c_str(), this->card->get_slotstring().c_str(), this->name.c_str(), ipmi_ctx_errnum(this->card->get_crate()->ctx.ipmi), ipmi_ctx_strerror(ipmi_ctx_errnum(this->card->get_crate()->ctx.ipmi)));
	}

	threshold_data_t ret;
	memset(&ret, 0, sizeof(ret));
	uint64_t val;

	fiid_obj_get(get_thresholds_rs, "comp_code", &val);
	if (val != 0) {
		fiid_obj_destroy(get_thresholds_rq);
		fiid_obj_destroy(get_thresholds_rs);
		THROWMSG(Crate_Exception, "ipmi_cmd_ipmb(\"%s\" (%s), \"%s\") returned completion code %02Xh", this->card->get_name().c_str(), this->card->get_slotstring().c_str(), this->name.c_str(), static_cast<uint32_t>(val));
	}

	fiid_obj_get(get_thresholds_rs, "readable_thresholds.lower_non_critical_threshold", &val);    ret.lnc_set = bool(val);
	fiid_obj_get(get_thresholds_rs, "readable_thresholds.lower_critical_threshold", &val);        ret.lc_set  = bool(val);
	fiid_obj_get(get_thresholds_rs, "readable_thresholds.lower_non_recoverable_threshold", &val); ret.lnr_set = bool(val);

	fiid_obj_get(get_thresholds_rs, "readable_thresholds.upper_non_critical_threshold", &val);    ret.unc_set = bool(val);
	fiid_obj_get(get_thresholds_rs, "readable_thresholds.upper_critical_threshold", &val);        ret.uc_set  = bool(val);
	fiid_obj_get(get_thresholds_rs, "readable_thresholds.upper_non_recoverable_threshold", &val); ret.unr_set = bool(val);


	if (ret.lnc_set) fiid_obj_get(get_thresholds_rs, "lower_non_critical_threshold", &val);    ret.lnc = val;
	if (ret.lc_set)  fiid_obj_get(get_thresholds_rs, "lower_critical_threshold", &val);        ret.lnc = val;
	if (ret.lnr_set) fiid_obj_get(get_thresholds_rs, "lower_non_recoverable_threshold", &val); ret.lnc = val;

	if (ret.unc_set) fiid_obj_get(get_thresholds_rs, "upper_non_critical_threshold", &val);    ret.unc = val;
	if (ret.uc_set)  fiid_obj_get(get_thresholds_rs, "upper_critical_threshold", &val);        ret.uc  = val;
	if (ret.unr_set) fiid_obj_get(get_thresholds_rs, "upper_non_recoverable_threshold", &val); ret.unr = val;

	fiid_obj_destroy(get_thresholds_rq);
	fiid_obj_destroy(get_thresholds_rs);

	return ret;
}

void Sensor::set_event_enables(event_enable_data_t enables)
{
	int rv;

	uint8_t sensor_owner_id_type;
	uint8_t sensor_owner_id;
	rv = ipmi_sdr_parse_sensor_owner_id(this->card->get_crate()->ctx.sdr, this->sdrbuf, this->sdrbuflen, &sensor_owner_id_type, &sensor_owner_id);
	if (rv < 0)
		THROWMSG(IPMI_LibraryError, "ipmi_sdr_parse_sensor_owner_id(\"%s\" (%s), \"%s\") failed: (%d) %s", this->card->get_name().c_str(), this->card->get_slotstring().c_str(), this->name.c_str(), ipmi_sdr_ctx_errnum(this->card->get_crate()->ctx.sdr), ipmi_sdr_ctx_strerror(ipmi_sdr_ctx_errnum(this->card->get_crate()->ctx.sdr)));

	uint8_t sensor_owner_lun;
	uint8_t channel_number;
	rv = ipmi_sdr_parse_sensor_owner_lun(this->card->get_crate()->ctx.sdr, this->sdrbuf, this->sdrbuflen, &sensor_owner_lun, &channel_number);
	if (rv < 0)
		THROWMSG(IPMI_LibraryError, "ipmi_sdr_parse_sensor_owner_lun(\"%s\" (%s), \"%s\") failed: (%d) %s", this->card->get_name().c_str(), this->card->get_slotstring().c_str(), this->name.c_str(), ipmi_sdr_ctx_errnum(this->card->get_crate()->ctx.sdr), ipmi_sdr_ctx_strerror(ipmi_sdr_ctx_errnum(this->card->get_crate()->ctx.sdr)));

	fiid_obj_t set_event_enables_rq = fiid_obj_create(tmpl_cmd_set_sensor_event_enable_rq);
	fiid_obj_t set_event_enables_rs = fiid_obj_create(tmpl_cmd_set_sensor_event_enable_rs);
	
	// ENABLE STAGE

	if (fill_cmd_set_sensor_event_enable(this->sensor_number,
				IPMI_SENSOR_EVENT_MESSAGE_ACTION_ENABLE_SELECTED_EVENT_MESSAGES,
				(enables.scanning ? 1 : 0),
				(enables.events ? 1 : 0),
				enables.assert,
				enables.deassert,
				set_event_enables_rq) < 0) {
		fiid_obj_destroy(set_event_enables_rq);
		fiid_obj_destroy(set_event_enables_rs);
		THROWMSG(IPMI_LibraryError, "fill_cmd_set_sensor_event_enable() failed");
	}

	rv = ipmi_cmd_ipmb(this->card->get_crate()->ctx.ipmi,
			sensor_owner_lun,
			sensor_owner_id << 1,
			0,
			IPMI_NET_FN_SENSOR_EVENT_RQ,
			set_event_enables_rq,
			set_event_enables_rs);
	if (rv < 0) {
		fiid_obj_destroy(set_event_enables_rq);
		fiid_obj_destroy(set_event_enables_rs);
		THROWMSG(IPMI_LibraryError, "ipmi_cmd_ipmb(\"%s\" (%s), \"%s\") failed: (%d) %s", this->card->get_name().c_str(), this->card->get_slotstring().c_str(), this->name.c_str(), ipmi_ctx_errnum(this->card->get_crate()->ctx.ipmi), ipmi_ctx_strerror(ipmi_ctx_errnum(this->card->get_crate()->ctx.ipmi)));
	}

	uint64_t val;

	fiid_obj_get(set_event_enables_rs, "comp_code", &val);
	if (val != 0) {
		fiid_obj_destroy(set_event_enables_rq);
		fiid_obj_destroy(set_event_enables_rs);
		THROWMSG(Crate_Exception, "ipmi_cmd_ipmb(\"%s\" (%s), \"%s\") returned completion code %02Xh", this->card->get_name().c_str(), this->card->get_slotstring().c_str(), this->name.c_str(), static_cast<uint32_t>(val));
	}

	// DISABLE STAGE

	if (fill_cmd_set_sensor_event_enable(this->sensor_number,
				IPMI_SENSOR_EVENT_MESSAGE_ACTION_DISABLE_SELECTED_EVENT_MESSAGES,
				(enables.scanning ? 1 : 0),
				(enables.events ? 1 : 0),
				(~enables.assert) & 0x7fff,
				(~enables.deassert) & 0x7fff,
				set_event_enables_rq) < 0) {
		fiid_obj_destroy(set_event_enables_rq);
		fiid_obj_destroy(set_event_enables_rs);
		THROWMSG(IPMI_LibraryError, "fill_cmd_set_sensor_event_enable() failed");
	}

	rv = ipmi_cmd_ipmb(this->card->get_crate()->ctx.ipmi,
			sensor_owner_lun,
			sensor_owner_id << 1,
			0,
			IPMI_NET_FN_SENSOR_EVENT_RQ,
			set_event_enables_rq,
			set_event_enables_rs);
	if (rv < 0) {
		fiid_obj_destroy(set_event_enables_rq);
		fiid_obj_destroy(set_event_enables_rs);
		THROWMSG(IPMI_LibraryError, "ipmi_cmd_ipmb(\"%s\" (%s), \"%s\") failed: (%d) %s", this->card->get_name().c_str(), this->card->get_slotstring().c_str(), this->name.c_str(), ipmi_ctx_errnum(this->card->get_crate()->ctx.ipmi), ipmi_ctx_strerror(ipmi_ctx_errnum(this->card->get_crate()->ctx.ipmi)));
	}

	fiid_obj_get(set_event_enables_rs, "comp_code", &val);
	if (val != 0) {
		fiid_obj_destroy(set_event_enables_rq);
		fiid_obj_destroy(set_event_enables_rs);
		THROWMSG(Crate_Exception, "ipmi_cmd_ipmb(\"%s\" (%s), \"%s\") returned completion code %02Xh", this->card->get_name().c_str(), this->card->get_slotstring().c_str(), this->name.c_str(), static_cast<uint32_t>(val));
	}

	fiid_obj_destroy(set_event_enables_rq);
	fiid_obj_destroy(set_event_enables_rs);
}

Sensor::event_enable_data_t Sensor::get_event_enables()
{
	int rv;

	uint8_t sensor_owner_id_type;
	uint8_t sensor_owner_id;
	rv = ipmi_sdr_parse_sensor_owner_id(this->card->get_crate()->ctx.sdr, this->sdrbuf, this->sdrbuflen, &sensor_owner_id_type, &sensor_owner_id);
	if (rv < 0)
		THROWMSG(IPMI_LibraryError, "ipmi_sdr_parse_sensor_owner_id(\"%s\" (%s), \"%s\") failed: (%d) %s", this->card->get_name().c_str(), this->card->get_slotstring().c_str(), this->name.c_str(), ipmi_sdr_ctx_errnum(this->card->get_crate()->ctx.sdr), ipmi_sdr_ctx_strerror(ipmi_sdr_ctx_errnum(this->card->get_crate()->ctx.sdr)));

	uint8_t sensor_owner_lun;
	uint8_t channel_number;
	rv = ipmi_sdr_parse_sensor_owner_lun(this->card->get_crate()->ctx.sdr, this->sdrbuf, this->sdrbuflen, &sensor_owner_lun, &channel_number);
	if (rv < 0)
		THROWMSG(IPMI_LibraryError, "ipmi_sdr_parse_sensor_owner_lun(\"%s\" (%s), \"%s\") failed: (%d) %s", this->card->get_name().c_str(), this->card->get_slotstring().c_str(), this->name.c_str(), ipmi_sdr_ctx_errnum(this->card->get_crate()->ctx.sdr), ipmi_sdr_ctx_strerror(ipmi_sdr_ctx_errnum(this->card->get_crate()->ctx.sdr)));

	fiid_obj_t get_event_enables_rq = fiid_obj_create(tmpl_cmd_get_sensor_event_enable_rq);
	fiid_obj_t get_event_enables_rs = fiid_obj_create(tmpl_cmd_get_sensor_event_enable_rs);

	if (fill_cmd_get_sensor_event_enable(this->sensor_number, get_event_enables_rq) < 0) {
		fiid_obj_destroy(get_event_enables_rq);
		fiid_obj_destroy(get_event_enables_rs);
		THROWMSG(IPMI_LibraryError, "fill_cmd_get_sensor_event_enable() failed");
	}

	rv = ipmi_cmd_ipmb(this->card->get_crate()->ctx.ipmi,
			sensor_owner_lun,
			sensor_owner_id << 1,
			0,
			IPMI_NET_FN_SENSOR_EVENT_RQ,
			get_event_enables_rq,
			get_event_enables_rs);
	if (rv < 0) {
		fiid_obj_destroy(get_event_enables_rq);
		fiid_obj_destroy(get_event_enables_rs);
		THROWMSG(IPMI_LibraryError, "ipmi_cmd_ipmb(\"%s\" (%s), \"%s\") failed: (%d) %s", this->card->get_name().c_str(), this->card->get_slotstring().c_str(), this->name.c_str(), ipmi_ctx_errnum(this->card->get_crate()->ctx.ipmi), ipmi_ctx_strerror(ipmi_ctx_errnum(this->card->get_crate()->ctx.ipmi)));
	}

	event_enable_data_t ret;
	memset(&ret, 0, sizeof(ret));
	uint64_t val;

	fiid_obj_get(get_event_enables_rs, "comp_code", &val);
	if (val != 0) {
		fiid_obj_destroy(get_event_enables_rq);
		fiid_obj_destroy(get_event_enables_rs);
		THROWMSG(Crate_Exception, "ipmi_cmd_ipmb(\"%s\" (%s), \"%s\") returned completion code %02Xh", this->card->get_name().c_str(), this->card->get_slotstring().c_str(), this->name.c_str(), static_cast<uint32_t>(val));
	}

	fiid_obj_get(get_event_enables_rs, "scanning_on_this_sensor", &val);    ret.scanning = bool(val);
	fiid_obj_get(get_event_enables_rs, "all_event_messages", &val);    ret.events = bool(val);

	fiid_obj_get(get_event_enables_rs, "assertion_event_bitmask", &val); ret.assert = val;
	fiid_obj_get(get_event_enables_rs, "deassertion_event_bitmask", &val); ret.deassert = val;

	fiid_obj_destroy(get_event_enables_rq);
	fiid_obj_destroy(get_event_enables_rs);

	return ret;
}

Sensor::hysteresis_data_t Sensor::get_hysteresis()
{
	int rv;

	uint8_t sensor_owner_id_type;
	uint8_t sensor_owner_id;
	rv = ipmi_sdr_parse_sensor_owner_id(this->card->get_crate()->ctx.sdr, this->sdrbuf, this->sdrbuflen, &sensor_owner_id_type, &sensor_owner_id);
	if (rv < 0)
		THROWMSG(IPMI_LibraryError, "ipmi_sdr_parse_sensor_owner_id(\"%s\" (%s), \"%s\") failed: (%d) %s", this->card->get_name().c_str(), this->card->get_slotstring().c_str(), this->name.c_str(), ipmi_sdr_ctx_errnum(this->card->get_crate()->ctx.sdr), ipmi_sdr_ctx_strerror(ipmi_sdr_ctx_errnum(this->card->get_crate()->ctx.sdr)));

	uint8_t sensor_owner_lun;
	uint8_t channel_number;
	rv = ipmi_sdr_parse_sensor_owner_lun(this->card->get_crate()->ctx.sdr, this->sdrbuf, this->sdrbuflen, &sensor_owner_lun, &channel_number);
	if (rv < 0)
		THROWMSG(IPMI_LibraryError, "ipmi_sdr_parse_sensor_owner_lun(\"%s\" (%s), \"%s\") failed: (%d) %s", this->card->get_name().c_str(), this->card->get_slotstring().c_str(), this->name.c_str(), ipmi_sdr_ctx_errnum(this->card->get_crate()->ctx.sdr), ipmi_sdr_ctx_strerror(ipmi_sdr_ctx_errnum(this->card->get_crate()->ctx.sdr)));

	fiid_obj_t get_hysteresis_rq = fiid_obj_create(tmpl_cmd_get_sensor_hysteresis_rq);
	fiid_obj_t get_hysteresis_rs = fiid_obj_create(tmpl_cmd_get_sensor_hysteresis_rs);

	if (fill_cmd_get_sensor_hysteresis(this->sensor_number, 0xff, get_hysteresis_rq) < 0) {
		fiid_obj_destroy(get_hysteresis_rq);
		fiid_obj_destroy(get_hysteresis_rs);
		THROWMSG(IPMI_LibraryError, "fill_cmd_get_sensor_hysteresis() failed");
	}

	rv = ipmi_cmd_ipmb(this->card->get_crate()->ctx.ipmi,
			sensor_owner_lun,
			sensor_owner_id << 1,
			0,
			IPMI_NET_FN_SENSOR_EVENT_RQ,
			get_hysteresis_rq,
			get_hysteresis_rs);
	if (rv < 0) {
		fiid_obj_destroy(get_hysteresis_rq);
		fiid_obj_destroy(get_hysteresis_rs);
		THROWMSG(IPMI_LibraryError, "ipmi_cmd_ipmb(\"%s\" (%s), \"%s\") failed: (%d) %s", this->card->get_name().c_str(), this->card->get_slotstring().c_str(), this->name.c_str(), ipmi_ctx_errnum(this->card->get_crate()->ctx.ipmi), ipmi_ctx_strerror(ipmi_ctx_errnum(this->card->get_crate()->ctx.ipmi)));
	}

	hysteresis_data_t ret;
	memset(&ret, 0, sizeof(ret));
	uint64_t val;

	fiid_obj_get(get_hysteresis_rs, "comp_code", &val);
	if (val != 0) {
		fiid_obj_destroy(get_hysteresis_rq);
		fiid_obj_destroy(get_hysteresis_rs);
		THROWMSG(Crate_Exception, "ipmi_cmd_ipmb(\"%s\" (%s), \"%s\") returned completion code %02Xh", this->card->get_name().c_str(), this->card->get_slotstring().c_str(), this->name.c_str(), static_cast<uint32_t>(val));
	}

	fiid_obj_get(get_hysteresis_rs, "positive_going_threshold_hysteresis_value", &val); ret.goinghigh = val;
	fiid_obj_get(get_hysteresis_rs, "negative_going_threshold_hysteresis_value", &val); ret.goinglow = val;

	fiid_obj_destroy(get_hysteresis_rq);
	fiid_obj_destroy(get_hysteresis_rs);

	return ret;
}
