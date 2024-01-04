#pragma once
#include "../Common/WinReg.hpp"
#include "../Common/GlobalConstValues.hpp"
#include "../Common/Singleton.hpp"
#include "../Common/WindowsUtil.h"
#include "../Common/Common.h"

#include <accctrl.h>
#include <aclapi.h>

#include <wil/resource.h>

#include <format>
#include <array>
//#include <any>
#include <algorithm>
#include <cwctype>

#include <iostream>


bool RegisterAPO(bool IsInstall = true);