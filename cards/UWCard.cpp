#include <time.h>

#include "../sysmgr.h"
#include "../Callback.h"
#include "UWCard.h"

Sensor *UWCard::instantiate_sensor(uint8_t sensor_number, const void *sdr, uint8_t sdrlen)
{
	int rv;

	char namebuf[64] = "";
	rv = ipmi_sdr_parse_sensor_name(this->crate->ctx.sdr, sdr, sdrlen,
				sensor_number, 0, namebuf, 64);
	assert(rv >= 0);

	if (strcmp(namebuf, "FPGA Config") == 0)
		return new UW_FPGAConfig_Sensor(this, sensor_number, sdr, sdrlen);

	return Card::instantiate_sensor(sensor_number, sdr, sdrlen);
}

void UWCard::hotswap_event(uint8_t oldstate, uint8_t newstate)
{
	if (newstate == 4) {
		Sensor *fpgaconfig = this->get_sensor("FPGA Config");
		if (fpgaconfig)
			static_cast<UW_FPGAConfig_Sensor*>(fpgaconfig)->scan_sensor(3);
	}
}

void UWCard::set_clock()
{
	fiid_template_t tmpl_set_clock_rq =
	{ 
		{ 8, "cmd", FIID_FIELD_REQUIRED | FIID_FIELD_LENGTH_FIXED},
		{ 32, "time", FIID_FIELD_REQUIRED | FIID_FIELD_LENGTH_FIXED},
		{ 0, "", 0}
	};

	fiid_obj_t set_clock_rq = fiid_obj_create(tmpl_set_clock_rq);
	fiid_obj_set(set_clock_rq, "cmd", 0x29);
	fiid_obj_set(set_clock_rq, "time", time(NULL));

	this->crate->send_bridged(0, this->get_bridge_addr(), this->get_channel(), this->get_addr(), 0x32, set_clock_rq, NULL);

	fiid_obj_destroy(set_clock_rq);
}

void UWCard::configure_fpga()
{
	if (this->fpga_configure_in_progress)
		return;
	this->fpga_configure_in_progress = true;

	fiid_template_t tmpl_fpgaconfig_rq =
	{ 
		{ 8, "cmd", FIID_FIELD_REQUIRED | FIID_FIELD_LENGTH_FIXED},
		{ 8, "port", FIID_FIELD_REQUIRED | FIID_FIELD_LENGTH_FIXED},
		{ 16, "address", FIID_FIELD_REQUIRED | FIID_FIELD_LENGTH_FIXED},
		{ 8, "length", FIID_FIELD_REQUIRED | FIID_FIELD_LENGTH_FIXED},
		{ 8, "slotid", FIID_FIELD_REQUIRED | FIID_FIELD_LENGTH_FIXED},
		{ 8, "netmask1", FIID_FIELD_REQUIRED | FIID_FIELD_LENGTH_FIXED},
		{ 8, "netmask2", FIID_FIELD_REQUIRED | FIID_FIELD_LENGTH_FIXED},
		{ 8, "netmask3", FIID_FIELD_REQUIRED | FIID_FIELD_LENGTH_FIXED},
		{ 8, "netmask4", FIID_FIELD_REQUIRED | FIID_FIELD_LENGTH_FIXED},
		{ 8, "ip1", FIID_FIELD_REQUIRED | FIID_FIELD_LENGTH_FIXED},
		{ 8, "ip2", FIID_FIELD_REQUIRED | FIID_FIELD_LENGTH_FIXED},
		{ 8, "ip3", FIID_FIELD_REQUIRED | FIID_FIELD_LENGTH_FIXED},
		{ 8, "ip4", FIID_FIELD_REQUIRED | FIID_FIELD_LENGTH_FIXED},
		{ 16, "bootvector", FIID_FIELD_REQUIRED | FIID_FIELD_LENGTH_FIXED},
		{ 0, "", 0}
	};

	fiid_obj_t fpgaconfig_rq = fiid_obj_create(tmpl_fpgaconfig_rq);
	fiid_obj_set(fpgaconfig_rq, "cmd", 0x33);
	fiid_obj_set(fpgaconfig_rq, "port", 0);
	fiid_obj_set(fpgaconfig_rq, "address", 0);
	fiid_obj_set(fpgaconfig_rq, "length", 11);
	fiid_obj_set(fpgaconfig_rq, "slotid", this->get_fru()-4);
	fiid_obj_set(fpgaconfig_rq, "netmask1", 0xff);
	fiid_obj_set(fpgaconfig_rq, "netmask2", 0xff);
	fiid_obj_set(fpgaconfig_rq, "netmask3", 0xff);
	fiid_obj_set(fpgaconfig_rq, "netmask4", 0x00);
	fiid_obj_set(fpgaconfig_rq, "ip1", 192);
	fiid_obj_set(fpgaconfig_rq, "ip2", 168);
	fiid_obj_set(fpgaconfig_rq, "ip3", this->crate->get_number());
	fiid_obj_set(fpgaconfig_rq, "ip4", 40+this->get_fru()-4);
	fiid_obj_set(fpgaconfig_rq, "bootvector", 0);

	//dmprintf("Sending Message to 0, %02xh, %d, %02xh 0x32 MSG\n", this->get_bridge_addr(), this->get_channel(), this->get_addr());
	this->crate->send_bridged(0, this->get_bridge_addr(), this->get_channel(), this->get_addr(), 0x32, fpgaconfig_rq, NULL);

	fiid_obj_destroy(fpgaconfig_rq);

	/*
	 * Now Commit the configuration
	 */

	fiid_template_t tmpl_fpga_ctrlwrite_rq =
	{ 
		{ 8, "cmd", FIID_FIELD_REQUIRED | FIID_FIELD_LENGTH_FIXED},
		{ 8, "port", FIID_FIELD_REQUIRED | FIID_FIELD_LENGTH_FIXED},
		{ 1, "clear", FIID_FIELD_REQUIRED | FIID_FIELD_LENGTH_FIXED},
		{ 1, "set", FIID_FIELD_REQUIRED | FIID_FIELD_LENGTH_FIXED},
		{ 3, "reserved1", FIID_FIELD_REQUIRED | FIID_FIELD_LENGTH_FIXED},
		{ 1, "cfgrdy", FIID_FIELD_REQUIRED | FIID_FIELD_LENGTH_FIXED},
		{ 1, "hf2", FIID_FIELD_REQUIRED | FIID_FIELD_LENGTH_FIXED},
		{ 1, "hf1", FIID_FIELD_REQUIRED | FIID_FIELD_LENGTH_FIXED},
		{ 0, "", 0}
	};

	fiid_obj_t fpga_ctrlwrite_rq = fiid_obj_create(tmpl_fpga_ctrlwrite_rq);
	fiid_obj_set(fpga_ctrlwrite_rq, "cmd", 0x32);
	fiid_obj_set(fpga_ctrlwrite_rq, "port", 0);
	fiid_obj_set(fpga_ctrlwrite_rq, "clear", 0);
	fiid_obj_set(fpga_ctrlwrite_rq, "set", 1);
	fiid_obj_set(fpga_ctrlwrite_rq, "reserved1", 0);
	fiid_obj_set(fpga_ctrlwrite_rq, "cfgrdy", 1);
	fiid_obj_set(fpga_ctrlwrite_rq, "hf2", 0);
	fiid_obj_set(fpga_ctrlwrite_rq, "hf1", 0);

	// TODO:RV
	this->crate->send_bridged(0, this->get_bridge_addr(), this->get_channel(), this->get_addr(), 0x32, fpga_ctrlwrite_rq, NULL);

	fiid_obj_destroy(fpga_ctrlwrite_rq);

	mprintf("C%d: Sent configuration to %s card in %s with IP address 192.168.%d.%d\n", crate->get_number(), this->name.c_str(), this->get_slotstring().c_str(), crate->get_number(), 40+this->get_fru()-4);

	this->fpga_configure_in_progress = false;
}

