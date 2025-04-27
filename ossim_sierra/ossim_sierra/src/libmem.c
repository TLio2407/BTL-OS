/*
 * libmem.c — Memory Module Library (Paging)
 *
 * Copyright (C) 2025 pdnguyen
 * HCMC University of Technology VNU-HCM, CO2018
 *
 * Sierra release
 * License: personal permission for course study.
 */

#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include "string.h"
#include "mm.h"
#include "syscall.h"
#include "libmem.h"

// Mutex protecting VM region lists and page operations
static pthread_mutex_t mmvm_lock = PTHREAD_MUTEX_INITIALIZER;

/*
 * enlist_vm_freerg_list - add new rg to free list
 */
int enlist_vm_freerg_list(struct mm_struct *mm, struct vm_rg_struct *rg_elmt)
{
  if (!mm || !mm->mmap || !rg_elmt)
    return -1;
  if (rg_elmt->rg_start >= rg_elmt->rg_end)
    return -1;

  pthread_mutex_lock(&mmvm_lock);
  rg_elmt->rg_next = mm->mmap->vm_freerg_list;
  mm->mmap->vm_freerg_list = rg_elmt;
  pthread_mutex_unlock(&mmvm_lock);
  return 0;
}

/*
 * get_symrg_byid - get region by ID
 */
struct vm_rg_struct *get_symrg_byid(struct mm_struct *mm, int rgid)
{
  if (!mm || rgid < 0 || rgid > PAGING_MAX_SYMTBL_SZ)
    return NULL;
  return &mm->symrgtbl[rgid];
}

/*
 * get_free_vmrg_area - find free VM region of given size
 */
int get_free_vmrg_area(struct pcb_t *caller, int vmaid, int size, struct vm_rg_struct *newrg)
{
  if (!caller || !caller->mm || !newrg || size <= 0)
    return -1;
  struct vm_area_struct *vma = get_vma_by_num(caller->mm, vmaid);
  if (!vma)
    return -1;

  pthread_mutex_lock(&mmvm_lock);
  struct vm_rg_struct *curr = vma->vm_freerg_list;
  struct vm_rg_struct *prev = NULL;
  int aligned = PAGING_PAGE_ALIGNSZ(size);

  while (curr)
  {
    int chunk = curr->rg_end - curr->rg_start;
    if (chunk >= aligned)
    {
      newrg->rg_start = curr->rg_start;
      newrg->rg_end = curr->rg_start + aligned;
      // adjust free list
      if (chunk == aligned)
      {
        if (prev)
          prev->rg_next = curr->rg_next;
        else
          vma->vm_freerg_list = curr->rg_next;
        free(curr);
      }
      else
      {
        curr->rg_start += aligned;
      }
      pthread_mutex_unlock(&mmvm_lock);
      return 0;
    }
    prev = curr;
    curr = curr->rg_next;
  }
  pthread_mutex_unlock(&mmvm_lock);
  return -1;
}

/*
 * __alloc - allocate a region
 */
int __alloc(struct pcb_t *caller, int vmaid, int rgid, int size, int *alloc_addr)
{
  if (!caller || !caller->mm || size <= 0 || !alloc_addr)
    return -1;
  struct vm_rg_struct rgnode;
  // Try to allocate from free list
  if (get_free_vmrg_area(caller, vmaid, size, &rgnode) == 0)
  {
    pthread_mutex_lock(&mmvm_lock);
    caller->mm->symrgtbl[rgid] = rgnode;
    *alloc_addr = rgnode.rg_start;
    pthread_mutex_unlock(&mmvm_lock);
    return 0;
  }
  // No space: request heap extension via sys_memmap
  struct vm_area_struct *vma = get_vma_by_num(caller->mm, vmaid);
  if (!vma)
    return -1;
  int inc = PAGING_PAGE_ALIGNSZ(size);
  struct sc_regs regs = {0};
  regs.a1 = SYSMEM_INC_OP;
  regs.a2 = vmaid;
  regs.a3 = inc;
  if (syscall(caller, 17, &regs) < 0)
    return -1;
  // Retry allocation
  if (get_free_vmrg_area(caller, vmaid, size, &rgnode) < 0)
    return -1;
  pthread_mutex_lock(&mmvm_lock);
  caller->mm->symrgtbl[rgid] = rgnode;
  *alloc_addr = rgnode.rg_start;
  pthread_mutex_unlock(&mmvm_lock);
  // extend vma->sbrk if needed
  if (vma->sbrk == rgnode.rg_start)
    vma->sbrk += inc;
  return 0;
}

