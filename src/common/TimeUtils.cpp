#include "common/TimeUtils.h"

#include <QDateTime>

namespace pano {

QString nowIsoUtc()
{
    return QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
}

} // namespace pano
