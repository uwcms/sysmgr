#include <time.h>
#include <unistd.h>
#include <typeinfo>

#include "GenericUW.h"

/** configure: sysmgr.conf.example
cardmodule {
	module = "GenericUW.so"
	config = {
		"ivtable=GenericUW_IVTable.xml",
		"support=WISC CTP-7",
		"support=WISC CTP-6",
		"support=WISC CIOX",
		"support=BU AMC13"
	}
}
*/

static std::string ivtable_path;
static std::vector<std::string> supported_cards;

extern "C" {
	uint32_t APIVER = 2;
	uint32_t MIN_APIVER = 2;
	bool initialize_module(std::vector<std::string> config) {
		for (std::vector<std::string>::iterator it = config.begin(); it != config.end(); it++) {
			size_t delim = it->find("=");
			if (delim == std::string::npos) {
				mprintf("Failure parsing module options: Invalid option: %s\n", it->c_str());
				return false;
			}
			std::string param = it->substr(0, delim);
			std::string value = it->substr(delim+1);

			if (param == "ivtable") {
				if (access(value.c_str(), R_OK) == 0) {
					ivtable_path = value;
				}
				else if (access((std::string(CONFIG_PATH "/") + value).c_str(), R_OK) == 0) {
					ivtable_path = (std::string(CONFIG_PATH "/") + value);
				}
				else {
					mprintf("Unable to access module configuration file: %s\n", value.c_str());
					return false;
				}
				dmprintf("GenericUW using ivtable %s\n", ivtable_path.c_str());
			}
			else if (param == "support") {
				supported_cards.push_back(value);
				dmprintf("GenericUW supporting %s cards\n", value.c_str());
			}
			else {
				mprintf("Unknown configuration option \"%s\"\n", param.c_str());
				return false;
			}
		}
		if (ivtable_path == "") {
			mprintf("No ivtable xml specified.  Unable to service cards.\n");
			return false;
		}
		if (supported_cards.size() == 0) {
			mprintf("No supported cards specified.  Unable to service cards.\n");
			return false;
		}
		return true;
	}
	Card *instantiate_card(Crate *crate, std::string name, void *sdrbuf, uint8_t sdrbuflen) {
		for (std::vector<std::string>::iterator it = supported_cards.begin(); it != supported_cards.end(); it++)
			if (*it == name)
				return new GenericUW(crate, name, sdrbuf, sdrbuflen, ivtable_path);
		return NULL;
	}
}

Sensor *GenericUW::instantiate_sensor(uint8_t sensor_number, const void *sdr, uint8_t sdrlen)
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

void GenericUW::hotswap_event(uint8_t oldstate, uint8_t newstate)
{
	if (newstate == 4) {
		Sensor *fpgaconfig = this->get_sensor("FPGA Config");
		if (fpgaconfig)
			static_cast<UW_FPGAConfig_Sensor*>(fpgaconfig)->scan_sensor(GENERICUW_CONFIG_RETRIES);
	}
}

void GenericUW::set_clock()
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