/*
 * __free - free a region
 */
int __free(struct pcb_t *caller, int vmaid, int rgid)
{
  if (!caller || !caller->mm)
    return -1;
  struct vm_rg_struct *sym = get_symrg_byid(caller->mm, rgid);
  if (!sym || sym->rg_start < 0 || sym->rg_end <= sym->rg_start)
    return -1;
  struct vm_rg_struct *node = malloc(sizeof(*node));
  if (!node)
    return -1;
  pthread_mutex_lock(&mmvm_lock);
  *node = *sym;
  pthread_mutex_unlock(&mmvm_lock);
  // add back to free list
  if (enlist_vm_freerg_list(caller->mm, node) < 0)
  {
    free(node);
    return -1;
  }
  // invalidate symbol entry
  pthread_mutex_lock(&mmvm_lock);
  sym->rg_start = sym->rg_end = -1;
  pthread_mutex_unlock(&mmvm_lock);
  return 0;
}

/* wrappers */
int liballoc(struct pcb_t *proc, uint32_t size, uint32_t reg_index)
{
  int addr;
  if (__alloc(proc, 0, reg_index, size, &addr) == 0)
  {
    proc->regs[reg_index] = addr;
    #ifdef IODUMP
    printf("===== PHYSICAL MEMORY AFTER ALLOCATION =====\n");
    printf("PID=%d - Region=%d - Address=%d - Size=%d byte\n", proc->pid, reg_index, addr, size);
#ifdef PAGETBL_DUMP
    print_pgtbl(proc, 0, -1);
#endif
    //MEMPHY_dump(proc->mram);
    printf("====================================================================\n");
#endif
    return 0;
  }
  #ifdef IODUMP
    printf("===== PHYSICAL MEMORY AFTER ALLOCATION =====\n");
    printf("PID=%d - Region=%d - Address=%d - Size=%d byte\n", proc->pid, reg_index, addr, size);
#ifdef PAGETBL_DUMP
    print_pgtbl(proc, 0, -1);
#endif
    //MEMPHY_dump(proc->mram);
    printf("====================================================================\n");
#endif
  return -1;
}

int libfree(struct pcb_t *proc, uint32_t reg_index)
{
  if (__free(proc, 0, reg_index) == 0)
  {
    proc->regs[reg_index] = 0;
    
#ifdef IODUMP
printf("===== PHYSICAL MEMORY AFTER DEALLOCATION =====\n");
printf("PID=%d - Region=%d\n", proc->pid, reg_index);
#ifdef PAGETBL_DUMP
print_pgtbl(proc, 0, -1);
#endif
//MEMPHY_dump(proc->mram);
printf("====================================================================\n");
#endif
    return 0;
  }
  
#ifdef IODUMP
printf("===== PHYSICAL MEMORY AFTER DEALLOCATION =====\n");
printf("PID=%d - Region=%d\n", proc->pid, reg_index);
#ifdef PAGETBL_DUMP
print_pgtbl(proc, 0, -1);
#endif
//MEMPHY_dump(proc->mram);
printf("====================================================================\n");
#endif
  return -1;
}

/* paging helpers */

int find_victim_page(struct mm_struct *mm, int *retpgn)
{
  if (!mm || !mm->fifo_pgn || !retpgn)
    return -1;
  struct pgn_t *head = mm->fifo_pgn;
  *retpgn = head->pgn;
  mm->fifo_pgn = head->pg_next;
  free(head);
  return 0;
}

int pg_getpage(struct mm_struct *mm, int pgn, int *fpn, struct pcb_t *caller)
{
  if (!mm || !fpn || !caller || pgn < 0 || pgn >= PAGING_MAX_PGN)
    return -1;
  uint32_t pte = mm->pgd[pgn];
  if (!PAGING_PAGE_PRESENT(pte))
  {
    int vicpgn, swpp;
    if (find_victim_page(mm, &vicpgn) < 0)
      return -1;
    uint32_t vicpte = mm->pgd[vicpgn];
    int vicfpn = PAGING_FPN(vicpte);
    if (MEMPHY_get_freefp(caller->active_mswp, &swpp) < 0)
      return -1;
    // swap victim -> swap
    struct sc_regs r1 = {.a1 = SYSMEM_SWP_OP, .a2 = vicfpn, .a3 = swpp};
    if (syscall(caller, 17, &r1) < 0)
      return -1;
    pte_set_swap(&mm->pgd[vicpgn], 0, swpp);
    // swap target out
    int tgtfpn = PAGING_PTE_SWP(pte);
    struct sc_regs r2 = {.a1 = SYSMEM_SWP_OP, .a2 = tgtfpn, .a3 = vicfpn};
    if (syscall(caller, 17, &r2) < 0)
      return -1;
    // update PTE
    pte_set_fpn(&mm->pgd[pgn], vicfpn);
    PAGING_PTE_SET_PRESENT(mm->pgd[pgn]);
    enlist_pgn_node(&caller->mm->fifo_pgn, pgn);
    *fpn = vicfpn;
    return 0;
  }
  *fpn = PAGING_FPN(pte);
  return 0;
}

