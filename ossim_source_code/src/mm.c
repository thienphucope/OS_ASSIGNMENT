//#ifdef MM_PAGING
/*
 * PAGING based Memory Management
 * Memory management unit mm/mm.c
 */

#include "mm.h"
#include <stdlib.h>
#include <stdio.h>

/* 
 * Page Table Entry (PTE) Initialization Function
 * Configures different states of a page table entry
 */
int init_pte(uint32_t *pte,
             int pre,    // present flag
             int fpn,    // Frame Page Number
             int drt,    // dirty flag
             int swp,    // swap flag
             int swptyp, // swap type
             int swpoff) // swap offset
{
    if (pre != 0) {
        if (swp == 0) { // Page is in memory
            if (fpn == 0) 
                return -1; // Invalid frame page number

            // Set page as present and not swapped
            SETBIT(*pte, PAGING_PTE_PRESENT_MASK);
            CLRBIT(*pte, PAGING_PTE_SWAPPED_MASK);
            CLRBIT(*pte, PAGING_PTE_DIRTY_MASK);

            // Set frame page number
            SETVAL(*pte, fpn, PAGING_PTE_FPN_MASK, PAGING_PTE_FPN_LOBIT); 
        } else { // Page is swapped out
            SETBIT(*pte, PAGING_PTE_PRESENT_MASK);
            SETBIT(*pte, PAGING_PTE_SWAPPED_MASK);
            CLRBIT(*pte, PAGING_PTE_DIRTY_MASK);

            // Set swap type and offset
            SETVAL(*pte, swptyp, PAGING_PTE_SWPTYP_MASK, PAGING_PTE_SWPTYP_LOBIT); 
            SETVAL(*pte, swpoff, PAGING_PTE_SWPOFF_MASK, PAGING_PTE_SWPOFF_LOBIT);
        }
    }

    return 0;   
}

/* 
 * Set PTE for a swapped page
 */
int pte_set_swap(uint32_t *pte, int swptyp, int swpoff)
{
    SETBIT(*pte, PAGING_PTE_PRESENT_MASK);
    SETBIT(*pte, PAGING_PTE_SWAPPED_MASK);

    SETVAL(*pte, swptyp, PAGING_PTE_SWPTYP_MASK, PAGING_PTE_SWPTYP_LOBIT);
    SETVAL(*pte, swpoff, PAGING_PTE_SWPOFF_MASK, PAGING_PTE_SWPOFF_LOBIT);

    return 0;
}

/* 
 * Set PTE for an in-memory page
 */
int pte_set_fpn(uint32_t *pte, int fpn)
{
    SETBIT(*pte, PAGING_PTE_PRESENT_MASK);
    CLRBIT(*pte, PAGING_PTE_SWAPPED_MASK);

    SETVAL(*pte, fpn, PAGING_PTE_FPN_MASK, PAGING_PTE_FPN_LOBIT); 

    return 0;
}

/* 
 * Map a range of pages in the address space
 */
int vmap_page_range(struct pcb_t *caller, 
                    int addr, 
                    int pgnum, 
                    struct framephy_struct *frames,
                    struct vm_rg_struct *ret_rg)
{
    int pgit, pgn = PAGING_PGN(addr);
    struct framephy_struct *current_frame = frames;

    // Update return region information
    ret_rg->rg_start = addr;
    ret_rg->rg_end = addr + pgnum * PAGING_PAGESZ;
    ret_rg->vmaid = 0;  // Default VM area ID

    // Map pages to page directory
    for (pgit = 0; pgit < pgnum; pgit++) {
        if (current_frame == NULL) break;

        // Set Page Table Entry with frame page number
        pte_set_fpn(&caller->mm->pgd[pgn + pgit], current_frame->fpn);

        // Track page for potential replacement
        enlist_pgn_node(&caller->mm->fifo_pgn, pgn + pgit);

        // Move to next frame
        current_frame = current_frame->fp_next;
    }

    return 0;
}

/* 
 * Allocate a range of pages in RAM
 */
int alloc_pages_range(struct pcb_t *caller, int req_pgnum, struct framephy_struct** frm_lst)
{
    int pgit, fpn;
    struct framephy_struct *head = NULL;
    struct framephy_struct *current = NULL;

    for (pgit = 0; pgit < req_pgnum; pgit++) {
        // Attempt to get a free frame
        if (MEMPHY_get_freefp(caller->mram, &fpn) == 0) {
            // Create new frame physical node
            struct framephy_struct *new_frame = malloc(sizeof(struct framephy_struct));
            new_frame->fpn = fpn;
            new_frame->fp_next = NULL;

            // Link frame to list
            if (head == NULL) {
                head = new_frame;
                current = new_frame;
            } else {
                current->fp_next = new_frame;
                current = new_frame;
            }
        } else {
            // Not enough frames available
            return -3000; 
        }
    }

    *frm_lst = head;
    return 0;
}

/* 
 * Map virtual memory to RAM
 */
int vm_map_ram(struct pcb_t *caller, int astart, int aend, 
               int mapstart, int incpgnum, struct vm_rg_struct *ret_rg)
{
    struct framephy_struct *frm_lst = NULL;
    int ret_alloc;

    // Allocate pages
    ret_alloc = alloc_pages_range(caller, incpgnum, &frm_lst);

    if (ret_alloc < 0 && ret_alloc != -3000)
        return -1;

    // Out of memory
    if (ret_alloc == -3000) {
#ifdef MMDBG
        printf("OOM: vm_map_ram out of memory \n");
#endif
        return -1;
    }

    // Map page range
    vmap_page_range(caller, mapstart, incpgnum, frm_lst, ret_rg);

    return 0;
}

/* 
 * Copy page content between memory locations
 */
