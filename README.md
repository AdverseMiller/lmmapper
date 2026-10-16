# lmmapper

`lmmapper` is an x86-64 userspace mapper for relocatable Linux kernel ELF objects. It stages sections, resolves symbols, applies relocations, writes an executable allocation, and calls a named entry point. It does not use the kernel's normal module loader. This is experimental kernel code: use a disposable VM and expect unsupported objects to fail closed.

## Layout

- `src/` and `include/` contain the ELF mapper, kernel-symbol and layout handling, and the live execution path.
- `src/rw_lmmapper.c` adapts the example READ/WRITE device to the mapper's RW interface.
- `module/` is an optional, root-only example RW primitive. It provides READ, WRITE, and GET_INFO ioctls; it does not allocate memory, invoke functions, or map objects.
- `tools/kernel_layout/` builds a small compile-only object used to obtain layout information from the running kernel's headers. It is never loaded.

The separation follows the same principle as kdmapper's mapper/driver split: the userspace mapper owns image loading and execution, while the RW source only supplies kernel virtual-memory reads and writes.

## Build and run

Build against the headers for the running kernel. The default kernel build flags use Clang/LLVM; set `KERNEL_MAKE_FLAGS` if your kernel requires another toolchain.

```sh
make
make -C module LLVM=1 W=1
make live-layout
```

As root in a disposable VM, load the example helper, run a suitable relocatable object, then unload the helper:

```sh
insmod module/lmmapper.ko
./lmmapper object.o entry_symbol 0 0
rmmod lmmapper
```

The two numeric arguments are passed to the entry point. `--release` frees the mapped allocation after the entry returns; use it only if the entry retained no callbacks or other references. Without it, the allocation remains resident. A cleanup routine must run before freeing any image that registered kernel objects.

## Replace the RW source

Implement the `read`, `write`, and optional `close` callbacks in [`include/lmm_rw.h`](include/lmm_rw.h). Bind them with `lmm_rw_bind()`, supplying a writable scratch region of at least 64 bytes within one 4 KiB page and the primitive's maximum transfer size. Pass the resulting `struct lmm_rw` to `lmm_live_open_with_rw()`; the live backend takes ownership of it and performs allocation, permission changes, and entry invocation itself. The CLI currently opens the example adapter through `lmm_rw_open_lmmapper()`; another adapter can replace that call without modifying the mapper or the example kernel module.

The RW source must address kernel virtual memory and support writes to writable kernel data, including page-table entries. It need not offer allocation or a call ioctl. The scratch page is temporarily made executable and restored during normal teardown; a failed restoration requires inspection before the RW source is unloaded.

## Compatibility and limits

The live path requires x86-64, a symbol-bearing `vmlinux` matching the running build ID, readable `/proc/kallsyms` addresses, and a kernel exposing the expected `execmem_alloc_rw` / `execmem_restore_rox` / `execmem_free` API. `make live-layout` checks the relevant function signatures against the current kernel headers. Set `LMM_VMLINUX` or `LMM_KERNEL_LAYOUT` to override the default artifact paths.

This is not a general `.ko` loader. Standard module metadata is rejected, and normal module registration, parameter processing, special-section handling, and arbitrary module lifecycles are not implemented. The supported input is a suitable ELF64/x86-64 relocatable object with a named function entry point. Verify an object's cleanup and lifetime before using `--release`.
