/* This file is derived from source code for the Nachos
   instructional operating system.  The Nachos copyright notice
   is reproduced in full below. */

/* Copyright (c) 1992-1996 The Regents of the University of California.
   All rights reserved.

   Permission to use, copy, modify, and distribute this software
   and its documentation for any purpose, without fee, and
   without written agreement is hereby granted, provided that the
   above copyright notice and the following two paragraphs appear
   in all copies of this software.

   IN NO EVENT SHALL THE UNIVERSITY OF CALIFORNIA BE LIABLE TO
   ANY PARTY FOR DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR
   CONSEQUENTIAL DAMAGES ARISING OUT OF THE USE OF THIS SOFTWARE
   AND ITS DOCUMENTATION, EVEN IF THE UNIVERSITY OF CALIFORNIA
   HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

   THE UNIVERSITY OF CALIFORNIA SPECIFICALLY DISCLAIMS ANY
   WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
   WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
   PURPOSE.  THE SOFTWARE PROVIDED HEREUNDER IS ON AN "AS IS"
   BASIS, AND THE UNIVERSITY OF CALIFORNIA HAS NO OBLIGATION TO
   PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR
   MODIFICATIONS.
*/

#include "threads/synch.h"
#include <stdio.h>
#include <string.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

bool compare_priority_waiter (struct list_elem *a,
                              struct list_elem *b,
                              void *aux);

/* Initializes semaphore SEMA to VALUE.  A semaphore is a
   nonnegative integer along with two atomic operators for
   manipulating it:

   - down or "P": wait for the value to become positive, then
     decrement it.

   - up or "V": increment the value (and wake up one waiting
     thread, if any). */
void
sema_init (struct semaphore *sema, unsigned value) 
{
  ASSERT (sema != NULL);

  sema->value = value;
  list_init (&sema->waiters);
}

/* Down or "P" operation on a semaphore.  Waits for SEMA's value
   to become positive and then atomically decrements it.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but if it sleeps then the next scheduled
   thread will probably turn interrupts back on. This is
   sema_down function. */
void
sema_down (struct semaphore *sema) 
{
  enum intr_level old_level;

  ASSERT (sema != NULL);
  ASSERT (!intr_context ());

  old_level = intr_disable ();
  while (sema->value == 0) 
  // if (sema->value <= 0)
    {
      list_insert_ordered (&sema->waiters, &thread_current ()->elem_sema, &compare_priority, "elem_sema");
      thread_block ();
    }
  sema->value--;
  intr_set_level (old_level);
}

/* Down or "P" operation on a semaphore, but only if the
   semaphore is not already 0.  Returns true if the semaphore is
   decremented, false otherwise.

   This function may be called from an interrupt handler. */
bool
sema_try_down (struct semaphore *sema) 
{
  enum intr_level old_level;
  bool success;

  ASSERT (sema != NULL);

  old_level = intr_disable ();
  if (sema->value > 0) 
    {
      sema->value--;
      success = true; 
    }
  else
    success = false;
  intr_set_level (old_level);

  return success;
}

/* Up or "V" operation on a semaphore.  Increments SEMA's value
   and wakes up one thread of those waiting for SEMA, if any.

   This function may be called from an interrupt handler. */
void
sema_up (struct semaphore *sema) 
{
  enum intr_level old_level;

  ASSERT (sema != NULL);

  old_level = intr_disable ();
  sema->value++;
  list_sort(&sema->waiters, &compare_priority, "elem_sema");
  if (sema->value > 0){
    if (!list_empty (&sema->waiters)) {
      thread_unblock (list_entry (list_pop_front (&sema->waiters),
                                  struct thread, elem_sema));
      if (thread_current () != idle_thread)
        thread_yield ();
    }
  }
  intr_set_level (old_level);
}

static void sema_test_helper (void *sema_);

/* Self-test for semaphores that makes control "ping-pong"
   between a pair of threads.  Insert calls to printf() to see
   what's going on. */
void
sema_self_test (void) 
{
  struct semaphore sema[2];
  int i;

  printf ("Testing semaphores...");
  sema_init (&sema[0], 0);
  sema_init (&sema[1], 0);
  thread_create ("sema-test", PRI_DEFAULT, sema_test_helper, &sema);
  for (i = 0; i < 10; i++) 
    {
      sema_up (&sema[0]);
      sema_down (&sema[1]);
    }
  printf ("done.\n");
}

