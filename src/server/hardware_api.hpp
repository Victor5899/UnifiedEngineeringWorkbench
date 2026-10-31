#pragma once

#include <httplib.h>

namespace workbench {

void register_hardware_routes(httplib::Server& server);

}
