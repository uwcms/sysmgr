#include "CIOX.h"

extern "C" {
	int APIVER = 1;
	int MIN_APIVER = 1;
	bool initialize_module(std::string config) { return true; };
	Card *instantiate_card(Crate *crate, std::string name, void *sdrbuf, uint8_t sdrbuflen) {
		if (name == "WISC CIOX")
			return new CIOX(crate, name, sdrbuf, sdrbuflen);
		return NULL;
	}
}
