#ifndef _CTP7_H
#define _CTP7_H

#include "../Crate.h"
#include "UWCard.h"

class CTP7 : public UWCard {
	public:
		CTP7(Crate *crate, std::string name, void *sdrbuf, uint8_t sdrbuflen)
			: UWCard(crate, name, sdrbuf, sdrbuflen) { };
		virtual std::string read_ip_config();
	protected:
		virtual void configure_fpga();
};

#endif
