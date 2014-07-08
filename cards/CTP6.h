#ifndef _CTP6_H
#define _CTP6_H

#include "../Crate.h"
#include "UWCard.h"

class CTP6 : public UWCard {
	public:
		CTP6(Crate *crate, std::string name, void *sdrbuf, uint8_t sdrbuflen)
			: UWCard(crate, name, sdrbuf, sdrbuflen) { };
};

#endif
