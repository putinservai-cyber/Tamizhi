# Memory Management & GC Invariants

Tamizhi uses a **conservative mark–sweep garbage collector** implemented in
`src/runtime/tart.c`. This document records the invariants the runtime relies
on so they are not accidentally broken by future changes.

## Object model

- Every GC-managed object is allocated by `rt_alloc()` and begins with a
  `TaGcHead` header. The user-visible pointer is `TaGcHead* + TA_GC_HDR`
  (i.e. the bytes right after the header).
- Object kinds (`TaGcHead.type`):
  - `0` — plain allocation (no outgoing references to scan)
  - `1` — `TaRtStr` (string)
  - `2` — `TaRtList` (list; `cells[]` contains raw values / pointers)
  - `3` — `TaRtDict` (dict; `state/keys/vals` are child allocations)
- `gc_in_use` tracks total live bytes; `gc_since` tracks bytes allocated since
  the last collection; `gc_threshold` (default 8 MiB) triggers a collection.

## Conservative root scanning

The GC scans memory for candidate pointers ("conservative"): any word that
happens to equal the address of a live object is treated as a root.

**Invariants:**

1. **Stack bounds are correct.** `gc_init_stack_bounds()` records the *top* of
   the stack. On Linux it parses `/proc/self/maps` for the `[stack]` region;
   on macOS/BSD it uses `pthread_get_stackaddr_np`. The scan window is
   `[stack_bottom, current_stack_pointer]` (stack grows downward).
2. **CalF-saved registers are roots.** `setjmp(gc_regs)` captures
   callee-saved registers so pointers held only in registers survive. The
   register buffer must be refreshed (`setjmp`) immediately before each
   collection, because a root may live only in a register at that moment.
3. **Allocations are linked into `gc_objects` BEFORE the new object is used.**
   `rt_alloc()` deliberately runs a collection *before* prepending the new
   block, so a collection triggered by the very allocation that is happening
   cannot free the not-yet-rooted object.

## Marking algorithm

- `gc_table_build()` constructs an **O(1)** pointer→object hash table once per
  collection. Previously the GC did a linear scan of `gc_objects` for every
  candidate word, which made collection O(objects²) once the heap held
  hundreds of thousands of live objects (this caused `gc_stress.ta` to hang).
- `gc_trace_object()` pushes referenced child objects onto a **mark worklist**.
  `gc_collect_inner()` drains the worklist; each object is traced exactly once.
  This avoids both recursion (C-stack overflow on deep graphs) and the old
  fixpoint re-scan.
- Total collection cost is **O(live objects + live bytes)**.

## Sweep & table maintenance

- The sweep phase frees every unmarked object and unlinks it from `gc_objects`.
- The hash table (`gc_table`) and worklist (`gc_wl`) are heap-allocated
  scratch buffers; they are `free()`d at the end of every `gc_collect_inner()`
  so no memory is leaked across collections.

## Known limitations (future work)

- Only the **main thread's** stack is scanned. A multithreaded Tamizhi would
  need per-thread stack bounds registered with the GC.
- Conservative scanning can **over-estimate** liveness (a stray integer that
  looks like a pointer keeps an object alive). This is safe but not optimal.
- The GC is leak-tolerant by design: objects that are no longer reachable are
  reclaimed, but a program that keeps growing roots will keep allocating.
