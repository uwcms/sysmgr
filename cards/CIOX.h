#ifndef _CIOX_H
#define _CIOX_H

#include "../Crate.h"
#include "UWCard.h"

class CIOX : public UWCard {
	public:
		CIOX(Crate *crate, std::string name, void *sdrbuf, uint8_t sdrbuflen)
			: UWCard(crate, name, sdrbuf, sdrbuflen) { };
};

#endif
