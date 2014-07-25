/*
 * nOS v0.1
 * Copyright (c) 2014 Jim Tremblay
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#define NOS_PRIVATE
#include "nOS.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Internal function */
static void TestFlag (void *payload, void *arg)
{
    nOS_Thread      *thread = (nOS_Thread*)payload;
    nOS_Flag        *flag = (nOS_Flag*)thread->event;
    nOS_FlagContext *ctx = (nOS_FlagContext*)thread->context;
    nOS_FlagResult  *res = (nOS_FlagResult*)arg;
    unsigned int    r;

    /* Verify flags from object with wanted flags from waiting thread. */
    r = flag->flags & ctx->flags;
    if (((ctx->opt & NOS_FLAG_WAIT) == NOS_FLAG_WAIT_ALL) && (r != ctx->flags)) {
        r = NOS_FLAG_NONE;
    }
    /* If conditions are met, wake up the thread and give it the result. */
    if (r != NOS_FLAG_NONE) {
        SignalThread(thread);
        *ctx->rflags = r;
        /* Accumulate awoken flags if waiting thread want to clear it when awoken. */
        if (ctx->opt & NOS_FLAG_CLEAR_ON_EXIT) {
            res->rflags |= r;
        }
        /* Indicate that we need a preemptive scheduling. */
        if (thread->prio > nOS_runningThread->prio) {
            res->sched = 1;
        }
    }
}

/*
 * Name        : nOS_FlagCreate
 *
 * Description : Initialize a flag event object with given flags.
 *
 * Arguments   : flag  : Pointer to flag object.
 *               flags : Initial values.
 *
 * Return      : Error code.
 *               NOS_E_NULL : Pointer to flag object is NULL.
 *               NOS_OK     : Flag initialized with success.
 *
 * Note        : Flag object must be created before using it, else
 *               behavior is undefined and must be called one time
 *               ONLY for each flag object.
 */
int8_t nOS_FlagCreate (nOS_Flag *flag, unsigned int flags)
{
    int8_t  err;

#if NOS_SAFE > 0
    if (flag == NULL) {
        err = NOS_E_FULL;
    } else
#endif
    {
        nOS_CriticalEnter();
        nOS_EventCreate((nOS_Event*)flag);
        flag->flags = flags;
        nOS_CriticalLeave();
        err = NOS_OK;
    }

    return err;
}

/*
 * Name        : nOS_FlagWait
 *
 * Description : Wait on flag object for given flags. If flags are NOT set, calling
 *               thread will be placed in object's waiting list for number of ticks
 *               specified by tout. If flags are set before end of timeout, res
 *               will contain flags that have awoken the thread. If caller specify
 *               NOS_FLAG_CLEAR_ON_EXIT, ONLY awoken flags will be cleared.
 *
 * Arguments   : flag  : Pointer to flag object.
 *               opt   : Waiting options
 *                       NOS_FLAG_WAIT_ALL      : Wait for all flags to be set.
 *                       NOS_FLAG_WAIT_ANY      : Wait for any flags to be set.
 *                       NOS_FLAG_CLEAR_ON_EXIT : Clear woken flags.
 *               flags : All flags to wait.
 *               res   : Pointer where to store awoken flags if needed. Only valid if
 *                       returned error code is NOS_OK. Otherwise, res is unchanged.
 *               tout  : Timeout value
 *                       NOS_NO_WAIT      : No waiting.
 *                       NOS_WAIT_INIFINE : Never timeout.
 *                       0 > tout < 65535 : Number of ticks to wait on flag object.
 *
 * Return      : Error code.
 *               NOS_E_NULL    : Pointer to flag object is NULL.
 *               NOS_E_ISR     : Called from interrupt.
 *               NOS_E_LOCKED  : Called with scheduler locked.
 *               NOS_E_IDLE    : Called from idle thread.
 *               NOS_E_AGAIN   : Flags NOT in wanted state and tout == 0.
 *               NOS_E_TIMEOUT : Flags NOT in wanted state after tout ticks.
 *               NOS_OK        : Flags are in wanted state.
 *
 * Note        : Safe to be called from threads ONLY with scheduler unlocked.
 */
