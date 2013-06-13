#ifndef _EXCEPTIONS_H
#define _EXCEPTIONS_H
#include "sysmgr.h"

#define THROW(e) throw e(__FILE__, __LINE__, __func__)
#define THROWMSG(e, f, ...) throw e(__FILE__, __LINE__, __func__, stdsprintf(f, ##__VA_ARGS__))
#define RETMSG(e, f, ...) do { mprintf("C%d: Returned " #e " at %s:%u: %s: " f "\n", CRATE_NO, __FILE__, __LINE__, __func__, ##__VA_ARGS__); return e; } while (0)

class Sysmgr_Exception {
	protected:
		std::string file;
		int line;
		std::string func;
		std::string message;
		std::string etype;
		std::string etypechain;
	public:
		Sysmgr_Exception(std::string file, int line, std::string func) : file(file), line(line), func(func), message(""), etype("Sysmgr_Exception"), etypechain("Sysmgr_Exception") { };
		Sysmgr_Exception(std::string file, int line, std::string func, std::string message) : file(file), line(line), func(func), message(message), etype("Sysmgr_Exception"), etypechain("Sysmgr_Exception") { };
		const std::string get_file() { return file; };
		const int get_line() { return line; };
		const std::string get_func() { return func; };
		const std::string get_type() { return etype; };
		const std::string get_typechain() { return etypechain; };
		const std::string get_message() { return message; };
};
#define DEFINE_EXCEPTION(name, base)                                                            \
	class name : public base {                                                                  \
		public:                                                                                 \
			name(std::string file, int line, std::string func)                                  \
				: base(file, line, func) { etype = #name; etypechain += "/" #name; };           \
			name(std::string file, int line, std::string func, std::string message)             \
				: base(file, line, func, message) { etype = #name; etypechain += "/" #name; };  \
	}
DEFINE_EXCEPTION(Crate_Exception, Sysmgr_Exception);
DEFINE_EXCEPTION(IPMI_Exception, Crate_Exception);
DEFINE_EXCEPTION(IPMI_ConnectionFailedException, IPMI_Exception);
DEFINE_EXCEPTION(IPMI_CommandError, IPMI_Exception);
DEFINE_EXCEPTION(IPMI_LibraryError, IPMI_Exception);

#endif
