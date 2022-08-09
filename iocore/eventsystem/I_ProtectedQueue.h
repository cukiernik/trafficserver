/** @file

  A brief file description

  @section license License

  Licensed to the Apache Software Foundation (ASF) under one
  or more contributor license agreements.  See the NOTICE file
  distributed with this work for additional information
  regarding copyright ownership.  The ASF licenses this file
  to you under the Apache License, Version 2.0 (the
  "License"); you may not use this file except in compliance
  with the License.  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
 */

/****************************************************************************

  Protected Queue, a FIFO queue with the following functionality:
  (1). Multiple threads could be simultaneously trying to enqueue
       and dequeue. Hence the queue needs to be protected with mutex.
  (2). In case the queue is empty, dequeue() sleeps for a specified
       amount of time, or until a new element is inserted, whichever
       is earlier


 ****************************************************************************/
#pragma once

#include "tscore/ink_platform.h"
#include "I_Event.h"

struct ProtectedQueue {
  void enqueue(Event *e);
  int try_signal();             // Use non blocking lock and if acquired, signal
  void enqueue_local(Event *e); // Safe when called from the same thread
  Event *dequeue_local();
#if TS_USE_NUMA_NODE
  InkAtomicList al[1+32] __attribute__((aligned( 0x1000))); // index 0 for non numa node assigned, 1 for numa node 0,2 for node 1,..
  void dequeue_external(enum Continuation::numa_node numa_node);       // Dequeue external events for node.
  void wait(ink_hrtime timeout,enum Continuation::numa_node numa_node); // Wait for @a timeout nanoseconds on a condition variable if there are no events.
#else
  InkAtomicList al;
  void dequeue_external();       // Dequeue any external events.
  void wait(ink_hrtime timeout); // Wait for @a timeout nanoseconds on a condition variable if there are no events.
#endif
  ink_mutex lock;
  ink_cond might_have_data;
  Que(Event, link) localQueue;

  ProtectedQueue();
};