void GenericUW::configure_fpga(uint8_t fpgaid)
{
	if (fpgaid > 3)
		THROWMSG(Sysmgr_Exception, "Invalid FPGA ID sent to GenericUW::configure_fpga(%hhu) for %s in %s", fpgaid, this->name.c_str(), this->get_slotstring().c_str());

	IVTableParser::cfg_ivtable ivtable;
	IVTableParser::cfg_fpga *fpgacfg = NULL;

	uint8_t myslot = 0;
	if (this->fru >= 5 && this->fru <= 16)
		myslot = this->fru - 4;
	else if (this->fru == 30)
		myslot = 13;
	else if (this->fru == 29)
		myslot = 14;

	try {
		IVTableParser ivparser;
		ivtable = ivparser.ParseIVTable(this->ivtable_path);

		this->configure_fpga_pre(this->crate->get_number(), myslot, this->name, fpgaid, ivtable);

		fpgacfg = ivtable.find(this->crate->get_number(), myslot, this->name, fpgaid);
		if (!fpgacfg || fpgacfg->iv.size() == 0) {
			mprintf("C%d: Unable to service %s in %s (FPGA %hhu): No matching configuration entry\n", this->crate->get_number(), this->name.c_str(), this->get_slotstring().c_str(), fpgaid);
			return;
		}
		if (fpgacfg->iv.size() > 20) {
			mprintf("C%d: Unable to service %s in %s (FPGA %hhu): IV too long.  IVs greater than 20 bytes long are not presently supported.\n", this->crate->get_number(), this->name.c_str(), this->get_slotstring().c_str(), fpgaid);
			return;
		}
	}
	catch (IVTableParser::ConfigParseException &e) {
		mprintf("C%d: Unable to service %s in %s (FPGA %hhu): %s at %s:%d: %s\n", this->crate->get_number(), this->name.c_str(), this->get_slotstring().c_str(), fpgaid, e.get_type().c_str(), e.get_file().c_str(), e.get_line(), e.get_message().c_str());
		return;
	}

	std::string ivstr;
	for (std::vector<uint8_t>::iterator it = fpgacfg->iv.begin(); it != fpgacfg->iv.end(); it++)
		ivstr += static_cast<char>(*it);

	fiid_template_t tmpl_fpgaconfig_rq =
	{ 
		{ 8, "cmd", FIID_FIELD_REQUIRED | FIID_FIELD_LENGTH_FIXED},
		{ 8, "port", FIID_FIELD_REQUIRED | FIID_FIELD_LENGTH_FIXED},
		{ 16, "address", FIID_FIELD_REQUIRED | FIID_FIELD_LENGTH_FIXED},
		{ 8, "length", FIID_FIELD_REQUIRED | FIID_FIELD_LENGTH_FIXED},
		{ 160, "data", FIID_FIELD_REQUIRED | FIID_FIELD_LENGTH_VARIABLE},
		{ 0, "", 0}
	};

	fiid_obj_t fpgaconfig_rq = fiid_obj_create(tmpl_fpgaconfig_rq);
	fiid_obj_set(fpgaconfig_rq, "cmd", 0x33);
	fiid_obj_set(fpgaconfig_rq, "port", fpgaid);
	fiid_obj_set(fpgaconfig_rq, "address", 0);
	fiid_obj_set(fpgaconfig_rq, "length", ivstr.size());
	fiid_obj_set_data(fpgaconfig_rq, "data", ivstr.data(), ivstr.size());

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
	fiid_obj_set(fpga_ctrlwrite_rq, "port", fpgaid);
	fiid_obj_set(fpga_ctrlwrite_rq, "clear", 0);
	fiid_obj_set(fpga_ctrlwrite_rq, "set", 1);
	fiid_obj_set(fpga_ctrlwrite_rq, "reserved1", 0);
	fiid_obj_set(fpga_ctrlwrite_rq, "cfgrdy", 1);
	fiid_obj_set(fpga_ctrlwrite_rq, "hf2", 0);
	fiid_obj_set(fpga_ctrlwrite_rq, "hf1", 0);

	int rv = this->crate->send_bridged(0, this->get_bridge_addr(), this->get_channel(), this->get_addr(), 0x32, fpga_ctrlwrite_rq, NULL);

	fiid_obj_destroy(fpga_ctrlwrite_rq);

	if (rv == 0) {
		this->configure_fpga_post(this->crate->get_number(), myslot, this->name, fpgaid, ivtable);

		scope_lock l(&stdout_mutex);
		mprintf("C%d: Sent configuration to %s card in %s (FPGA %hhu):", crate->get_number(), this->name.c_str(), this->get_slotstring().c_str(), fpgaid);
		for (std::string::iterator it = ivstr.begin(); it != ivstr.end(); it++)
			mprintf(" %hhu", *it);
		mprintf("\n");
	}
	else
		mprintf("C%d: Unable to deliver configuration vector to %s in %s (FPGA %hhu)\n", crate->get_number(), this->name.c_str(), this->get_slotstring().c_str(), fpgaid);
}