int8_t nOS_FlagWait (nOS_Flag *flag, uint8_t opt, unsigned int flags, unsigned int *res, uint16_t tout)
{
    int8_t          err;
    nOS_FlagContext ctx;
    unsigned int    r;

#if NOS_SAFE > 0
    if (flag == NULL) {
        err = NOS_E_NULL;
    } else
#endif
    if (nOS_isrNestingCounter > 0) {
        err = NOS_E_ISR;
    } else if (nOS_lockNestingCounter > 0) {
        err = NOS_E_LOCKED;
    } else if ((nOS_runningThread == &nOS_mainThread) && (tout > 0)) {
        err = NOS_E_IDLE;
    } else {
        nOS_CriticalEnter();
        r = flag->flags & flags;
        /* If thread is waiting for ALL flags, then clear result if NOT ALL flags set. */
        if (((opt & NOS_FLAG_WAIT) == NOS_FLAG_WAIT_ALL) && (r != flags)) {
            r = NOS_FLAG_NONE;
        }
        /* If result is not cleared, then condition is met for waiting thread. */
        if (r != NOS_FLAG_NONE) {
            err = NOS_OK;
        /* Calling thread can wait only if tout is higher than 0. */
        } else if (tout > 0) {
            ctx.flags   = flags;
            ctx.opt     = opt;
            ctx.rflags  = &r;
            nOS_runningThread->context = &ctx;
            err = nOS_EventWait((nOS_Event*)flag, NOS_WAITING_FLAG, tout);
        /* Flags are NOT set and caller can't wait, then try again. */
        } else {
            err = NOS_E_AGAIN;
        }
        nOS_CriticalLeave();

        /* Return awoken flags if succeed to wait on flag object. */
        if (err == NOS_OK) {
            if (res != NULL) {
                *res = r;
            }
        }
    }

    return err;
}

/*
 * Name        : nOS_FlagSet
 *
 * Description : Set/Clear given flags on flag object. Many flags can be set and clear
 *               at the same time atomically. Can clear flags that has just been set
 *               if waiting threads as requested NOS_FLAG_CLEAR_ON_EXIT.
 *
 * Arguments   : flag  : Pointer to flag object.
 *               flags : All flags value to set or clear depending on mask.
 *               mask  : Mask containing which flags to affect. If corresponding bit
 *                       in flags is 0, this bit will be cleared. If corresponding
 *                       bit in flags is 1, this bit will be set.
 *
 * Return      : Error code.
 *               NOS_E_NULL    : Pointer to flag object is NULL.
 *               NOS_OK        : Flags are set/clear successfully.
 *
 * Note        : Safe to be called from threads, idle and ISR.
 */
int8_t nOS_FlagSet (nOS_Flag *flag, unsigned int flags, unsigned int mask)
{
    int8_t          err;
    nOS_FlagResult  res;

#if NOS_SAFE > 0
    if (flag == NULL) {
        err = NOS_E_NULL;
    } else
#endif
    {
        res.rflags = NOS_FLAG_NONE;
        res.sched = 0;
        nOS_CriticalEnter();
        flag->flags = flag->flags ^ ((flag->flags ^ flags) & mask);
        nOS_ListWalk(&flag->e.waitingList, TestFlag, &res);
        /* Clear all flags that have awoken the waiting threads. */
        flag->flags &= ~res.rflags;
        nOS_CriticalLeave();
        /* Schedule only if one of awoken thread has an higher priority. */
        if (res.sched != 0) {
            nOS_Sched();
        }
        err = NOS_OK;
    }

    return err;
}

#ifdef __cplusplus
}
#endif