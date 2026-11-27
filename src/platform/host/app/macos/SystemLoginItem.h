#pragma once

#include <memory>

#include "platform/host/app/macos/AppPaths.h"
#include "platform/host/app/macos/LoginItem.h"

namespace awtrix::hostapp {

std::unique_ptr<LoginItem> systemLoginItem(const AppPaths& paths);

}