IVTableParser::cfg_ivtable IVTableParser::ParseIVTable(std::string ivtable_file)
{
	ivtable.clear();
	try {
		this->set_substitute_entities(true);
		this->parse_file(ivtable_file);
		return ivtable;
	}
	catch(xmlpp::exception& ex)
	{
		if (this->last_exception) {
			ConfigParseException e = *(this->last_exception);
			delete this->last_exception;
			this->last_exception = NULL;
			throw e;
		}
		else
			THROWMSG(ConfigParseException, "Error parsing ivtable file \"%s\": %s", ivtable_file.c_str(), ex.what());
	}
}

void IVTableParser::on_start_document()
{
	this->ivtable.clear();
	this->parse_state = PST_TOP;
	this->cur_crate = 0;
	this->cur_slot = 0;
	this->cur_card = "";
	this->cur_fpga = 0;
}

void IVTableParser::on_start_element(const Glib::ustring& tagname,
		const AttributeList& attributes)
{
	try {
		std::map<std::string, std::string> attrs;
		for(xmlpp::SaxParser::AttributeList::const_iterator iter = attributes.begin(); iter != attributes.end(); ++iter)
			attrs.insert(std::pair<const Glib::ustring&, const Glib::ustring&>(iter->name.raw(), iter->value.raw()));

#define LEVEL_CHECK(curtag, tag) \
			if (tagname != tag) \
				THROWMSG(ConfigParseException, "Error parsing config file (in Crate %hhu, Slot %hhu, Card \"%s\", FPGA %hhu): Found a %s tag at the " curtag " level", this->cur_crate, this->cur_slot, this->cur_card.c_str(), this->cur_fpga, tagname.c_str())

#define ATTR_CHECK(tag, attr) \
			if (attrs.find(attr) == attrs.end()) \
				THROWMSG(ConfigParseException, "Error parsing config file (in Crate %hhu, Slot %hhu, Card \"%s\", FPGA %hhu): " tag " requires " attr "=\"\"", this->cur_crate, this->cur_slot, this->cur_card.c_str(), this->cur_fpga)


		if (this->parse_state == PST_TOP) {
			LEVEL_CHECK("top", "IVTable");
			this->parse_state = PST_IVTABLE;
		}
		else if (this->parse_state == PST_IVTABLE) {
			LEVEL_CHECK("<IVTable>", "Crate");
			ATTR_CHECK("<Crate>", "number");

			this->cur_crate = parse_uint8(attrs["number"]);
			this->ivtable.crates.insert(std::pair<uint8_t,cfg_crate>(this->cur_crate, cfg_crate(this->cur_crate, attrs)));
			this->parse_state = PST_CRATE;
		}
		else if (this->parse_state == PST_CRATE) {
			LEVEL_CHECK("<Crate>", "Slot");
			ATTR_CHECK("<Slot>", "number");

			this->cur_slot = parse_uint8(attrs["number"]);
			cfg_crate *crate = this->ivtable.find(this->cur_crate);
			crate->slots.insert(std::pair<uint8_t,cfg_slot>(this->cur_slot, cfg_slot(this->cur_slot, attrs)));
			this->parse_state = PST_SLOT;
		}
		else if (this->parse_state == PST_SLOT) {
			LEVEL_CHECK("<Slot>", "Card");
			ATTR_CHECK("<Card>", "type");

			this->cur_card = attrs["type"];
			cfg_slot *slot = this->ivtable.find(this->cur_crate, this->cur_slot);
			slot->cards.insert(std::pair<std::string,cfg_card>(this->cur_card, cfg_card(this->cur_card, attrs)));
			this->parse_state = PST_CARD;
		}
		else if (this->parse_state == PST_CARD) {
			LEVEL_CHECK("<Card>", "FPGA");
			ATTR_CHECK("<FPGA>", "id");

			this->cur_fpga = parse_uint8(attrs["id"]);
			cfg_card *card = this->ivtable.find(this->cur_crate, this->cur_slot, this->cur_card);
			card->fpgas.insert(std::pair<uint8_t,cfg_fpga>(this->cur_fpga, cfg_fpga(this->cur_fpga, attrs)));
			this->parse_state = PST_FPGA;
		}
		else if (this->parse_state == PST_FPGA) {
			LEVEL_CHECK("<FPGA>", NULL);
		}
		else {
			THROWMSG(ConfigParseException, "Error parsing config file (in Crate %hhu, Slot %hhu, Card \"%s\", FPGA %hhu): Parser is in an unknown state encountering %s tag", this->cur_crate, this->cur_slot, this->cur_card.c_str(), this->cur_fpga, tagname.c_str());
		}
#undef LEVEL_CHECK
#undef ATTR_CHECK
	}
	catch (ConfigParseException &e) {
		this->last_exception = new ConfigParseException(e);
		throw xmlpp::exception("USE LAST_EXCEPTION");
	}
}

