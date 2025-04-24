/*
 * Copyright (C) 2025 pdnguyen of HCMC University of Technology VNU-HCM
 */

/* Sierra release
 * Source Code License Grant: The authors hereby grant to Licensee
 * personal permission to use and modify the Licensed Source Code
 * for the sole purpose of studying while attending the course CO2018.
 */

#include "common.h"
#include "syscall.h"
#include "stdio.h"
#include "libmem.h"

// Cac thu vien ben duoi tu add
#include "string.h"
#include "queue.h"
#include "sched.h"
#include "mm.h"

int __sys_killall(struct pcb_t *caller, struct sc_regs *regs)
{
    char proc_name[100];
    uint32_t data;
    // hardcode for demo only
    uint32_t memrg = regs->a1; // ID of mem region that store proc naame need to kill

    /* TODO: Get name of the target proc */
    // proc_name = libread..
    int i = 0;
    data = 0;
    while (data != -1)
    {
        libread(caller, memrg, i, &data);
        proc_name[i] = data;
        if (data == -1)
            proc_name[i] = '\0';
        i++;
    }

    printf("The procname retrieved from memregionid %d is \"%s\"\n", memrg, proc_name);

    /* TODO: Traverse proclist to terminate the proc
     *       stcmp to check the process match proc_name
     */
    struct pcb_t *kill_process[MAX_PRIO];
    int index = 0;

    struct queue_t *run_list = caller->running_list;
    struct queue_t *mlq = caller->mlq_ready_queue;

    for (int i = 0; i < run_list->size; i++)
    {
        char *proc_get_name = strrchr(run_list->proc[i]->path, '/');
        if (proc_get_name && strcmp(proc_get_name + 1, proc_name) == 0)
        {
            kill_process[index++] = run_list->proc[i];
            run_list->proc[i]->pc = run_list->proc[i]->code->size;
            for (int j = i; j < run_list->size - 1; j++)
            {
                run_list->proc[j] = run_list->proc[j + 1];
            }
            run_list->size--;
            i--;
        }
    }

    for (int i = 0; i < MAX_PRIO; i++)
    {
        for (int j = 0; j < mlq[i].size; j++)
        {
            char *mlq_proc_get_name = strrchr(mlq[i].proc[j]->path, '/');
            if (mlq_proc_get_name && strcmp(mlq_proc_get_name + 1, proc_name) == 0)
            {
                kill_process[index++] = mlq[i].proc[j];
                mlq[i].proc[j]->pc = mlq[i].proc[j]->code->size;
                for (int k = 0; k < mlq[i].size - 1; k++)
                {
                    mlq[i].proc[k] = mlq[i].proc[k + 1];
                }
                mlq[i].size--;
                j--;
            }
        }
    }

    /* TODO Maching and terminating
     *       all processes with given
     *        name in var proc_name
     */

    return 0;
}