/* Thread function used by sema_self_test(). */
static void
sema_test_helper (void *sema_) 
{
  struct semaphore *sema = sema_;
  int i;

  for (i = 0; i < 10; i++) 
    {
      sema_down (&sema[0]);
      sema_up (&sema[1]);
    }
}

bool compare_priority_lock (struct list_elem *a,
                       struct list_elem *b,
                       void *aux){
  struct lock *A = list_entry(a, struct lock, elem_lock);
  struct lock *B = list_entry(b, struct lock, elem_lock);

  if (list_empty(&(&A->semaphore)->waiters) || list_empty(&(&B->semaphore)->waiters)){
    return true;
  }

  int A_priority = list_entry(list_front(&(&A->semaphore)->waiters), struct thread, elem_sema);
  int B_priority = list_entry(list_front(&(&B->semaphore)->waiters), struct thread, elem_sema);


  if (A_priority > B_priority)
    return true;
  else 
    return false;
}


/* Initializes LOCK.  A lock can be held by at most a single
   thread at any given time.  Our locks are not "recursive", that
   is, it is an error for the thread currently holding a lock to
   try to acquire that lock.

   A lock is a specialization of a semaphore with an initial
   value of 1.  The difference between a lock and such a
   semaphore is twofold.  First, a semaphore can have a value
   greater than 1, but a lock can only be owned by a single
   thread at a time.  Second, a semaphore does not have an owner,
   meaning that one thread can "down" the semaphore and then
   another one "up" it, but with a lock the same thread must both
   acquire and release it.  When these restrictions prove
   onerous, it's a good sign that a semaphore should be used,
   instead of a lock. */
void
lock_init (struct lock *lock)
{
  ASSERT (lock != NULL);

  list_init(&lock->wait_thread_list);
  lock->num_waiters = 0;
  lock->holder = NULL;
  sema_init (&lock->semaphore, 1);
}

/* Acquires LOCK, sleeping until it becomes available if
   necessary.  The lock must not already be held by the current
   thread.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but interrupts will be turned back on if
   we need to sleep. */
void
lock_acquire (struct lock *lock)
{
  ASSERT (lock != NULL);
  ASSERT (!intr_context ());
  ASSERT (!lock_held_by_current_thread (lock));

  struct semaphore lock_sema = lock->semaphore;
  struct thread *holder = lock->holder;
  int i = 0;
  struct thread *holder_of_holder;
  struct lock *lock_of_holder;
  struct list_elem *e;
  struct thread *first_waiter;

  if (holder != NULL){ 
    if ((&lock_sema)->value == 0 && holder->priority < thread_current ()->priority){ // or test if lock->semaphore->value == 0
      holder->priority = thread_current ()->priority;
      list_push_back(&thread_current()->wait_list, &lock->elem_wait);
      lock->num_waiters++;
      list_insert_ordered(&lock->wait_thread_list, &thread_current()->elem_wait_lock, &compare_priority, "elem_wait_lock");
      e = list_begin(&holder->wait_list);
      for (i = 0; i < 2; i++){
        if (e == list_tail(&holder->wait_list)){
          break;
        }
        lock_of_holder = list_entry(e, struct lock, elem_wait);
        holder_of_holder = lock_of_holder->holder;
        if (holder_of_holder->priority < thread_current()->priority){
          holder_of_holder->priority = thread_current()->priority;
        }
        e = e->next;
      }
      sema_down (&lock->semaphore);
      list_remove(&lock->elem_wait);
      list_remove(&thread_current()->elem_wait_lock);
      lock->num_waiters--;
    }
    else {
      sema_down (&lock->semaphore);
    }
  }
  else {
    sema_down(&lock->semaphore);
  }

  list_insert_ordered(&thread_current()->lock_list, &lock->elem_lock, &compare_priority_lock, NULL);
  lock->holder = thread_current ();
}

/* Tries to acquires LOCK and returns true if successful or false
   on failure.  The lock must not already be held by the current
   thread.

   This function will not sleep, so it may be called within an
   interrupt handler. */
bool
lock_try_acquire (struct lock *lock)
{
  bool success;

  ASSERT (lock != NULL);
  ASSERT (!lock_held_by_current_thread (lock));

  success = sema_try_down (&lock->semaphore);
  if (success)
    lock->holder = thread_current ();
  return success;
}

/* Releases LOCK, which must be owned by the current thread.
   This is lock_release function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to release a lock within an interrupt
   handler. */
