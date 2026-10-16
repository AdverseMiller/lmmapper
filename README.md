# lmmapper

`lmmapper` is an x86-64 userspace mapper for relocatable Linux kernel ELF objects. It stages sections, resolves symbols, applies relocations, writes an executable allocation, and calls a named entry point without using the kernel's normal module loader.

## Layout

- `src/` and `include/` contain the ELF mapper, kernel-symbol and layout handling, and the live execution path.
- `src/rw_helper.c` adapts the example READ/WRITE device to the mapper's RW interface.
- `module/` builds the optional `helper.ko` example RW source. It provides READ, WRITE, and GET_INFO ioctls; it does not allocate memory, invoke functions, or map objects.
- `tools/kernel_layout/` builds a small compile-only object used to obtain layout information from the running kernel's headers. It is never loaded.

The helper is a stand-in for the *outcome* of a kernel vulnerability: an ability to read and write kernel virtual memory. A real exploit of an existing kernel CVE might provide such a primitive, but this project does not implement or reproduce a CVE, and not every vulnerability provides the required access. The example deliberately obtains RW through a root-only device so the mapper can be developed and tested independently of an exploit. The userspace mapper—not `helper.ko`—performs the subsequent allocation, image mapping, and execution. The helper's GET_INFO ioctl only identifies its test scratch region and transfer limit.

## How it works

1. An RW adapter supplies kernel virtual-memory `read` and `write` callbacks, a writable scratch region, and a maximum transfer size. The included adapter opens `/dev/helper`; another RW source can implement the same interface.
2. Userspace checks that its symbol-bearing `vmlinux` matches the running kernel's build ID, derives the runtime symbol slide, and reads layout constants from a compile-only object built against the running kernel headers.
3. Using the RW primitive, userspace temporarily prepares its scratch page and its own task's restart callback as a short-lived call bridge. It uses that bridge to call the kernel's executable-memory allocation and permission APIs; the helper offers no allocation or call ioctl.
4. Userspace lays out the input ELF object, resolves symbols, applies supported x86-64 relocations, uploads and verifies the image, seals executable pages, and invokes the selected entry point. It then restores the temporary bridge state. The mapped allocation remains resident unless explicitly released.

This is not a normal `.ko` load: the kernel module loader does not register the mapped object or manage its lifecycle. A log message from the mapped object's entry point is produced by that object, not by the RW helper.

## Build and run

Build against the headers for the running kernel. The default kernel build flags use Clang/LLVM; set `KERNEL_MAKE_FLAGS` if your kernel requires another toolchain.

```sh
make
make -C module LLVM=1 W=1
make live-layout
```

```sh
insmod module/helper.ko
./lmmapper object.o entry_symbol 0 0
rmmod helper
```

The two numeric arguments are passed to the entry point. `--release` frees the mapped allocation after the entry returns; use it only if the entry retained no callbacks or other references. Without it, the allocation remains resident. A cleanup routine must run before freeing any image that registered kernel objects.

## Replace the RW source

Implement the `read`, `write`, and optional `close` callbacks in [`include/lmm_rw.h`](include/lmm_rw.h). Bind them with `lmm_rw_bind()`, supplying a writable scratch region of at least 64 bytes within one 4 KiB page and the primitive's maximum transfer size. Pass the resulting `struct lmm_rw` to `lmm_live_open_with_rw()`; the live backend takes ownership of it and performs allocation, permission changes, and entry invocation itself. The CLI currently opens the example adapter through `lmm_rw_open_helper()`; another adapter can replace that call without modifying the mapper or the example kernel module.

The RW source must address kernel virtual memory and support writes to writable kernel data, including page-table entries. It need not offer allocation or a call ioctl. The scratch page is temporarily made executable and restored during normal teardown; a failed restoration requires inspection before the RW source is unloaded.

## Compatibility and limits

The live path requires x86-64, a symbol-bearing `vmlinux` matching the running build ID, readable `/proc/kallsyms` addresses, and a kernel exposing the expected `execmem_alloc_rw` / `execmem_restore_rox` / `execmem_free` API. `make live-layout` checks the relevant function signatures against the current kernel headers. Set `LMM_VMLINUX` or `LMM_KERNEL_LAYOUT` to override the default artifact paths.

This is not a general `.ko` loader. Standard module metadata is rejected, and normal module registration, parameter processing, special-section handling, and arbitrary module lifecycles are not implemented. The supported input is a suitable ELF64/x86-64 relocatable object with a named function entry point. Verify an object's cleanup and lifetime before using `--release`.
