/****************************************************************************
**
** Copyright (C) 2016 The Qt Company Ltd.
** Contact: https://www.qt.io/licensing/
**
** This file is part of PySide2.
**
** $QT_BEGIN_LICENSE:LGPL$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see https://www.qt.io/terms-conditions. For further
** information use the contact form at https://www.qt.io/contact-us.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 3 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPL3 included in the
** packaging of this file. Please review the following information to
** ensure the GNU Lesser General Public License version 3 requirements
** will be met: https://www.gnu.org/licenses/lgpl-3.0.html.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 2.0 or (at your option) the GNU General
** Public license version 3 or any later version approved by the KDE Free
** Qt Foundation. The licenses are as published by the Free Software
** Foundation and appearing in the file LICENSE.GPL2 and LICENSE.GPL3
** included in the packaging of this file. Please review the following
** information to ensure the GNU General Public License requirements will
** be met: https://www.gnu.org/licenses/gpl-2.0.html and
** https://www.gnu.org/licenses/gpl-3.0.html.
**
** $QT_END_LICENSE$
**
****************************************************************************/

#include "pyside.h"
#include "signalmanager.h"
#include "pysideclassinfo_p.h"
#include "pysideproperty_p.h"
#include "pysideproperty.h"
#include "pysidesignal.h"
#include "pysidesignal_p.h"
#include "pysideslot_p.h"
#include "pysidemetafunction_p.h"
#include "pysidemetafunction.h"
#include "dynamicqmetaobject.h"
#include "destroylistener.h"

#include <basewrapper.h>
#include <conversions.h>
#include <sbkconverter.h>
#include <gilstate.h>
#include <typeresolver.h>
#include <bindingmanager.h>
#include <algorithm>
#include <cstring>
#include <cctype>
#include <QStack>
#include <QCoreApplication>
#include <QDebug>
#include <QSharedPointer>

static QStack<PySide::CleanupFunction> cleanupFunctionList;
static void* qobjectNextAddr;