int pg_getval(struct mm_struct *mm, int addr, BYTE *data, struct pcb_t *caller)
{
  if (!mm || !data || !caller)
    return -1;
  int pgn = PAGING_PGN(addr);
  int off = PAGING_OFFST(addr);
  int fpn;
  if (pg_getpage(mm, pgn, &fpn, caller) < 0)
    return -1;
  int phy = (fpn << PAGING_ADDR_FPN_LOBIT) + off;
  struct sc_regs regs = {.a1 = SYSMEM_IO_READ, .a2 = phy};
  syscall(caller, 17, &regs);
  *data = (BYTE)regs.a3;
  return 0;
}

int pg_setval(struct mm_struct *mm, int addr, BYTE value, struct pcb_t *caller)
{
  if (!mm || !caller)
    return -1;
  int pgn = PAGING_PGN(addr);
  int off = PAGING_OFFST(addr);
  int fpn;
  if (pg_getpage(mm, pgn, &fpn, caller) < 0)
    return -1;
  int phy = (fpn << PAGING_ADDR_FPN_LOBIT) + off;
  struct sc_regs regs = {.a1 = SYSMEM_IO_WRITE, .a2 = phy, .a3 = value};
  syscall(caller, 17, &regs);
  pthread_mutex_lock(&mmvm_lock);
  SETBIT(mm->pgd[pgn], PAGING_PTE_PRESENT_MASK);
  SETBIT(mm->pgd[pgn], PAGING_PTE_DIRTY_MASK);
  pthread_mutex_unlock(&mmvm_lock);
  return 0;
}

int __read(struct pcb_t *caller, int vmaid, int rgid, int offset, BYTE *data)
{
  if (!caller || !data)
    return -1;
  struct vm_rg_struct *rg = get_symrg_byid(caller->mm, rgid);
  struct vm_area_struct *vma = get_vma_by_num(caller->mm, vmaid);
  if (!rg || !vma)
    return -1;
  return pg_getval(caller->mm, rg->rg_start + offset, data, caller);
}

int libread(struct pcb_t *proc, uint32_t source, uint32_t offset, uint32_t *dest)
{
  BYTE b;
  if (__read(proc, 0, source, offset, &b) == 0)
  {
    *dest = (uint32_t)b;
    return 0;
  }
  return -1;
}

int __write(struct pcb_t *caller, int vmaid, int rgid, int offset, BYTE val)
{
  struct vm_rg_struct *rg = get_symrg_byid(caller->mm, rgid);
  struct vm_area_struct *vma = get_vma_by_num(caller->mm, vmaid);
  if (!rg || !vma)
    return -1;
  return pg_setval(caller->mm, rg->rg_start + offset, val, caller);
}

int libwrite(struct pcb_t *proc, BYTE data, uint32_t destination, uint32_t offset)
{
  
#ifdef IODUMP
printf("===== PHYSICAL MEMORY AFTER WRITING =====\n");
printf("write region=%d offset=%d value=%d\n", destination, offset, data);
#ifdef PAGETBL_DUMP
print_pgtbl(proc, 0, -1); //print max TBL
#endif
//MEMPHY_dump(proc->mram);
printf("====================================================================\n");
#endif
  return __write(proc, 0, destination, offset, data);
}

int free_pcb_memph(struct pcb_t *caller)
{
  if (!caller || !caller->mm)
    return -1;
  for (int i = 0; i < PAGING_MAX_PGN; i++)
  {
    uint32_t pte = caller->mm->pgd[i];
    int fpn = PAGING_PAGE_PRESENT(pte)
                  ? PAGING_PTE_FPN(pte)
                  : PAGING_PTE_SWP(pte);
    MEMPHY_put_freefp(
        PAGING_PAGE_PRESENT(pte) ? caller->active_mswp : caller->mram,
        fpn);
  }
  return 0;
}
