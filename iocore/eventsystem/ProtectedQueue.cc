/** @file

  FIFO queue

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

  @section details Details

  ProtectedQueue implements a FIFO queue with the following functionality:
    -# Multiple threads could be simultaneously trying to enqueue and
      dequeue. Hence the queue needs to be protected with mutex.
    -# In case the queue is empty, dequeue() sleeps for a specified amount
      of time, or until a new element is inserted, whichever is earlier.

*/

#include "P_EventSystem.h"

// The protected queue is designed to delay signaling of threads
// until some amount of work has been completed on the current thread
// in order to prevent excess context switches.
//
// Defining EAGER_SIGNALLING disables this behavior and causes
// threads to be made runnable immediately.
//
// #define EAGER_SIGNALLING

extern ClassAllocator<Event> eventAllocator;

void
ProtectedQueue::enqueue(Event *e)
{
  ink_assert(!e->in_the_prot_queue && !e->in_the_priority_queue);
  EThread *e_ethread   = e->ethread;
  e->in_the_prot_queue = 1;
#if TS_USE_NUMA_NODE
//  fprintf(stderr,"\t%p:%x=>%p\n",e,e->numa_node,al);
  ink_assert((e->numa_node+1)<33);
  bool was_empty = (ink_atomiclist_push(al+1+e->numa_node, e)==nullptr);
#else
  bool was_empty       = (ink_atomiclist_push(&al, e) == nullptr);
#endif

  if (was_empty) {
    EThread *inserting_thread = this_ethread();
    // queue e->ethread in the list of threads to be signalled
    // inserting_thread == 0 means it is not a regular EThread
    if (inserting_thread != e_ethread) {
      e_ethread->tail_cb->signalActivity();
    }
  }
}
#if TS_USE_NUMA_NODE
void
ProtectedQueue::dequeue_external(enum Continuation::numa_node numa_node)
{
  ink_assert((numa_node+1)<sizeof(al)/sizeof(*al));
  SLL<Event, Event::Link_link> t[2]={
    static_cast<Event*>(ink_atomiclist_pop(al+1+numa_node)),
    static_cast<Event*>(ink_atomiclist_pop(al+0))
  };
  for(auto&i:t)
    while(Event*e=i.pop()) {
//      fprintf(stderr,"\t%p=>%p:%x\n",al,e,e->numa_node);
      if (!e->cancelled) {
        localQueue.enqueue(e);
      } else {
        e->mutex = nullptr;
        eventAllocator.free(e);
      }
    }
}
#else
void
ProtectedQueue::dequeue_external()
{
  while (Event *e = t.pop()) {
      localQueue.enqueue(e);
  }
}
#endif

void
ProtectedQueue::wait(ink_hrtime timeout, enum Continuation::numa_node numa_node)
{
  /* If there are no external events available, will do a cond_timedwait.
   *
   *   - The `EThread::lock` will be released,
   *   - And then the Event Thread goes to sleep and waits for the wakeup signal of `EThread::might_have_data`,
   *   - The `EThread::lock` will be locked again when the Event Thread wakes up.
   */
#if TS_USE_NUMA_NODE
    ink_assert((numa_node+1)<sizeof(al)/sizeof(*al));
    if (INK_ATOMICLIST_EMPTY(al[0]) && INK_ATOMICLIST_EMPTY(al[1+numa_node]) && localQueue.empty()) {
#else
  if (INK_ATOMICLIST_EMPTY(al) && localQueue.empty()) {
#endif
    timespec ts = ink_hrtime_to_timespec(timeout);
    ink_cond_timedwait(&might_have_data, &lock, &ts);
  }
}
