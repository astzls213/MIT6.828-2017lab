/* See COPYRIGHT for copyright information. */

#include <inc/x86.h>
#include <inc/error.h>
#include <inc/string.h>
#include <inc/assert.h>

#include <kern/env.h>
#include <kern/pmap.h>
#include <kern/trap.h>
#include <kern/syscall.h>
#include <kern/console.h>
#include <kern/sched.h>

// Print a string to the system console.
// The string is exactly 'len' characters long.
// Destroys the environment on memory errors.
static void
sys_cputs(const char *s, size_t len)
{
	// Check that the user has permission to read memory [s, s+len).
	// Destroy the environment if not.

	// LAB 3: Your code here.
	pte_t *ptr = NULL;
	void *addr = (void *)s;
	void *end = (void *)ROUNDUP(s+len, PGSIZE);
	/*while (addr < end) {
		if(page_lookup(curenv->env_pgdir, addr, &ptr) == NULL || 
			!(*ptr & PTE_U)) {
			cprintf("Cannot access memory!\n");
			env_destroy(curenv);
			return;
		}
		addr += PGSIZE;
	}*/
	user_mem_assert(curenv, (void *)s, len, 0);	
	// Print the string supplied by the user.
	cprintf("%.*s", len, s);
}

// Read a character from the system console without blocking.
// Returns the character, or 0 if there is no input waiting.
static int
sys_cgetc(void)
{
	return cons_getc();
}

// Returns the current environment's envid.
static envid_t
sys_getenvid(void)
{
	return curenv->env_id;
}

// Destroy a given environment (possibly the currently running environment).
//
// Returns 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist,
//		or the caller doesn't have permission to change envid.
static int
sys_env_destroy(envid_t envid)
{
	int r;
	struct Env *e;

	if ((r = envid2env(envid, &e, 1)) < 0)
		return r;
	env_destroy(e);
	return 0;
}

// Deschedule current environment and pick a different one to run.
static void
sys_yield(void)
{
	sched_yield();
}

// Allocate a new environment.
// Returns envid of new environment, or < 0 on error.  Errors are:
//	-E_NO_FREE_ENV if no free environment is available.
//	-E_NO_MEM on memory exhaustion.
static envid_t
sys_exofork(void)
{
	// Create the new environment with env_alloc(), from kern/env.c.
	// It should be left as env_alloc created it, except that
	// status is set to ENV_NOT_RUNNABLE, and the register set is copied
	// from the current environment -- but tweaked so sys_exofork
	// will appear to return 0.

	// LAB 4: Your code here.
	struct Env *child = NULL;
	int rtv = env_alloc(&child, curenv->env_id);
	if (rtv < 0)
		return rtv;
	else {
		// set Not Runnable
		child->env_status = ENV_NOT_RUNNABLE;
		child->env_tf = curenv->env_tf;
		child->env_tf.tf_regs.reg_eax = 0;
		return child->env_id;
	}
	//panic("sys_exofork not implemented");
}

// Set envid's env_status to status, which must be ENV_RUNNABLE
// or ENV_NOT_RUNNABLE.
//
// Returns 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist,
//		or the caller doesn't have permission to change envid.
//	-E_INVAL if status is not a valid status for an environment.
static int
sys_env_set_status(envid_t envid, int status)
{
	// Hint: Use the 'envid2env' function from kern/env.c to translate an
	// envid to a struct Env.
	// You should set envid2env's third argument to 1, which will
	// check whether the current environment has permission to set
	// envid's status.

	// LAB 4: Your code here.
	struct Env *aEnv;
	int rtv = envid2env(envid, &aEnv, 1);
	if (rtv < 0)
		return rtv;
	else {
		if (status != ENV_RUNNABLE && status != ENV_NOT_RUNNABLE)
			return -E_INVAL;
		aEnv->env_status = status;
		return 0;
	}

	//panic("sys_env_set_status not implemented");
}

// Set envid's trap frame to 'tf'.
// tf is modified to make sure that user environments always run at code
// protection level 3 (CPL 3), interrupts enabled, and IOPL of 0.
//
// Returns 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist,
//		or the caller doesn't have permission to change envid.
static int
sys_env_set_trapframe(envid_t envid, struct Trapframe *tf)
{
	// LAB 5: Your code here.
	// Remember to check whether the user has supplied us with a good
	// address!
	int r;
	struct Env *aEnv;
	// 获取env
	if ((r = envid2env(envid, &aEnv, true)) < 0)
		return r;
	
	// 设置trapframe
	aEnv->env_tf = *tf;
	aEnv->env_tf.tf_eflags |= FL_IF;
	aEnv->env_tf.tf_cs |= 3;
	aEnv->env_tf.tf_eflags &= ~FL_IOPL_MASK;
	return 0;
}