void IVTableParser::on_end_element(const Glib::ustring& tagname)
{
	switch (this->parse_state) {
		case PST_TOP: break;
		case PST_IVTABLE:
					  this->parse_state = PST_TOP;
					  break;
		case PST_CRATE:
					  this->cur_crate = 0;
					  this->parse_state = PST_IVTABLE;
					  break;
		case PST_SLOT:
					  this->cur_slot = 0;
					  this->parse_state = PST_CRATE;
					  break;
		case PST_CARD:
					  this->cur_card = "";
					  this->parse_state = PST_SLOT;
					  break;
		case PST_FPGA:
					  this->cur_fpga = 0;
					  this->parse_state = PST_CARD;
					  break;
	}
}

void IVTableParser::on_characters(const Glib::ustring& text)
{
	if (this->parse_state != PST_FPGA)
		return;

	std::vector<uint8_t> bytes;
	std::string str = text.raw();
	size_t pos = 0;
	size_t delimpos = 0;
	do {
		delimpos = str.find_first_of(" \t\r\n", pos);
		if (delimpos != pos) {
			bytes.push_back(parse_uint8(str.substr(pos, delimpos-pos)));
		}
		pos = delimpos+1;
	} while (delimpos != std::string::npos);

	scope_lock l(&stdout_mutex);
	this->ivtable.find(this->cur_crate, this->cur_slot, this->cur_card, this->cur_fpga)->iv = bytes;
}

void IVTableParser::on_warning(const Glib::ustring& text)
{
	try {
		THROWMSG(ConfigParseException, "Error parsing config file (in Crate %hhu, Slot %hhu, Card \"%s\", FPGA %hhu): Warning detected: %s", this->cur_crate, this->cur_slot, this->cur_card.c_str(), this->cur_fpga, text.c_str());
	}
	catch (ConfigParseException &e) {
		this->last_exception = new ConfigParseException(e);
		throw xmlpp::exception("USE LAST_EXCEPTION");
	}
}

void IVTableParser::on_error(const Glib::ustring& text)
{
	try {
		THROWMSG(ConfigParseException, "Error parsing config file (in Crate %hhu, Slot %hhu, Card \"%s\", FPGA %hhu): Warning detected: %s", this->cur_crate, this->cur_slot, this->cur_card.c_str(), this->cur_fpga, text.c_str());
	}
	catch (ConfigParseException &e) {
		this->last_exception = new ConfigParseException(e);
		throw xmlpp::exception("USE LAST_EXCEPTION");
	}
}

void IVTableParser::on_fatal_error(const Glib::ustring& text)
{
	try {
		THROWMSG(ConfigParseException, "Error parsing config file (in Crate %hhu, Slot %hhu, Card \"%s\", FPGA %hhu): Warning detected: %s", this->cur_crate, this->cur_slot, this->cur_card.c_str(), this->cur_fpga, text.c_str());
	}
	catch (ConfigParseException &e) {
		this->last_exception = new ConfigParseException(e);
		throw xmlpp::exception("USE LAST_EXCEPTION");
	}
}
