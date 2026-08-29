/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * AbstractX Domain Bridge / Coroutine-I/O Dispatcher
 * -------------------------------------------------
 * Provides the queue and resume bridge between the cooperative coroutine domain
 * and the external I/O domain. The bridge is intentionally generic: the queue
 * policy is selected by the platform and can be ISR-safe, locked, priority-aware,
 * or processor/thread specific.
 *
 * RISC-V ISR case: use an interrupt-masked queue with push/pop flag save/restore.
 * Future cases: core-to-core shared-memory queue, thread/task message queue,
 * or priority-ordered dispatcher queue.
 */

#ifndef ABSTRACTX_ISR_DISPATCHER_HPP
#define ABSTRACTX_ISR_DISPATCHER_HPP

#include "abstractx/domain_dispatcher.hpp"

#endif // ABSTRACTX_ISR_DISPATCHER_HPP
