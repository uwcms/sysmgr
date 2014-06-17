Summary: University of Wisconsin IPMI MicroTCA System Manager
Name: sysmgr
Version: 1.0.0
Release: 1
Packager: Jesra Tikalsky
#BuildArch: %{_buildarch}
License: Reserved
Group: Applications/XDAQ
#Source: https://github.com/uwcms/sysmgr/archive/master.zip
URL: https://github.com/uwcms/sysmgr
BuildRoot: %{PWD}/rpm/buildroot
Requires: freeipmi >= 1.2.1, libconfuse >= 2.7
Prefix: /usr

%description
The University of Wisconsin IPMI MicroTCA System Manager grants access to
MicroTCA crates over IPMI, providing basic status information and allowing
multiple other applications to concurrently access the crates while buffering
them from vendor or protocol related idiosyncracies, as well as providing a
platform for automatic card initialization.

#
# Devel RPM specified attributes (extension to binary rpm with include files)
#
#%package -n sysmgr-devel
#Summary:  Development package for the sysmgr client API
#Group:    Applications/XDAQ
#
#%description -n %{_project}-%{_packagename}-devel
#The University of Wisconsin IPMI MicroTCA System Manager grants access to
#MicroTCA crates over IPMI, providing basic status information and allowing
#multiple other applications to concurrently access the crates while buffering
#them from vendor or protocol related idiosyncracies, as well as providing a
#platform for automatic card initialization.

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
mkdir -p %{buildroot}/usr/lib64
mkdir -p %{buildroot}/usr/share/doc/%{name}-%{version}/

install -m 755 $SYSMGR_ROOT/sysmgr %{buildroot}/usr/bin/
install -m 644 $SYSMGR_ROOT/sysmgr.conf.example %{buildroot}/usr/share/doc/%{name}-%{version}/
install -m 755 $SYSMGR_ROOT/clientapi/sysmgr.h %{buildroot}/usr/include/
install -m 755 $SYSMGR_ROOT/clientapi/libsysmgr.so %{buildroot}/usr/lib64/
#install -m 655 %{_packagedir}/MAINTAINER %{_packagedir}/rpm/RPMBUILD/BUILD/MAINTAINER

%clean
rm -rf %{buildroot}

#
# Files that go in the binary RPM
#
%files
%defattr(-,root,root,-)
%doc /usr/share/doc/%{name}-%{version}/sysmgr.conf.example
/usr/bin/
/usr/lib64/
/usr/include/

#
# Files that go in the devel RPM
#
#%files -n sysmgr-devel
#%defattr(-,root,root,-)

#%changelog
