%global commit0 8245dd0bb9ec719932372e8349a610d74c8cb514
%global commit1 f740ec6f25dbd16e7739ded955b7cbd4eadd1f16
%global _lto_cflags %{nil}
%global _name amd-rocm-clr
%global rocm_path /opt/rocm
%global shortcommit0 %(c=%{commit0}; echo ${c:0:7})
%global shortcommit1 %(c=%{commit1}; echo ${c:0:7})
%global toolchain clang
%global up_name clr
%global up_name1 HIP

%define patch_level 5
%bcond_with debug
%bcond_with static

%if %{without debug}
  %if %{without static}
    %global suf %{nil}
  %else
    %global suf -static
  %endif
%else
  %if %{without static}
    %global suf -debug
  %else
    %global suf -static-debug
  %endif
%endif

Name: %{_name}%{suf}

Version:        5.6
Release:        %{patch_level}.git%{?shortcommit0}%{?dist}
Summary:        TBD
License:        TBD

URL:            https://github.com/trixirt/%{up_name}
Source0:        %{url}/archive/%{commit0}/%{up_name}-%{shortcommit0}.tar.gz
Source1:        https://github.com/trixirt/%{up_name1}/archive/%{commit1}/%{up_name1}-%{shortcommit1}.tar.gz

BuildRequires:  cmake
BuildRequires:  ocl-icd-devel

Requires:       ocl-icd%{?_isa}
Requires:       opencl-filesystem

%if %{without debug}
%global debug_package %{nil}
%endif

%description
TBD

%package devel
Summary:        TBD

%description devel
%{summary}

%prep
%autosetup -N -a 1 -n %{up_name}-%{commit0}

%build
%cmake \
       -DCLR_BUILD_HIP=ON \
       -DCLR_BUILD_OCL=ON \
%if %{without debug}
       -DCMAKE_BUILD_TYPE=RELEASE \
%else
       -DCMAKE_BUILD_TYPE=DEBUG \
%endif
       -DCMAKE_INSTALL_PREFIX=%{rocm_path} \
       -DHIP_COMMON_DIR=$(realpath HIP-%{commit1}) \
       -DHIP_PLATFORM=amd \
       -DHIPCC_BIN_DIR=%{rocm_path}/bin \
       -DUSE_PROF_API=OFF
      
%cmake_build

%install
%cmake_install

%files devel
%{rocm_path}

%changelog
* Mon Aug 07 2023 Tom Rix <trix@redhat.com>
- Stub something together
