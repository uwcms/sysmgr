#include "CTP6.h"

extern "C" {
	uint32_t APIVER = 1;
	uint32_t MIN_APIVER = 1;
	bool initialize_module(std::string config) { return true; };
	Card *instantiate_card(Crate *crate, std::string name, void *sdrbuf, uint8_t sdrbuflen) {
		if (name == "WISC CTP-6")
			return new CTP6(crate, name, sdrbuf, sdrbuflen);
		return NULL;
	}
}
