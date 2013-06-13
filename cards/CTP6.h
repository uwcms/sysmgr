#ifndef _CTP6_H
#define _CTP6_H

#include "../Crate.h"

class CTP6 : public UWCard {
	public:
		static bool check_card_type(Crate *crate, std::string name, void *sdrbuf, uint8_t sdrbuflen) { return (name == "WISC CTP-6"); }
		CTP6(Crate *crate, std::string name, void *sdrbuf, uint8_t sdrbuflen)
			: UWCard(crate, name, sdrbuf, sdrbuflen) { };
};

#endif