std::string UWCard::read_ip_config()
{
	fiid_template_t tmpl_configread_rq =
	{ 
		{ 8, "cmd", FIID_FIELD_REQUIRED | FIID_FIELD_LENGTH_FIXED},
		{ 8, "port", FIID_FIELD_REQUIRED | FIID_FIELD_LENGTH_FIXED},
		{ 16, "address", FIID_FIELD_REQUIRED | FIID_FIELD_LENGTH_FIXED},
		{ 8, "length", FIID_FIELD_REQUIRED | FIID_FIELD_LENGTH_FIXED},
		{ 0, "", 0}
	};
	fiid_template_t tmpl_configread_rs =
	{ 
		{ 8, "slotid", FIID_FIELD_REQUIRED | FIID_FIELD_LENGTH_FIXED},
		{ 8, "netmask1", FIID_FIELD_REQUIRED | FIID_FIELD_LENGTH_FIXED},
		{ 8, "netmask2", FIID_FIELD_REQUIRED | FIID_FIELD_LENGTH_FIXED},
		{ 8, "netmask3", FIID_FIELD_REQUIRED | FIID_FIELD_LENGTH_FIXED},
		{ 8, "netmask4", FIID_FIELD_REQUIRED | FIID_FIELD_LENGTH_FIXED},
		{ 8, "ip1", FIID_FIELD_REQUIRED | FIID_FIELD_LENGTH_FIXED},
		{ 8, "ip2", FIID_FIELD_REQUIRED | FIID_FIELD_LENGTH_FIXED},
		{ 8, "ip3", FIID_FIELD_REQUIRED | FIID_FIELD_LENGTH_FIXED},
		{ 8, "ip4", FIID_FIELD_REQUIRED | FIID_FIELD_LENGTH_FIXED},
		{ 16, "bootvector", FIID_FIELD_REQUIRED | FIID_FIELD_LENGTH_FIXED},
		{ 0, "", 0}
	};

	fiid_obj_t configread_rq = fiid_obj_create(tmpl_configread_rq);
	fiid_obj_t configread_rs = fiid_obj_create(tmpl_configread_rs);
	fiid_obj_set(configread_rq, "cmd", 0x34);
	fiid_obj_set(configread_rq, "port", 0);
	fiid_obj_set(configread_rq, "address", 0);
	fiid_obj_set(configread_rq, "length", 11);

	this->crate->send_bridged(0, this->get_bridge_addr(), this->get_channel(), this->get_addr(), 0x32, configread_rq, configread_rs);

	uint64_t ip_parts[4];
	fiid_obj_get(configread_rs, "ip1", &ip_parts[0]);
	fiid_obj_get(configread_rs, "ip2", &ip_parts[1]);
	fiid_obj_get(configread_rs, "ip3", &ip_parts[2]);
	fiid_obj_get(configread_rs, "ip4", &ip_parts[3]);

	std::string output = stdsprintf("%u.%u.%u.%u", ip_parts[0], ip_parts[1], ip_parts[2], ip_parts[3]);

	fiid_obj_destroy(configread_rq);
	fiid_obj_destroy(configread_rs);

	return output;
}