// Set the page fault upcall for 'envid' by modifying the corresponding struct
// Env's 'env_pgfault_upcall' field.  When 'envid' causes a page fault, the
// kernel will push a fault record onto the exception stack, then branch to
// 'func'.
//
// Returns 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist,
//		or the caller doesn't have permission to change envid.
static int
sys_env_set_pgfault_upcall(envid_t envid, void *func)
{
	// LAB 4: Your code here.
	struct Env *aEnv;
	int rtv = envid2env(envid, &aEnv, 1);
	if (rtv < 0)
		return rtv;
	aEnv->env_pgfault_upcall = func;
	return 0;
	//panic("sys_env_set_pgfault_upcall not implemented");
}

// Allocate a page of memory and map it at 'va' with permission
// 'perm' in the address space of 'envid'.
// The page's contents are set to 0.
// If a page is already mapped at 'va', that page is unmapped as a
// side effect.
//
// perm -- PTE_U | PTE_P must be set, PTE_AVAIL | PTE_W may or may not be set,
//         but no other bits may be set.  See PTE_SYSCALL in inc/mmu.h.
//
// Return 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist,
//		or the caller doesn't have permission to change envid.
//	-E_INVAL if va >= UTOP, or va is not page-aligned.
//	-E_INVAL if perm is inappropriate (see above).
//	-E_NO_MEM if there's no memory to allocate the new page,
//		or to allocate any necessary page tables.
static int
sys_page_alloc(envid_t envid, void *va, int perm)
{
	// Hint: This function is a wrapper around page_alloc() and
	//   page_insert() from kern/pmap.c.
	//   Most of the new code you write should be to check the
	//   parameters for correctness.
	//   If page_insert() fails, remember to free the page you
	//   allocated!

	// LAB 4: Your code here.
	// 分配一页
	struct PageInfo *aPage = page_alloc(ALLOC_ZERO);
	if (!aPage)
		return -E_NO_MEM;
	// 参数正确性检验
	if ((uintptr_t) va & 0xFFF || (uintptr_t) va >= UTOP)
		return -E_INVAL;
	int must_set = PTE_U | PTE_P;
	int prohibit_set = 0x1F8;		// see mmu.h
	if ((perm & must_set) != must_set || perm & prohibit_set)
		return -E_INVAL;
	// 获取Env
	struct Env *aEnv;
	int rtv = envid2env(envid, &aEnv, 1);
	if (rtv < 0)
		return rtv;
	// 设置映射
	rtv = page_insert(aEnv->env_pgdir, aPage, va, perm);
	if (rtv < 0) 	// insert failed...
		page_free(aPage);
	return rtv;
	//panic("sys_page_alloc not implemented");
}

// Map the page of memory at 'srcva' in srcenvid's address space
// at 'dstva' in dstenvid's address space with permission 'perm'.
// Perm has the same restrictions as in sys_page_alloc, except
// that it also must not grant write access to a read-only
// page.
//
// Return 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if srcenvid and/or dstenvid doesn't currently exist,
//		or the caller doesn't have permission to change one of them.
//	-E_INVAL if srcva >= UTOP or srcva is not page-aligned,
//		or dstva >= UTOP or dstva is not page-aligned.
//	-E_INVAL is srcva is not mapped in srcenvid's address space.
//	-E_INVAL if perm is inappropriate (see sys_page_alloc).
//	-E_INVAL if (perm & PTE_W), but srcva is read-only in srcenvid's
//		address space.
//	-E_NO_MEM if there's no memory to allocate any necessary page tables.
static int
sys_page_map(envid_t srcenvid, void *srcva,
	     envid_t dstenvid, void *dstva, int perm)
{
	// Hint: This function is a wrapper around page_lookup() and
	//   page_insert() from kern/pmap.c.
	//   Again, most of the new code you write should be to check the
	//   parameters for correctness.
	//   Use the third argument to page_lookup() to
	//   check the current permissions on the page.

	// LAB 4: Your code here.
	struct Env *src, *dst;
	int r1 = envid2env(srcenvid, &src, 1);
	int r2 = envid2env(dstenvid, &dst, 1);
	if (r1 < 0 || r2 < 0)
		return -E_BAD_ENV;
	// 范围检查
	if ((uintptr_t) srcva >= UTOP || (uintptr_t) dstva >= UTOP)
		panic(">=UTOP\n");//return -E_INVAL;	
	// 对齐检查
	if ((uintptr_t) srcva & 0xFFF || (uintptr_t) dstva & 0xFFF)
        panic("not aligned\n");//return -E_INVAL;
	// 权限检查
    int must_set = PTE_U | PTE_P;
    int prohibit_set = 0x1F8;       // see mmu.h
    if ((perm & must_set) != must_set ||
        perm & prohibit_set)
        panic("perm failed!\n");//return -E_INVAL;
	// Get 原env的那个物理页
	pte_t *aPte;
	struct PageInfo *aPage = page_lookup(src->env_pgdir, srcva, &aPte);
	// srcva是否有效检查
	if (aPage == NULL)
		panic("aPage == NULL\n");//return -E_INVAL;
	// 权限检查
	if (perm & PTE_W && (*aPte & PTE_W) == 0)
		panic("map to writable for readonly page\n");//return -E_INVAL;
	//cprintf("before insert, page ref is %d\n",aPage->pp_ref);
	r1 = page_insert(dst->env_pgdir, aPage, dstva, perm);
	return r1;
	//panic("sys_page_map not implemented");
}

