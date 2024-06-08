### User Program Tests

**Arguments Handling:**
- args-none: pass
- args-single: pass
- args-multiple: pass
- args-many: pass
- args-dbl-space: pass

**System Call Handling:**
- sc-bad-sp: pass
- sc-bad-arg: pass
- sc-boundary: pass
- sc-boundary-2: pass

**System Termination:**
- halt: pass
- exit: pass

**File Operations:**
- create-normal: pass
- create-empty: pass
- create-null: pass
- create-bad-ptr: pass
- create-long: pass
- create-exists: pass
- create-bound: pass
- open-normal: pass
- open-missing: pass
- open-boundary: pass
- open-empty: pass
- open-null: pass
- open-bad-ptr: pass
- open-twice: pass
- close-normal: pass
- close-twice: pass
- close-stdin: pass
- close-stdout: pass
- close-bad-fd: pass
- read-normal: pass
- read-bad-ptr: pass
- read-boundary: pass
- read-zero: pass
- read-stdout: pass
- read-bad-fd: pass
- write-normal: pass
- write-bad-ptr: pass
- write-boundary: pass
- write-zero: pass
- write-stdin: pass
- write-bad-fd: pass

**Process Management:**
- exec-once: pass
- exec-arg: pass
- exec-multiple: pass
- exec-missing: pass
- exec-bad-ptr: pass
- wait-simple: pass
- wait-twice: pass
- wait-killed: pass
- wait-bad-pid: pass

**Multi-threading:**
- multi-recurse: pass
- multi-child-fd: pass

**Read-Only Execution:**
- rox-simple: pass
- rox-child: pass
- rox-multichild: pass

### File System Tests

**Large File Operations:**
- lg-create: pass
- lg-full: pass
- lg-random: pass
- lg-seq-block: pass
- lg-seq-random: pass

**Small File Operations:**
- sm-create: pass
- sm-full: pass
- sm-random: pass
- sm-seq-block: pass
- sm-seq-random: pass

**Synchronous Operations:**
- syn-read: pass
- syn-remove: pass
- syn-write: pass

### Virtual Memory Tests

**Page Table Management:**
- pt-grow-stack: fail
- pt-grow-pusha: fail
- pt-grow-bad: fail
- pt-big-stk-obj: fail
- pt-bad-addr: fail
- pt-bad-read: pass
- pt-write-code: fail
- pt-write-code2: pass
- pt-grow-stk-sc: fail

**Page Replacement:**
- page-linear: fail
- page-parallel: fail
- page-merge-seq: fail
- page-merge-par: fail
- page-merge-stk: fail
- page-merge-mm: fail
- page-shuffle: pass

**Memory Mapped Files:**
- mmap-read: fail
- mmap-close: fail
- mmap-unmap: fail
- mmap-overlap: fail
- mmap-twice: fail
- mmap-write: fail
- mmap-exit: fail
- mmap-shuffle: fail
- mmap-bad-fd: fail
- mmap-clean: fail
- mmap-inherit: fail
- mmap-misalign: fail
- mmap-null: fail
- mmap-over-code: fail
- mmap-over-data: fail
- mmap-over-stk: fail
- mmap-remove: fail
- mmap-zero: fail
