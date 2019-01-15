/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2019, Google Inc.
 *
 * event_dispatcher.cpp - Event dispatcher
 */

#include <libcamera/event_dispatcher.h>

/**
 * \file event_dispatcher.h
 */

namespace libcamera {

/**
 * \class EventDispatcher
 * \brief Interface to manage the libcamera events and timers
 *
 * The EventDispatcher class allows the integration of the application event
 * loop with libcamera by abstracting how events and timers are managed and
 * processed.
 *
 * To listen to events, libcamera creates EventNotifier instances and registers
 * them with the dispatcher with registerEventNotifier(). The event notifier
 * \ref EventNotifier::activated signal is then emitted by the dispatcher
 * whenever the event is detected.
 *
 * To set timers, libcamera creates Timer instances and registers them with the
 * dispatcher with registerTimer(). The timer \ref Timer::timeout signal is then
 * emitted by the dispatcher when the timer times out.
 */

EventDispatcher::~EventDispatcher()
{
}

/**
 * \fn EventDispatcher::registerEventNotifier()
 * \brief Register an event notifier
 * \param notifier The event notifier to register
 *
 * Once the \a notifier is registered with the dispatcher, the dispatcher will
 * emit the notifier \ref EventNotifier::activated signal whenever a
 * corresponding event is detected on the notifier's file descriptor. The event
 * is monitored until the notifier is unregistered with
 * unregisterEventNotifier().
 *
 * Registering multiple notifiers for the same file descriptor and event type is
 * not allowed and results in undefined behaviour.
 */

/**
 * \fn EventDispatcher::unregisterEventNotifier()
 * \brief Unregister an event notifier
 * \param notifier The event notifier to unregister
 *
 * After this function returns the \a notifier is guaranteed not to emit the
 * \ref EventNotifier::activated signal.
 *
 * If the notifier isn't registered, this function performs no operation.
 */

/**
 * \fn EventDispatcher::registerTimer()
 * \brief Register a timer
 * \param timer The timer to register
 *
 * Once the \a timer is registered with the dispatcher, the dispatcher will emit
 * the timer \ref Timer::timeout signal when the timer times out. The timer can
 * be unregistered with unregisterTimer() before it times out, in which case the
 * signal will not be emitted.
 *
 * When the \a timer times out, it is automatically unregistered by the
 * dispatcher and can be registered back as early as from the \ref Timer::timeout
 * signal handlers.
 *
 * Registering the same timer multiple times is not allowed and results in
 * undefined behaviour.
 */

/**
 * \fn EventDispatcher::unregisterTimer()
 * \brief Unregister a timer
 * \param timer The timer to unregister
 *
 * After this function returns the \a timer is guaranteed not to emit the
 * \ref Timer::timeout signal.
 *
 * If the timer isn't registered, this function performs no operation.
 */

/**
 * \fn EventDispatcher::processEvents()
 * \brief Wait for and process pending events
 *
 * This function processes all pending events associated with registered event
 * notifiers and timers and signals the corresponding EventNotifier and Timer
 * objects. If no events are pending, it waits for the first event and processes
 * it before returning.
 */

} /* namespace libcamera */