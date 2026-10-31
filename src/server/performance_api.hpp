#pragma once

#include <httplib.h>

namespace workbench {

void register_performance_routes(httplib::Server& server);

}