int __swap_cp_page(struct memphy_struct *mpsrc, int srcfpn,
                   struct memphy_struct *mpdst, int dstfpn) 
{
    int cellidx;
    int addrsrc, addrdst;
    BYTE data;

    for (cellidx = 0; cellidx < PAGING_PAGESZ; cellidx++) {
        addrsrc = srcfpn * PAGING_PAGESZ + cellidx;
        addrdst = dstfpn * PAGING_PAGESZ + cellidx;

        MEMPHY_read(mpsrc, addrsrc, &data);
        MEMPHY_write(mpdst, addrdst, data);
    }

    return 0;
}

/* 
 * Initialize Memory Management Structure
 */
int init_mm(struct mm_struct *mm, struct pcb_t *caller)
{
    // Allocate Virtual Memory Areas
    struct vm_area_struct *vma0 = malloc(sizeof(struct vm_area_struct));
    struct vm_area_struct *vma1 = malloc(sizeof(struct vm_area_struct));

    // Allocate Page Global Directory
    mm->pgd = malloc(PAGING_MAX_PGN * sizeof(uint32_t));

    // Initialize DATA Segment VMA
    vma0->vm_id = 0;
    vma0->vm_start = 0;
    vma0->vm_end = 0;
    vma0->sbrk = vma0->vm_start;
    struct vm_rg_struct *data_rg = init_vm_rg(vma0->vm_start, vma0->vm_end, 0);
    enlist_vm_rg_node(&vma0->vm_freerg_list, data_rg);

    // Initialize HEAP Segment VMA
    vma1->vm_id = 1;
    vma1->vm_start = vma0->vm_end;
    vma1->vm_end = vma1->vm_start;
    vma1->sbrk = vma1->vm_start;
    struct vm_rg_struct *heap_rg = init_vm_rg(vma1->vm_start, vma1->vm_end, 1);
    enlist_vm_rg_node(&vma1->vm_freerg_list, heap_rg);

    // Link VMAs
    vma0->vm_next = vma1;
    vma1->vm_next = NULL;

    // Set VMA owners
    vma0->vm_mm = mm;
    vma1->vm_mm = mm;

    // Set memory management map
    mm->mmap = vma0;

    return 0;
}

/* 
 * Initialize VM Region
 */
struct vm_rg_struct* init_vm_rg(int rg_start, int rg_end, int vmaid)
{
    struct vm_rg_struct *rgnode = malloc(sizeof(struct vm_rg_struct));

    rgnode->rg_start = rg_start;
    rgnode->rg_end = rg_end;
    rgnode->vmaid = vmaid;
    rgnode->rg_next = NULL;

    return rgnode;
}

/* 
 * Enlist VM Region Node
 */
int enlist_vm_rg_node(struct vm_rg_struct **rglist, struct vm_rg_struct* rgnode)
{
    rgnode->rg_next = *rglist;
    *rglist = rgnode;

    return 0;
}

/* 
 * Enlist Page Number Node
 */
int enlist_pgn_node(struct pgn_t **plist, int pgn)
{
    struct pgn_t* pnode = malloc(sizeof(struct pgn_t));

    pnode->pgn = pgn;
    pnode->pg_next = *plist;
    *plist = pnode;

    return 0;
}

// Diagnostic print functions remain the same as in the original implementation

int print_list_fp(struct framephy_struct *ifp)
{
   struct framephy_struct *fp = ifp;
 
   printf("print_list_fp: ");
   if (fp == NULL) {printf("NULL list\n"); return -1;}
   printf("\n");
   while (fp != NULL )
   {
       printf("fp[%d]\n",fp->fpn);
       fp = fp->fp_next;
   }
   printf("\n");
   return 0;
}

int print_list_rg(struct vm_rg_struct *irg)
{
   struct vm_rg_struct *rg = irg;
 
   printf("print_list_rg: ");
   if (rg == NULL) {printf("NULL list\n"); return -1;}
   printf("\n");
   while (rg != NULL)
   {
       printf("rg[%ld->%ld<at>vma=%d]\n",rg->rg_start, rg->rg_end, rg->vmaid);
       rg = rg->rg_next;
   }
   printf("\n");
   return 0;
}

int print_list_vma(struct vm_area_struct *ivma)
{
   struct vm_area_struct *vma = ivma;
 
   printf("print_list_vma: ");
   if (vma == NULL) {printf("NULL list\n"); return -1;}
   printf("\n");
   while (vma != NULL )
   {
       printf("va[%ld->%ld]\n",vma->vm_start, vma->vm_end);
       vma = vma->vm_next;
   }
   printf("\n");
   return 0;
}

int print_list_pgn(struct pgn_t *ip)
{
   printf("print_list_pgn: ");
   if (ip == NULL) {printf("NULL list\n"); return -1;}
   printf("\n");
   while (ip != NULL )
   {
       printf("va[%d]-\n",ip->pgn);
       ip = ip->pg_next;
   }
   printf("n");
   return 0;
}

int print_pgtbl(struct pcb_t *caller, uint32_t start, uint32_t end)
{
  int pgn_start,pgn_end;
  int pgit;

  if(end == -1){
    pgn_start = 0;
    struct vm_area_struct *cur_vma = get_vma_by_num(caller->mm, 0);
    end = cur_vma->vm_end;
  }
  pgn_start = PAGING_PGN(start);
  pgn_end = PAGING_PGN(end);

  printf("print_pgtbl: %d - %d", start, end);
  if (caller == NULL) {printf("NULL caller\n"); return -1;}
    printf("\n");


  for(pgit = pgn_start; pgit < pgn_end; pgit++)
  {
     printf("%08ld: %08x\n", pgit * sizeof(uint32_t), caller->mm->pgd[pgit]);
  }

  return 0;
}

//#endif