void
lock_release (struct lock *lock) 
{
  ASSERT (lock != NULL);
  ASSERT (lock_held_by_current_thread (lock));

  struct thread *holder = lock->holder;
  struct list_elem *e;
  struct list_elem *e2;
  struct thread *t;
  int i = 0, j = 0;
  struct list lock_waiters;
  int max_priority = holder->creation_priority;
  int max_lock_priority = 0;
  struct lock *lock_of_holder;

  struct thread *high_priority_waiter;
  struct lock *high_priority_lock;

  list_remove(&lock->elem_lock);

  if (!list_empty(&lock->wait_thread_list)){
    high_priority_waiter = list_entry(list_front(&lock->wait_thread_list), struct thread, elem_wait_lock);
    if (holder->priority >= high_priority_waiter->priority){
      max_priority = holder->creation_priority;
        // printf("P1\n");
    }
  }
  if ( ! list_empty(&holder->lock_list)){
    high_priority_lock = list_entry(list_front(&holder->lock_list), struct lock, elem_lock);
    if (! list_empty(&high_priority_lock->wait_thread_list)){ 
      max_lock_priority = list_entry(list_front(&high_priority_lock->wait_thread_list), struct thread, elem_wait_lock)->priority;
    }
    // printf("P1, priority: %d\n", max_priority);
    max_priority = max_priority > max_lock_priority ? max_priority : max_lock_priority;
  }
  holder->priority = max_priority;

  lock->holder = NULL;
  sema_up (&lock->semaphore);
}

/* Returns true if the current thread holds LOCK, false
   otherwise.  (Note that testing whether some other thread holds
   a lock would be racy.) */
bool
lock_held_by_current_thread (const struct lock *lock) 
{
  ASSERT (lock != NULL);

  return lock->holder == thread_current ();
}

/* One semaphore in a list. */
struct semaphore_elem 
  {
    struct list_elem elem;              /* List element. */
    struct semaphore semaphore;         /* This semaphore. */
    int priority;
  };

/* Initializes condition variable COND.  A condition variable
   allows one piece of code to signal a condition and cooperating
   code to receive the signal and act upon it. */
void
cond_init (struct condition *cond)
{
  ASSERT (cond != NULL);

  list_init (&cond->waiters);
}

bool compare_priority_waiter (struct list_elem *a,
                              struct list_elem *b,
                              void *aux){
  struct semaphore_elem *A = list_entry(a, struct semaphore_elem, elem);
  struct semaphore_elem *B = list_entry(b, struct semaphore_elem, elem);

  if (A->priority > B->priority)
    return true;
  else 
    return false;
}

/* Atomically releases LOCK and waits for COND to be signaled by
   some other piece of code.  After COND is signaled, LOCK is
   reacquired before returning.  LOCK must be held before calling
   this function.

   The monitor implemented by this function is "Mesa" style, not
   "Hoare" style, that is, sending and receiving a signal are not
   an atomic operation.  Thus, typically the caller must recheck
   the condition after the wait completes and, if necessary, wait
   again.

   A given condition variable is associated with only a single
   lock, but one lock may be associated with any number of
   condition variables.  That is, there is a one-to-many mapping
   from locks to condition variables.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but interrupts will be turned back on if
   we need to sleep. */
void
cond_wait (struct condition *cond, struct lock *lock) 
{
  struct semaphore_elem waiter;

  ASSERT (cond != NULL);
  ASSERT (lock != NULL);
  ASSERT (!intr_context ());
  ASSERT (lock_held_by_current_thread (lock));
  
  sema_init (&waiter.semaphore, 0);
  int *p = (&waiter.priority);
  *p = thread_current ()->priority;
  list_insert_ordered (&cond->waiters, &waiter.elem, &compare_priority_waiter, NULL);
  lock_release (lock);
  sema_down (&waiter.semaphore);
  lock_acquire (lock);
}

/* If any threads are waiting on COND (protected by LOCK), then
   this function signals one of them to wake up from its wait.
   LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */
void
cond_signal (struct condition *cond, struct lock *lock UNUSED) 
{
  ASSERT (cond != NULL);
  ASSERT (lock != NULL);
  ASSERT (!intr_context ());
  ASSERT (lock_held_by_current_thread (lock));

  if (!list_empty (&cond->waiters)) 
    sema_up (&list_entry (list_pop_front (&cond->waiters),
                          struct semaphore_elem, elem)->semaphore);
}

/* Wakes up all threads, if any, waiting on COND (protected by
   LOCK).  LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */
void
cond_broadcast (struct condition *cond, struct lock *lock) 
{
  ASSERT (cond != NULL);
  ASSERT (lock != NULL);

  while (!list_empty (&cond->waiters))
    cond_signal (cond, lock);
}
