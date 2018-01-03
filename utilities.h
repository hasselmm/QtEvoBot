#ifndef UTILITIES_H
#define UTILITIES_H

#include <QMetaObject>

namespace EvoBot {

const char *key(const QMetaObject *metaObject, const char *enumName, int value);

template<typename T>
const char *key(T value, typename std::enable_if<std::is_enum<T>::value>::type * = {})
{
    return key(qt_getEnumMetaObject(T{}), qt_getEnumName(T{}), value);
}

} // namespace EvoBot

#endif // UTILITIES_H
