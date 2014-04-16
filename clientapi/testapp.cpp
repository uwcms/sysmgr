#include <stdarg.h>
#include <stdio.h>
#include <sys/time.h>
#include "sysmgr.h"

#undef	NOISY
#define	EXCEPTION_CHECK
#define	INTERACT
#define	THRESHOLD
#undef	SET_THRESHOLD
#undef	RAW

static inline std::string stdsprintf(const char *fmt, ...) {
	va_list va;
	va_list va2;
	va_start(va, fmt);
	va_copy(va2, va);
	size_t s = vsnprintf(NULL, 0, fmt, va);
	char str[s];
	vsnprintf(str, s+1, fmt, va2);
	va_end(va);
	va_end(va2);
	return std::string(str);
}

void event_print(const sysmgr::event& e) {
	printf("Event!\n\tFilter:\t%u\n\tCrate:\t%hhu\n\tFRU:\t%hhu\n\tCard:\t%s\n\tSensor:\t%s\n\tType:\t%s\n\tOffset:\t%hhu\n\n",
			e.filterid,
			e.crate,
			e.fru,
			e.card.c_str(),
			e.sensor.c_str(),
			(e.assertion ? "Assertion" : "Deassertion"),
			e.offset);
}

int main(int argc, char *argv[]) {
#ifdef EXCEPTION_CHECK
	try {
#endif
		sysmgr::sysmgr sm("localhost", "raw");

		sm.connect();

		uint8_t selected_crate = 0;
		std::vector<sysmgr::crate_info> crates = sm.list_crates();
		for (std::vector<sysmgr::crate_info>::iterator it = crates.begin(); it != crates.end(); it++) {
			if (it->connected && !selected_crate)
				selected_crate = it->crateno;
			printf("Crate %hhu:\tOnline:%u,\tMCH:%s,\tDescription: %s\n",
					it->crateno,
					(it->connected ? 1 : 0),
					it->mch.c_str(),
					it->description.c_str()
				  );
		}

#ifdef INTERACT
		printf("\nSelect Crate: ");
		scanf("%hhu", &selected_crate);
#endif

		printf("\n= Crate %hhu =\n", selected_crate);
		uint8_t selected_card = 0;
		std::vector<sysmgr::card_info> cards = sm.list_cards(selected_crate);
		for (std::vector<sysmgr::card_info>::iterator it = cards.begin(); it != cards.end(); it++) {
			if (it->fru > 4 && !selected_card)
				selected_card = it->fru;
			printf("FRU:%u,\tM%hhu,\tName: %s\n",
					it->fru,
					it->mstate,
					it->name.c_str()
				  );
		}

#ifdef INTERACT
		while (1) {
			printf("\nSelect FRU (0 exit): ");
			scanf("%hhu", &selected_card);
			if (!selected_card)
				break;
#endif

			printf("\n= Crate %hhu FRU %hhu =\n", selected_crate, selected_card);
			std::string frusdr = sm.get_fru_sdr(selected_crate, selected_card);
			printf("SDR:");
			for (std::string::iterator it = frusdr.begin(); it != frusdr.end(); it++)
				printf(" %02hhx", *it);
			printf("\n\n");

			std::vector<sysmgr::sensor_info> sensors = sm.list_sensors(selected_crate, selected_card);
			std::string selected_sensor;

#ifdef THRESHOLD
			sysmgr::sensor_thresholds thr;
			std::string thrsens;
#endif
			for (std::vector<sysmgr::sensor_info>::iterator it = sensors.begin(); it != sensors.end(); it++) {
				printf("Name:\"%s\",\tType:%c,\tSUnit: \"%s\", LUnit: \"%s\"",
						it->name.c_str(),
						it->type,
						it->shortunits.c_str(),
						it->longunits.c_str()
					  );

				if (it->type == 'T') {
					if (!selected_sensor.size())
						selected_sensor = it->name;

#ifdef THRESHOLD
					try {
						if (!thr.lnc_set && !thr.lc_set && !thr.lnr_set && !thr.unr_set && !thr.uc_set && !thr.unr_set) {
							thr = sm.get_sensor_thresholds(selected_crate, selected_card, it->name);
							thrsens = it->name;
						}
					}
					catch (sysmgr::remote_exception &e) {
						// Oh well.
					}
#endif

					sysmgr::sensor_reading reading = sm.sensor_read(selected_crate, selected_card, it->name);
					printf("\traw:0x%02hhx,", reading.raw);
					if (reading.threshold_set)
						printf(" value:%f,", reading.threshold);
					else
						printf(" value:-,");
					printf(" event:0x%04hx", reading.eventmask);
				}
				printf("\n");
			}

			printf("\n= Crate %hhu FRU %hhu Sensor \"%s\" =\n", selected_crate, selected_card, selected_sensor.c_str());
			std::string sensdr = sm.get_sensor_sdr(selected_crate, selected_card, selected_sensor);
			printf("SDR:");
			for (std::string::iterator it = sensdr.begin(); it != sensdr.end(); it++)
				printf(" %02hhx", *it);
			printf("\n");

#ifdef THRESHOLD
			printf("\n= Crate %hhu FRU %hhu Sensor \"%s\" =\n", selected_crate, selected_card, thrsens.c_str());
			printf("Thresholds:");
			if (thr.lnc_set) printf(" lnc:0x%02x,", thr.lnc); else printf(" lnc:-,");
			if (thr.lc_set) printf(" lc:0x%02x,", thr.lc); else printf(" lc:-,");
			if (thr.lnr_set) printf(" lnr:0x%02x,", thr.lnr); else printf(" lnr:-,");
			if (thr.unc_set) printf(" unc:0x%02x,", thr.unc); else printf(" unc:-,");
			if (thr.uc_set) printf(" uc:0x%02x,", thr.uc); else printf(" uc:-,");
			if (thr.unr_set) printf(" unr:0x%02x\n", thr.unr); else printf(" unr:-");
#ifdef SET_THRESHOLD
			thr.lnc_set = false;
			thr.lc_set = false;
			thr.lnr_set = false;
			thr.unc_set = false;
			thr.uc_set = false;
			thr.unr_set = true;
			thr.unr--;
			sm.set_sensor_thresholds(selected_crate, selected_card, thrsens, thr);

			thr = sm.get_sensor_thresholds(selected_crate, selected_card, thrsens);
			printf("Thresholds:");
			if (thr.lnc_set) printf(" lnc:0x%02x,", thr.lnc); else printf(" lnc:-,");
			if (thr.lc_set) printf(" lc:0x%02x,", thr.lc); else printf(" lc:-,");
			if (thr.lnr_set) printf(" lnr:0x%02x,", thr.lnr); else printf(" lnr:-,");
			if (thr.unc_set) printf(" unc:0x%02x,", thr.unc); else printf(" unc:-,");
			if (thr.uc_set) printf(" uc:0x%02x,", thr.uc); else printf(" uc:-,");
			if (thr.unr_set) printf(" unr:0x%02x\n", thr.unr); else printf(" unr:-");

			thr.lnc_set = false;
			thr.lc_set = false;
			thr.lnr_set = false;
			thr.unc_set = false;
			thr.uc_set = false;
			thr.unr_set = true;
			thr.unr++;
			sm.set_sensor_thresholds(selected_crate, selected_card, thrsens, thr);

			thr = sm.get_sensor_thresholds(selected_crate, selected_card, thrsens);
			printf("Thresholds:");
			if (thr.lnc_set) printf(" lnc:0x%02x,", thr.lnc); else printf(" lnc:-,");
			if (thr.lc_set) printf(" lc:0x%02x,", thr.lc); else printf(" lc:-,");
			if (thr.lnr_set) printf(" lnr:0x%02x,", thr.lnr); else printf(" lnr:-,");
			if (thr.unc_set) printf(" unc:0x%02x,", thr.unc); else printf(" unc:-,");
			if (thr.uc_set) printf(" uc:0x%02x,", thr.uc); else printf(" uc:-,");
			if (thr.unr_set) printf(" unr:0x%02x\n", thr.unr); else printf(" unr:-");
#endif
#endif

#ifdef RAW
			printf("\n= Crate %hhu FRU %hhu =\n", selected_crate, selected_card);
			std::string deviceid = sm.raw_card(selected_crate, selected_card, "\x06\x01");
			printf("Get Device ID:");
			for (std::string::iterator it = deviceid.begin(); it != deviceid.end(); it++)
				printf(" %02hhx", *it);
			printf("\n");

			deviceid = sm.raw_forwarded(selected_crate, 0, 0x82, 7, 0x70+(2*(selected_card-4)), "\x06\x01");
			printf("Get Device ID:");
			for (std::string::iterator it = deviceid.begin(); it != deviceid.end(); it++)
				printf(" %02hhx", *it);
			printf("\n");

			printf("\n= Shelf Manager =\n");
			deviceid = sm.raw_direct(selected_crate, 0, 0x20, "\x06\x01");
			printf("Get Device ID:");
			for (std::string::iterator it = deviceid.begin(); it != deviceid.end(); it++)
				printf(" %02hhx", *it);
			printf("\n");
#endif

#ifdef INTERACT
		}
#endif

		unsigned int seconds = 60;
#ifdef INTERACT
		printf("\nWatch for events for (seconds): ");
		scanf("%u", &seconds);

		printf("\n= Events =\n");
#endif

		//

		sm.register_event_filter(0xff, 0xff, "", "", 0x7fff, 0x7fff, event_print);
#if NOISY
		sm.register_event_filter(1, 0xff, "", "", 0x7fff, 0x7fff, event_print);
		sm.register_event_filter(2, 0xff, "", "", 0x7fff, 0x7fff, event_print);
		for (int i = 1; i < 13; i++)
			sm.register_event_filter(0xff, i, "", "", 0x7fff, 0x7fff, event_print);
#endif

		struct timeval timeout = { seconds, 0 };
		sm.process_events(&timeout);
#ifdef EXCEPTION_CHECK
	}
	catch (sysmgr::sysmgr_exception &e) {
		printf("ERROR: %s\n", e.message.c_str());
	}
#endif
}
