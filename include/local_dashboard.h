#pragma once

#include "dashboard_client.h"

namespace dashboard {
ClientResult local_dashboard_fetch(Snapshot& snapshot, String& validatedJson);
}
