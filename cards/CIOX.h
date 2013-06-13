#ifndef _CIOX_H
#define _CIOX_H

#include "../Crate.h"

class CIOX : public UWCard {
	public:
		static bool check_card_type(Crate *crate, std::string name, void *sdrbuf, uint8_t sdrbuflen) { return (name == "WISC CIOX"); }
		CIOX(Crate *crate, std::string name, void *sdrbuf, uint8_t sdrbuflen)
			: UWCard(crate, name, sdrbuf, sdrbuflen) { };
};

#endif