// Unmap the page of memory at 'va' in the address space of 'envid'.
// If no page is mapped, the function silently succeeds.
//
// Return 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist,
//		or the caller doesn't have permission to change envid.
//	-E_INVAL if va >= UTOP, or va is not page-aligned.
static int
sys_page_unmap(envid_t envid, void *va)
{
	// Hint: This function is a wrapper around page_remove().

	// LAB 4: Your code here.
	// 检查 va
	if ((uintptr_t) va >= UTOP || (uintptr_t) va & 0xFFF)
		return -E_INVAL;
	// get env
	struct Env *aEnv;
	int rtv = envid2env(envid, &aEnv, 1);
	if (rtv < 0)
		return rtv;
	// page remove
	page_remove(aEnv->env_pgdir, va);
	return 0;
	//panic("sys_page_unmap not implemented");
}

// Try to send 'value' to the target env 'envid'.
// If srcva < UTOP, then also send page currently mapped at 'srcva',
// so that receiver gets a duplicate mapping of the same page.
//
// The send fails with a return value of -E_IPC_NOT_RECV if the
// target is not blocked, waiting for an IPC.
//
// The send also can fail for the other reasons listed below.
//
// Otherwise, the send succeeds, and the target's ipc fields are
// updated as follows:
//    env_ipc_recving is set to 0 to block future sends;
//    env_ipc_from is set to the sending envid;
//    env_ipc_value is set to the 'value' parameter;
//    env_ipc_perm is set to 'perm' if a page was transferred, 0 otherwise.
// The target environment is marked runnable again, returning 0
// from the paused sys_ipc_recv system call.  (Hint: does the
// sys_ipc_recv function ever actually return?)
//
// If the sender wants to send a page but the receiver isn't asking for one,
// then no page mapping is transferred, but no error occurs.
// The ipc only happens when no errors occur.
//
// Returns 0 on success, < 0 on error.
// Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist.
//		(No need to check permissions.)
//	-E_IPC_NOT_RECV if envid is not currently blocked in sys_ipc_recv,
//		or another environment managed to send first.
//	-E_INVAL if srcva < UTOP but srcva is not page-aligned.
//	-E_INVAL if srcva < UTOP and perm is inappropriate
//		(see sys_page_alloc).
//	-E_INVAL if srcva < UTOP but srcva is not mapped in the caller's
//		address space.
//	-E_INVAL if (perm & PTE_W), but srcva is read-only in the
//		current environment's address space.
//	-E_NO_MEM if there's not enough memory to map srcva in envid's
//		address space.
static int
sys_ipc_try_send(envid_t envid, uint32_t value, void *srcva, unsigned perm)
{
	// LAB 4: Your code here.
	uintptr_t addr = (uintptr_t) srcva;
 	pte_t *aPte;
	struct PageInfo *aPage;
	int r;

	// 获取目标进程
	struct Env *dstEnv;
	if ((r = envid2env(envid, &dstEnv, 0)) < 0)
		return r;

 	// 目标进程正在等待接收信息吗？
 	if (dstEnv->env_ipc_recving)
	{
  		dstEnv->env_ipc_recving = 0;

  		// sender要共享页吗？receiver要共享页吗？
  		if (addr < UTOP && (uintptr_t) (dstEnv->env_ipc_dstva) < UTOP)
		{
			// Page-aligned flag
			int f1 = addr & 0xFFF;
			// Perm check flag: f2/f3
			int f2 = (perm & (PTE_U | PTE_P)) != (PTE_U | PTE_P) ;
			int f3 = perm & 0x1F8;
			// Caller non map on srcva flag
			int f4 = ((aPage = page_lookup(curenv->env_pgdir, srcva, &aPte)) == NULL);
			// map writable to read-only page
			int f5 = ((perm & PTE_W) && (*aPte & PTE_W) == 0);

   			if (f1 || f2 || f3 || f4 || f5)
    			return -E_INVAL;
   			else {
				r = page_insert(dstEnv->env_pgdir, aPage, dstEnv->env_ipc_dstva, perm);
				if (r < 0)
					return r;
				dstEnv->env_ipc_perm = perm;
			}
  		}
  		else
   			dstEnv->env_ipc_perm = 0;
		
		// word transfer
		dstEnv->env_ipc_value = value;
		dstEnv->env_ipc_from = sys_getenvid();
		// 唤醒接受进程
		dstEnv->env_status = ENV_RUNNABLE;
		return 0;
 	}
 	else
  		return -E_IPC_NOT_RECV;
}