namespace PySide
{

void init(PyObject *module)
{
    qobjectNextAddr = 0;
    ClassInfo::init(module);
    Signal::init(module);
    Slot::init(module);
    Property::init(module);
    MetaFunction::init(module);
    // Init signal manager, so it will register some meta types used by QVariant.
    SignalManager::instance();
}

bool fillQtProperties(PyObject* qObj, const QMetaObject* metaObj, PyObject* kwds, const char** blackList, unsigned int blackListSize)
{

    PyObject *key, *value;
    Py_ssize_t pos = 0;

    while (PyDict_Next(kwds, &pos, &key, &value)) {
        if (!blackListSize || !std::binary_search(blackList, blackList + blackListSize, std::string(Shiboken::String::toCString(key)))) {
            QByteArray propName(Shiboken::String::toCString(key));
            if (metaObj->indexOfProperty(propName) != -1) {
                propName[0] = std::toupper(propName[0]);
                propName.prepend("set");

                Shiboken::AutoDecRef propSetter(PyObject_GetAttrString(qObj, propName.constData()));
                if (!propSetter.isNull()) {
                    Shiboken::AutoDecRef args(PyTuple_Pack(1, value));
                    Shiboken::AutoDecRef retval(PyObject_CallObject(propSetter, args));
                } else {
                    PyObject* attr = PyObject_GenericGetAttr(qObj, key);
                    if (PySide::Property::checkType(attr))
                        PySide::Property::setValue(reinterpret_cast<PySideProperty*>(attr), qObj, value);
                }
            } else {
                propName.append("()");
                if (metaObj->indexOfSignal(propName) != -1) {
                    propName.prepend('2');
                    PySide::Signal::connect(qObj, propName, value);
                } else {
                    PyErr_Format(PyExc_AttributeError, "'%s' is not a Qt property or a signal", propName.constData());
                    return false;
                };
            }
        }
    }
    return true;
}

void registerCleanupFunction(CleanupFunction func)
{
    cleanupFunctionList.push(func);
}

void runCleanupFunctions()
{
    //PySide::DestroyListener::instance()->destroy();
    while (!cleanupFunctionList.isEmpty()) {
        CleanupFunction f = cleanupFunctionList.pop();
        f();
    }
    PySide::DestroyListener::destroy();
}

static void destructionVisitor(SbkObject* pyObj, void* data)
{
    void** realData = reinterpret_cast<void**>(data);
    SbkObject* pyQApp = reinterpret_cast<SbkObject*>(realData[0]);
    PyTypeObject* pyQObjectType = reinterpret_cast<PyTypeObject*>(realData[1]);

    if (pyObj != pyQApp && PyObject_TypeCheck(pyObj, pyQObjectType)) {
        if (Shiboken::Object::hasOwnership(pyObj) && Shiboken::Object::isValid(pyObj, false)) {
            Shiboken::Object::setValidCpp(pyObj, false);

            Py_BEGIN_ALLOW_THREADS
            Shiboken::callCppDestructor<QObject>(Shiboken::Object::cppPointer(pyObj, pyQObjectType));
            Py_END_ALLOW_THREADS
        }
    }

};

void destroyQCoreApplication()
{
    SignalManager::instance().clear();
    QCoreApplication* app = QCoreApplication::instance();
    if (!app)
        return;

    Shiboken::BindingManager& bm = Shiboken::BindingManager::instance();
    SbkObject* pyQApp = bm.retrieveWrapper(app);
    PyTypeObject* pyQObjectType = Shiboken::Conversions::getPythonTypeObject("QObject*");
    assert(pyQObjectType);

    void* data[2] = {pyQApp, pyQObjectType};
    bm.visitAllPyObjects(&destructionVisitor, &data);

    // in the end destroy app
    // Allow threads because the destructor calls
    // QThreadPool::globalInstance().waitForDone() which may deadlock on the GIL
    // if there is a worker working with python objects.
    Py_BEGIN_ALLOW_THREADS
    delete app;
    Py_END_ALLOW_THREADS
}

struct TypeUserData {
    TypeUserData(PyTypeObject* type, const QMetaObject* metaobject) : mo(type, metaobject) {}
    DynamicQMetaObject mo;
    std::size_t cppObjSize;
};

std::size_t getSizeOfQObject(SbkObjectType* type)
{
    using namespace Shiboken::ObjectType;
    TypeUserData* userData = reinterpret_cast<TypeUserData*>(getTypeUserData(reinterpret_cast<SbkObjectType*>(type)));
    return userData->cppObjSize;
}

void initDynamicMetaObject(SbkObjectType* type, const QMetaObject* base, const std::size_t& cppObjSize)
{
    //create DynamicMetaObject based on python type
    TypeUserData* userData = new TypeUserData(reinterpret_cast<PyTypeObject*>(type), base);
    userData->cppObjSize = cppObjSize;
    userData->mo.update();
    Shiboken::ObjectType::setTypeUserData(type, userData, Shiboken::callCppDestructor<TypeUserData>);

    //initialize staticQMetaObject property
    void* metaObjectPtr = &userData->mo;
    static SbkConverter* converter = Shiboken::Conversions::getConverter("QMetaObject");
    if (!converter)
        return;
    Shiboken::AutoDecRef pyMetaObject(Shiboken::Conversions::pointerToPython(converter, metaObjectPtr));
    PyObject_SetAttrString(reinterpret_cast<PyObject*>(type), "staticMetaObject", pyMetaObject);
}

void initDynamicMetaObject(SbkObjectType* type, const QMetaObject* base)
{
    initDynamicMetaObject(type, base, 0);
}

void initQObjectSubType(SbkObjectType *type, PyObject *args, PyObject * /* kwds */)
{
    PyTypeObject* qObjType = Shiboken::Conversions::getPythonTypeObject("QObject*");
    QByteArray className(Shiboken::String::toCString(PyTuple_GET_ITEM(args, 0)));

    PyObject* bases = PyTuple_GET_ITEM(args, 1);
    int numBases = PyTuple_GET_SIZE(bases);
    QMetaObject* baseMo = 0;
    SbkObjectType* qobjBase = 0;

    for (int i = 0; i < numBases; ++i) {
        PyTypeObject* base = reinterpret_cast<PyTypeObject*>(PyTuple_GET_ITEM(bases, i));
        if (PyType_IsSubtype(base, qObjType)) {
            baseMo = reinterpret_cast<QMetaObject*>(Shiboken::ObjectType::getTypeUserData(reinterpret_cast<SbkObjectType*>(base)));
            qobjBase = reinterpret_cast<SbkObjectType*>(base);
            reinterpret_cast<DynamicQMetaObject*>(baseMo)->update();
            break;
        }
    }
    if (!baseMo) {
        qWarning("Sub class of QObject not inheriting QObject!? Crash will happen when using %s.", className.constData());
        return;
    }

    TypeUserData* userData = reinterpret_cast<TypeUserData*>(Shiboken::ObjectType::getTypeUserData(qobjBase));
    initDynamicMetaObject(type, baseMo, userData->cppObjSize);
}

PyObject* getMetaDataFromQObject(QObject* cppSelf, PyObject* self, PyObject* name)
{
    PyObject* attr = PyObject_GenericGetAttr(self, name);
    if (!Shiboken::Object::isValid(reinterpret_cast<SbkObject*>(self), false))
        return attr;

    if (attr && Property::checkType(attr)) {
        PyObject *value = Property::getValue(reinterpret_cast<PySideProperty*>(attr), self);
        Py_DECREF(attr);
        if (!value)
            return 0;
        Py_INCREF(value);
        attr = value;
    }

    //mutate native signals to signal instance type
    if (attr && PyObject_TypeCheck(attr, &PySideSignalType)) {
        PyObject* signal = reinterpret_cast<PyObject*>(Signal::initialize(reinterpret_cast<PySideSignal*>(attr), name, self));
        PyObject_SetAttr(self, name, reinterpret_cast<PyObject*>(signal));
        return signal;
    }

    //search on metaobject (avoid internal attributes started with '__')
    if (!attr) {
        const char* cname = Shiboken::String::toCString(name);
        uint cnameLen = qstrlen(cname);
        if (std::strncmp("__", cname, 2)) {
            const QMetaObject* metaObject = cppSelf->metaObject();
            //signal
            QList<QMetaMethod> signalList;
            for(int i=0, i_max = metaObject->methodCount(); i < i_max; i++) {
                QMetaMethod method = metaObject->method(i);
                const char* methSig = method.methodSignature();
                bool methMacth = !std::strncmp(cname, methSig, cnameLen) && methSig[cnameLen] == '(';
                if (methMacth) {
                    if (method.methodType() == QMetaMethod::Signal) {
                        signalList.append(method);
                    } else {
                        PySideMetaFunction* func = MetaFunction::newObject(cppSelf, i);
                        if (func) {
                            PyObject_SetAttr(self, name, (PyObject*)func);
                            return (PyObject*)func;
                        }
                    }
                }
            }
            if (signalList.size() > 0) {
                PyObject* pySignal = reinterpret_cast<PyObject*>(Signal::newObjectFromMethod(self, signalList));
                PyObject_SetAttr(self, name, pySignal);
                return pySignal;
            }
        }
    }
    return attr;
}

bool inherits(PyTypeObject* objType, const char* class_name)
{
    if (strcmp(objType->tp_name, class_name) == 0)
        return true;

    PyTypeObject* base = (objType)->tp_base;
    if (base == 0)
        return false;

    return inherits(base, class_name);
}

void* nextQObjectMemoryAddr()
{
    return qobjectNextAddr;
}

void setNextQObjectMemoryAddr(void* addr)
{
    qobjectNextAddr = addr;
}

} // namespace PySide

