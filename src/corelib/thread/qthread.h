/****************************************************************************
**
** Copyright (C) 2016 The Qt Company Ltd.
** Copyright (C) 2017 Klarälvdalens Datakonsult AB, a KDAB Group company, info@kdab.com, author Giuseppe D'Angelo <giuseppe.dangelo@kdab.com>
** Contact: https://www.qt.io/licensing/
**
** This file is part of the QtCore module of the Qt Toolkit.
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

#ifndef QTHREAD_H
#define QTHREAD_H

#include <QtCore/qobject.h>

// For QThread::create. The configure-time test just checks for the availability
// of std::future and std::async; for the C++17 codepath we perform some extra
// checks here (for std::invoke and C++14 lambdas).
#if QT_CONFIG(cxx11_future)
#  include <future> // for std::async
#  include <functional> // for std::invoke; no guard needed as it's a C++98 header

#  if defined(__cpp_lib_invoke) && __cpp_lib_invoke >= 201411 \
      && defined(__cpp_init_captures) && __cpp_init_captures >= 201304 \
      && defined(__cpp_generic_lambdas) &&  __cpp_generic_lambdas >= 201304
#    define QTHREAD_HAS_VARIADIC_CREATE
#  endif
#endif

#include <limits.h>

QT_BEGIN_NAMESPACE


class QThreadData;
class QThreadPrivate;
class QAbstractEventDispatcher;

class Q_CORE_EXPORT QThread : public QObject
{
    Q_OBJECT
public:
    static Qt::HANDLE currentThreadId() noexcept Q_DECL_PURE_FUNCTION;
    static QThread *currentThread();
    static int idealThreadCount() noexcept;
    static void yieldCurrentThread();

    explicit QThread(QObject *parent = nullptr);
    ~QThread();

    enum Priority {
        IdlePriority,

        LowestPriority,
        LowPriority,
        NormalPriority,
        HighPriority,
        HighestPriority,

        TimeCriticalPriority,

        InheritPriority
    };

    void setPriority(Priority priority);
    Priority priority() const;

    bool isFinished() const;
    bool isRunning() const;

    void requestInterruption();
    bool isInterruptionRequested() const;

    void setStackSize(uint stackSize);
    uint stackSize() const;

    void exit(int retcode = 0);

    QAbstractEventDispatcher *eventDispatcher() const;
    void setEventDispatcher(QAbstractEventDispatcher *eventDispatcher);

    bool event(QEvent *event) override;
    int loopLevel() const;

#ifdef Q_CLANG_QDOC
    template <typename Function, typename... Args>
    static QThread *create(Function &&f, Args &&... args);
    template <typename Function>
    static QThread *create(Function &&f);
#else
#  if QT_CONFIG(cxx11_future)
#    ifdef QTHREAD_HAS_VARIADIC_CREATE
    template <typename Function, typename... Args>
    static QThread *create(Function &&f, Args &&... args);
#    else
    template <typename Function>
    static QThread *create(Function &&f);
#    endif // QTHREAD_HAS_VARIADIC_CREATE
#  endif // QT_CONFIG(cxx11_future)
#endif // Q_CLANG_QDOC

public Q_SLOTS:
    void start(Priority = InheritPriority);
    void terminate();
    void quit();

public:
    // default argument causes thread to block indefinetely
    bool wait(unsigned long time = ULONG_MAX);

    static void sleep(unsigned long);
    static void msleep(unsigned long);
    static void usleep(unsigned long);

Q_SIGNALS:
    void started(QPrivateSignal);
    void finished(QPrivateSignal);

protected:
    virtual void run();
    int exec();

    static void setTerminationEnabled(bool enabled = true);

protected:
    QThread(QThreadPrivate &dd, QObject *parent = nullptr);

private:
    Q_DECLARE_PRIVATE(QThread)

#if QT_CONFIG(cxx11_future)
    static QThread *createThreadImpl(std::future<void> &&future);
#endif
    static Qt::HANDLE currentThreadIdImpl() noexcept Q_DECL_PURE_FUNCTION;

    friend class QCoreApplication;
    friend class QThreadData;
};

#if QT_CONFIG(cxx11_future)

#if defined(QTHREAD_HAS_VARIADIC_CREATE) || defined(Q_CLANG_QDOC)
// C++17: std::thread's constructor complying call
template <typename Function, typename... Args>
QThread *QThread::create(Function &&f, Args &&... args)
{
    using DecayedFunction = typename std::decay<Function>::type;
    auto threadFunction =
        [f = static_cast<DecayedFunction>(std::forward<Function>(f))](auto &&... largs) mutable -> void
        {
            (void)std::invoke(std::move(f), std::forward<decltype(largs)>(largs)...);
        };

    return createThreadImpl(std::async(std::launch::deferred,
                                       std::move(threadFunction),
                                       std::forward<Args>(args)...));
}
#elif defined(__cpp_init_captures) && __cpp_init_captures >= 201304
// C++14: implementation for just one callable
template <typename Function>
QThread *QThread::create(Function &&f)
{
    using DecayedFunction = typename std::decay<Function>::type;
    auto threadFunction =
        [f = static_cast<DecayedFunction>(std::forward<Function>(f))]() mutable -> void
        {
            (void)f();
        };

    return createThreadImpl(std::async(std::launch::deferred, std::move(threadFunction)));
}
#else
// C++11: same as C++14, but with a workaround for not having generalized lambda captures
namespace QtPrivate {
template <typename Function>
struct Callable
{
    explicit Callable(Function &&f)
        : m_function(std::forward<Function>(f))
    {
    }

    // Apply the same semantics of a lambda closure type w.r.t. the special
    // member functions, if possible: delete the copy assignment operator,
    // bring back all the others as per the RO5 (cf. §8.1.5.1/11 [expr.prim.lambda.closure])
    ~Callable() = default;
    Callable(const Callable &) = default;
    Callable(Callable &&) = default;
    Callable &operator=(const Callable &) = delete;
    Callable &operator=(Callable &&) = default;

    void operator()()
    {
        (void)m_function();
    }

    typename std::decay<Function>::type m_function;
};
} // namespace QtPrivate

template <typename Function>
QThread *QThread::create(Function &&f)
{
    return createThreadImpl(std::async(std::launch::deferred, QtPrivate::Callable<Function>(std::forward<Function>(f))));
}
#endif // QTHREAD_HAS_VARIADIC_CREATE

#endif // QT_CONFIG(cxx11_future)

/*
    On architectures and platforms we know, interpret the thread control
    block (TCB) as a unique identifier for a thread within a process. Otherwise,
    fall back to a slower but safe implementation.

    As per the documentation of currentThreadId, we return an opaque handle
    as a thread identifier, and application code is not supposed to use that
    value for anything. In Qt we use the handle to check if threads are identical,
    for which the TCB is sufficient.

    So we use the fastest possible way, rathern than spend time on returning
    some pseudo-interoperable value.
*/
inline Qt::HANDLE QThread::currentThreadId() noexcept
{
    Qt::HANDLE tid; // typedef to void*
    Q_STATIC_ASSERT(sizeof(tid) == sizeof(void*));
    // See https://akkadia.org/drepper/tls.pdf for x86 ABI
#if defined(Q_PROCESSOR_X86_32) && defined(Q_OS_LINUX) // x86 32-bit always uses GS
    __asm__("movl %%gs:0, %0" : "=r" (tid) : : );
#elif defined(Q_PROCESSOR_X86_64) && defined(Q_OS_DARWIN64)
    // 64bit macOS uses GS, see https://github.com/apple/darwin-xnu/blob/master/libsyscall/os/tsd.h
    __asm__("movq %%gs:0, %0" : "=r" (tid) : : );
#elif defined(Q_PROCESSOR_X86_64) && (defined(Q_OS_LINUX) || defined(Q_OS_FREEBSD))
    // x86_64 Linux, BSD uses FS
    __asm__("movq %%fs:0, %0" : "=r" (tid) : : );
#else
    tid = currentThreadIdImpl();
#endif
    return tid;
}

QT_END_NAMESPACE

#endif // QTHREAD_H
