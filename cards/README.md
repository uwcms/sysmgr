# 1. Card Specific Modules

Card specific modules to handle automatic configuration or monitoring from
within the system manager itself live here.

All .cpp files will be built as shared modules and included in the RPM.  These
shared modules can be loaded by specifying them in the sysmgr.conf file.

# 2. Required Components
Several components are required to produce a valid system manager card module.

## 2.1. Required functions and variables
Certain functions and variables must be exported with `extern "C"`:

```cpp
extern "C" {
	uint32_t APIVER = 2;     // I support this API version.
	uint32_t MIN_APIVER = 2; // I support no version before this.

	bool initialize_module(std::vector<std::string> config) {
		// Perform any required initialization, and read sysmgr.conf config options.

		return true; // Indicate that the module has been successfully initalized.
	}

	Card *instantiate_card(Crate *crate, std::string name, void *sdrbuf, uint8_t sdrbuflen) {
		// Decide whether or not this module will handle the newly discovered card.

		if (name == "MyCard")
			return new MyCard(crate, name, sdrbuf, sdrbuflen); // This is mine alone.

		return NULL; // I stake no claim to this.
	}
}
```

## 2.2. Control Comments
The build process will detect and make use of certain specially formatted
comments which begin with `/** configure: ` in column 0.

### 2.2.1. depends
```cpp
/** configure: depends: ParentCard OtherParentCard */
```
A module containing a 'depends' line will load after (in the example config)
and be considered for use before any cards it specified.  You can use this to
make subclasses or more specific versions of general modules.

### 2.2.2. sysmgr.conf.example
```cpp
/** configure: sysmgr.conf.example
cardmodule {
	module = "GenericUW.so"
	config = {
		"ivtable=ipconfig.xml",
		"support=WISC CTP-7",
		"support=WISC CTP-6",
		"support=WISC CIOX",
		"support=BU AMC13"
	}
}
*/
```
A module containing a sysmgr.conf.example section will have the provided code
copied (in commented out form) into the sysmgr.conf.example file in appropriate
dependency order.