// A QSharedPointer is used with a deletion function to invalidate a pointer
// when the property value is cleared.  This should be a QSharedPointer with
// a void* pointer, but that isn't allowed
typedef char any_t;
Q_DECLARE_METATYPE(QSharedPointer<any_t>);

namespace PySide
{

static void invalidatePtr(any_t* object)
{
    Shiboken::GilState state;

    SbkObject* wrapper = Shiboken::BindingManager::instance().retrieveWrapper(object);
    if (wrapper != NULL)
        Shiboken::BindingManager::instance().releaseWrapper(wrapper);
}

static const char invalidatePropertyName[] = "_PySideInvalidatePtr";

PyObject* getWrapperForQObject(QObject* cppSelf, SbkObjectType* sbk_type)
{
    PyObject* pyOut = (PyObject*)Shiboken::BindingManager::instance().retrieveWrapper(cppSelf);
    if (pyOut) {
        Py_INCREF(pyOut);
        return pyOut;
    }

    // Setting the property will trigger an QEvent notification, which may call into
    // code that creates the wrapper so only set the property if it isn't already
    // set and check if it's created after the set call
    QVariant existing = cppSelf->property(invalidatePropertyName);
    if (!existing.isValid()) {
        QSharedPointer<any_t> shared_with_del((any_t*)cppSelf, invalidatePtr);
        cppSelf->setProperty(invalidatePropertyName, QVariant::fromValue(shared_with_del));
        pyOut = (PyObject*)Shiboken::BindingManager::instance().retrieveWrapper(cppSelf);
        if (pyOut) {
            Py_INCREF(pyOut);
            return pyOut;
        }
    }

    const char* typeName = typeid(*cppSelf).name();
    pyOut = Shiboken::Object::newObject(sbk_type, cppSelf, false, false, typeName);

    return pyOut;
}

#ifdef PYSIDE_QML_SUPPORT
static QuickRegisterItemFunction quickRegisterItem;

QuickRegisterItemFunction getQuickRegisterItemFunction()
{
    return quickRegisterItem;
}

void setQuickRegisterItemFunction(QuickRegisterItemFunction function)
{
    quickRegisterItem = function;
}
#endif // PYSIDE_QML_SUPPORT

} //namespace PySide

