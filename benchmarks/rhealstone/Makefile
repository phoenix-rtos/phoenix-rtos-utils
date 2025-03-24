#
# Makefile for Rhealstone benchmark suite
#
# Copyright 2025 Phoenix Systems
#
# %LICENSE%
#

NAME := rh_deadlock_break
LOCAL_SRCS := deadlock_break.c
DEP_LIBS := bench_common

include $(binary.mk)

NAME := rh_irq_latency
LOCAL_SRCS := irq_latency.c
DEP_LIBS := bench_common

include $(binary.mk)

NAME := rh_msg_latency
LOCAL_SRCS := msg_latency.c
DEP_LIBS := bench_common

include $(binary.mk)

NAME := rh_preemption
LOCAL_SRCS := preemption.c
DEP_LIBS := bench_common

include $(binary.mk)

NAME := rh_sem_shuffle
LOCAL_SRCS := sem_shuffle.c
DEP_LIBS := bench_common

include $(binary.mk)

NAME := rh_task_switch
LOCAL_SRCS := task_switch.c
DEP_LIBS := bench_common

include $(binary.mk)
