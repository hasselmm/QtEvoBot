#include "utilities.h"

#include <QMetaEnum>

namespace EvoBot {

const char *key(const QMetaObject *metaObject, const char *enumName, int value)
{
    return metaObject->enumerator(metaObject->indexOfEnumerator(enumName)).valueToKey(value);
}

} // namespace EvoBot
