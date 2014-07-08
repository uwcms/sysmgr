#include "CTP6.h"

extern "C" {
	int APIVER = 1;
	int MIN_APIVER = 1;
	bool initialize_module(std::string config) { return true; };
	bool check_card_type(Crate *crate, std::string name, void *sdrbuf, uint8_t sdrbuflen) {
		return name == "WISC CTP-6";
	}
	Card *instantiate_card(Crate *crate, std::string name, void *sdrbuf, uint8_t sdrbuflen) {
		return new CTP6(crate, name, sdrbuf, sdrbuflen);
	}
}