// Block until a value is ready.  Record that you want to receive
// using the env_ipc_recving and env_ipc_dstva fields of struct Env,
// mark yourself not runnable, and then give up the CPU.
//
// If 'dstva' is < UTOP, then you are willing to receive a page of data.
// 'dstva' is the virtual address at which the sent page should be mapped.
//
// This function only returns on error, but the system call will eventually
// return 0 on success.
// Return < 0 on error.  Errors are:
//	-E_INVAL if dstva < UTOP but dstva is not page-aligned.
static int
sys_ipc_recv(void *dstva)
{
	// LAB 4: Your code here.
 	uintptr_t addr = (uintptr_t) dstva;
 	if (addr < UTOP) {
  		if ((addr & 0xFFF) == 0)
   			curenv->env_ipc_dstva = dstva;
  		else
   			return -E_INVAL;
 	}
 
	// 阻塞自己
	curenv->env_status = ENV_NOT_RUNNABLE;
	// 设置返回值
	curenv->env_tf.tf_regs.reg_eax = 0;
	// 设置想要接受信息标志
	curenv->env_ipc_recving = 1;
	// give up CPU
	sched_yield();
	return 0;
}

// Dispatches to the correct kernel function, passing the arguments.
int32_t
syscall(uint32_t syscallno, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5)
{
	// Call the function corresponding to the 'syscallno' parameter.
	// Return any appropriate return value.
	// LAB 3: Your code here.

	//panic("syscall not implemented");

	switch (syscallno) {
		case SYS_cputs:
			sys_cputs((const char *)a1,a2);
			return 0;
		case SYS_cgetc:
			return sys_cgetc();
		case SYS_getenvid:
			return sys_getenvid();
		case SYS_env_destroy:
			return sys_env_destroy((envid_t)a1);
		case SYS_yield:
			sys_yield();
			return 0;
		case SYS_exofork:
			return sys_exofork();
		case SYS_env_set_status:
			return sys_env_set_status((envid_t)a1, (int)a2);
		case SYS_env_set_pgfault_upcall:
			return sys_env_set_pgfault_upcall((envid_t)a1, (void *) a2);
		case SYS_page_alloc:
			return sys_page_alloc((envid_t)a1, (void *)a2, (int)a3);
		case SYS_page_map:
			return sys_page_map((envid_t)a1,
								(void *)a2,
								(envid_t)a3,
								(void *)a4,
								(int)a5);
		case SYS_page_unmap:
			return sys_page_unmap((envid_t)a1, (void *)a2);
		case SYS_ipc_try_send:
			return sys_ipc_try_send((envid_t) a1, (uint32_t) a2, (void *) a3, (unsigned) a4);
		case SYS_ipc_recv:
			return sys_ipc_recv((void *) a1);
		case SYS_env_set_trapframe:
			return sys_env_set_trapframe((envid_t) a1, (struct Trapframe *) a2);
		case NSYSCALLS:
			return 0;
		default:
			return -E_INVAL;
	}
}

