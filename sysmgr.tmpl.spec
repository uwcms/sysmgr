%define commit %(git rev-parse HEAD)
%define shortcommit %(git rev-parse --short=8 HEAD)
%define sysmgr_version %(git describe --dirty | sed -re 's/^v//;s/-dirty/-d/;s/-/./g')

%define sysmgr_module_api_version %(echo -e '#include "sysmgr.h"\\nCARD_MODULE_API_VERSION' | g++ -E -x c++ - | tail -n1)

{% set pyverinfo = sorted(map(lambda x: (re.sub('.*/python([0-9.]+)/.*','\\1',x), x), glob.glob('/usr/lib/python*/site-packages'))) %}

Summary: University of Wisconsin IPMI MicroTCA System Manager
Name: sysmgr
Version: %{sysmgr_version}
Release: 1%{?dist}
#Release: 1%{?dist}.%(git rev-parse --abbrev-ref HEAD | sed s/-/_/g)
#BuildArch: %{_buildarch}
License: Reserved
Group: Applications/XDAQ
#Source: http://github.com/uwcms/sysmgr/archive/%{commit}/sysmgr-%{commit}.tar.gz
URL: https://github.com/uwcms/sysmgr
BuildRoot: %{PWD}/rpm/buildroot
Requires: freeipmi >= 1.2.1, libconfuse >= 2.7, libxml++ >= 2.30.0
Provides: sysmgr_module_api(%{sysmgr_module_api_version})
Conflicts: sysmgr < 1.1.5
#Prefix: /usr

%undefine __python_requires

%description
The University of Wisconsin IPMI MicroTCA System Manager grants access to
MicroTCA crates over IPMI, providing basic status information and allowing
multiple other applications to concurrently access the crates while buffering
them from vendor or protocol related idiosyncracies, as well as providing a
platform for automatic card initialization.

{% for card in cards.values() %}
%package module-{{ card['name'] }}
Summary:  University of Wisconsin IPMI MicroTCA System Manager {{ card['name'] }} Card Module
Group:    Applications/XDAQ
Requires: sysmgr_module_api(%{sysmgr_module_api_version})
Conflicts: sysmgr < 1.1.5
{% for depend in card['depends'] %}
Requires: sysmgr-module-{{ depend }} = %{sysmgr_version}
{% end %}

%description module-{{ card['name'] }}
The {{ card['name'] }} card support module for the University of Wisconsin IPMI MicroTCA System Manager.
{% end %}

%package client
Summary:  University of Wisconsin IPMI MicroTCA System Manager C++ Client API
Group:    Applications/XDAQ
Conflicts: sysmgr < 1.1.5

%description client
The C++ client API library for communicating with the University of Wisconsin IPMI MicroTCA System Manager.

%package client-devel
Summary:  University of Wisconsin IPMI MicroTCA System Manager C++ Client API
Group:    Applications/XDAQ
Requires: sysmgr-client = %{sysmgr_version}
Conflicts: sysmgr < 1.1.5

%description client-devel
The C++ client API library for communicating with the University of Wisconsin IPMI MicroTCA System Manager.

{% for pyver, pydir in pyverinfo %}
%package -n python{{ pyver.replace('.','') }}-sysmgr
Summary:  University of Wisconsin IPMI MicroTCA System Manager Python {{ pyver }} Client API
Group:    Applications/XDAQ
Conflicts: sysmgr < 1.1.5

%description -n python{{ pyver.replace('.','') }}-sysmgr
The python {{ pyver }} client API library for communicating with the University of Wisconsin IPMI MicroTCA System Manager.
{% end %}

%package complete
Summary:  University of Wisconsin IPMI MicroTCA System Manager - All Components
Group:    Applications/XDAQ
Requires: sysmgr = %{sysmgr_version}
Obsoletes: sysmgr < 1.1.5
{% for card in cards.values() %}
Requires: sysmgr-module-{{ card['name'] }} = %{sysmgr_version}
{% end %}
Requires: sysmgr-client = %{sysmgr_version}
Requires: sysmgr-client-devel = %{sysmgr_version}
{% for pyver, pydir in pyverinfo %}
Requires: python{{ pyver.replace('.','') }}-sysmgr = %{sysmgr_version}
{% end %}

%description complete
All component packages of the University of Wisconsin IPMI MicroTCA System Manager.

#%prep

#%setup 

#%build

#
# Prepare the list of files that are the input to the binary and devel RPMs
#
%install
rm -rf %{buildroot}
mkdir -p %{buildroot}/usr/include
mkdir -p %{buildroot}/usr/bin
mkdir -p %{buildroot}/etc/init.d
mkdir -p %{buildroot}/etc/sysmgr
mkdir -p %{buildroot}/usr/lib64
mkdir -p %{buildroot}/usr/lib64/sysmgr/modules
mkdir -p %{buildroot}/usr/share/doc/%{name}-%{version}/

install -m 755 $SYSMGR_ROOT/sysmgr %{buildroot}/usr/bin/
install -m 644 $SYSMGR_ROOT/README %{buildroot}/usr/share/doc/%{name}-%{version}/
install -m 644 $SYSMGR_ROOT/sysmgr.example.conf %{buildroot}/etc/sysmgr/
install -m 644 $SYSMGR_ROOT/sysmgr.example.conf %{buildroot}/usr/share/doc/%{name}-%{version}/
install -m 755 $SYSMGR_ROOT/init.d_sysmgr.sh %{buildroot}/etc/init.d/sysmgr
install -m 755 $SYSMGR_ROOT/clientapi/sysmgr.h %{buildroot}/usr/include/
install -m 755 $SYSMGR_ROOT/clientapi/libsysmgr.so %{buildroot}/usr/lib64/
install -m 755 $SYSMGR_ROOT/cards/*.so %{buildroot}/usr/lib64/sysmgr/modules/
cp -r $SYSMGR_ROOT/cards/doc/ %{buildroot}/usr/share/doc/%{name}-%{version}/modules/
{% for card in cards.keys() %}
mkdir -p %{buildroot}/usr/share/doc/%{name}-%{version}/modules/{{ card }}
{% end %}
chmod -R u=rwX,go=rX %{buildroot}/usr/share/doc/%{name}-%{version}/modules/
{% for pyver, pylibdir in pyverinfo %}
mkdir -p %{buildroot}{{ pylibdir }}/sysmgr
install -m 644 $SYSMGR_ROOT/clientapi/sysmgr.py %{buildroot}{{ pylibdir }}/sysmgr/__init__.py
{% end %}

%clean
rm -rf %{buildroot}

#
# Files that go in the binary RPM
#
%files
%defattr(-,root,root,-)
%doc %dir /usr/share/doc/%{name}-%{version}/
%doc /usr/share/doc/%{name}-%{version}/README
%doc /usr/share/doc/%{name}-%{version}/sysmgr.example.conf
%doc %dir /usr/share/doc/%{name}-%{version}/modules
/etc/sysmgr/
/etc/init.d/sysmgr
/usr/bin/sysmgr
%dir /usr/lib64/sysmgr/modules/

%files client
/usr/lib64/libsysmgr.so

%files client-devel
/usr/include/sysmgr.h

{% for card in cards.values() %}
%files module-{{ card['name'] }}
/usr/lib64/sysmgr/modules/{{ card['name']}}.so
%doc /usr/share/doc/%{name}-%{version}/modules/{{ card['name'] }}
{% end %}

{% for pyver, pylibdir in pyverinfo %}
%files -n python{{ pyver.replace('.','') }}-sysmgr
{{ pylibdir }}/sysmgr
{% end %}

%files complete
# Pseudo-package, no files

%debug_package
