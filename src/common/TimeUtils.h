#pragma once

#include <QString>

namespace pano {

// UTC ISO-8601 timestamp with milliseconds; shared by annotations, notes and
// other persisted records. The on-disk format must stay stable.
QString nowIsoUtc();

} // namespace pano
