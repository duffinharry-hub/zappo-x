"""Make an ARM64 build folder ("sysroot") that matches Knulli on the RG40XX H,
so programs and libraries built here run on the twin and on the handheld.

  headers  - glibc 2.40 for aarch64 (from Zig's per-target copies) and the
             GCC 13 C++ headers (Knulli's libstdc++ is GCC 13's)
  libraries - Knulli's own .so files, linked to, never copied or changed
  start-up files - small crt files written here (Knulli ships none)

usage: mksysroot.py KNULLI_ROOTFS ZIG_LIBC_DIR OUT_SYSROOT CLANG COMPILER_RT_BUILTINS
(the last: llvm-project's compiler-rt/lib/builtins, for the ARM64 atomic
helpers clang calls - lse.S - which GCC's libgcc.a would normally provide)"""
import os
import shutil
import subprocess
import sys

CRT1 = r"""
    .text
    .globl _start
    .type _start, %function
_start:
    mov x29, #0
    mov x30, #0
    mov x5, x0                  // rtld_fini from the dynamic loader
    ldr x1, [sp]                // argc
    add x2, sp, #8              // argv
    mov x6, sp                  // stack end
    adrp x0, :got:main
    ldr x0, [x0, #:got_lo12:main]
    mov x3, #0                  // init and fini: glibc 2.34+ runs them itself
    mov x4, #0
    bl __libc_start_main
    bl abort
    .section .note.GNU-stack,"",%progbits
"""

CRTBEGIN = r"""
    .section .data.rel.ro.local,"aw"
    .align 3
    .globl __dso_handle
    .hidden __dso_handle
    .type __dso_handle, %object
__dso_handle:
    .quad DSO
    .section .note.GNU-stack,"",%progbits
"""

NONSHARED = r"""
extern void *__dso_handle __attribute__((visibility("hidden")));
extern int __cxa_atexit(void (*)(void *), void *, void *);
extern int __cxa_at_quick_exit(void (*)(void *), void *);
extern int __register_atfork(void (*)(void), void (*)(void), void (*)(void), void *);
extern void __stack_chk_fail(void) __attribute__((noreturn));
__attribute__((visibility("hidden"))) int atexit(void (*f)(void))
{ return __cxa_atexit((void (*)(void *))f, 0, __dso_handle); }
__attribute__((visibility("hidden"))) int at_quick_exit(void (*f)(void))
{ return __cxa_at_quick_exit((void (*)(void *))f, __dso_handle); }
__attribute__((visibility("hidden"))) int pthread_atfork(void (*p)(void), void (*c)(void), void (*ch)(void))
{ return __register_atfork(p, c, ch, __dso_handle); }
__attribute__((visibility("hidden"), noreturn)) void __stack_chk_fail_local(void)
{ __stack_chk_fail(); }
"""


def sh(*a):
    subprocess.run(a, check=True)


def main(rootfs, zlibc, out, clang, rt):
    tgt = ["--target=aarch64-linux-gnu"]
    inc = os.path.join(out, "usr/include")
    os.makedirs(inc, exist_ok=True)
    # glibc headers: the more specific folder wins, so copy general -> specific
    for d in ["any-linux-any", "aarch64-linux-any", "generic-glibc", "aarch64-linux-gnu"]:
        shutil.copytree(os.path.join(zlibc, "include", d), inc, dirs_exist_ok=True)
    # Zig's headers are told the glibc version on the command line; fix it in place (2.40)
    feat = os.path.join(inc, "features.h")
    s = open(feat).read()
    if "TWIN_GLIBC_MINOR" not in s:
        s = s.replace("#define\t__GLIBC__\t2\n", "#define\t__GLIBC__\t2\n#ifndef __GLIBC_MINOR__ /* TWIN_GLIBC_MINOR */\n#define __GLIBC_MINOR__ 40\n#endif\n", 1)
        assert "TWIN_GLIBC_MINOR" in s, "features.h layout changed"
        open(feat, "w").write(s)

    # C++ headers: GCC 13, same as Knulli's libstdc++.so.6.0.32
    shutil.copytree("/usr/include/c++/13", os.path.join(inc, "c++/13"), dirs_exist_ok=True)
    cfg = os.path.join(inc, "aarch64-linux-gnu/c++/13")
    shutil.copytree("/usr/include/x86_64-linux-gnu/c++/13", cfg, dirs_exist_ok=True)
    c = os.path.join(cfg, "bits/c++config.h")
    s = open(c).read()
    for m in ["_GLIBCXX_USE_FLOAT128", "_GLIBCXX_HAVE_FLOAT128_MATH"]:
        s = s.replace("#define %s 1" % m, "/* %s: not on aarch64 */" % m)
    open(c, "w").write(s)
    open(os.path.join(cfg, "bits/opt_random.h"), "w").write(
        "#ifndef _BITS_OPT_RANDOM_H\n#define _BITS_OPT_RANDOM_H 1\n#pragma GCC system_header\n#endif\n")

    # Knulli's libraries, by link
    for d in ["lib", "usr/lib"]:
        os.makedirs(os.path.join(out, d), exist_ok=True)
        for n in os.listdir(os.path.join(rootfs, d)):
            src = os.path.join(rootfs, d, n)
            dst = os.path.join(out, d, n)
            if ".so" in n and not os.path.isdir(src) and not os.path.lexists(dst):
                os.symlink(src, dst)
    lib = os.path.join(out, "usr/lib")

    def put(name, text):
        p = os.path.join(lib, name)
        if os.path.lexists(p):
            os.remove(p)
        open(p, "w").write(text)

    put("libc.so", "OUTPUT_FORMAT(elf64-littleaarch64)\nGROUP ( /lib/libc.so.6 /usr/lib/libc_nonshared.a AS_NEEDED ( /lib/ld-linux-aarch64.so.1 ) )\n")
    put("libm.so", "GROUP ( /lib/libm.so.6 AS_NEEDED ( /lib/libmvec.so.1 ) )\n")
    for n in ["libpthread", "libdl", "librt", "libutil", "libresolv"]:
        sh("llvm-ar", "rc", os.path.join(lib, n + ".a"))

    # start-up files and the small static part of glibc
    tmp = os.path.join(out, ".build")
    os.makedirs(tmp, exist_ok=True)

    def asm(name, text, defs=()):
        src = os.path.join(tmp, name + ".S")
        open(src, "w").write(text)
        sh(clang, *tgt, "-c", *defs, src, "-o", os.path.join(tmp, name + ".o"))
        return os.path.join(tmp, name + ".o")

    shutil.copy(asm("crt1", CRT1), os.path.join(lib, "crt1.o"))
    shutil.copy(asm("crt1", CRT1), os.path.join(lib, "Scrt1.o"))
    empty = asm("empty", ".section .note.GNU-stack,\"\",%progbits\n")
    for n in ["crti.o", "crtn.o"]:
        shutil.copy(empty, os.path.join(lib, n))
    src = os.path.join(tmp, "nonshared.c")
    open(src, "w").write(NONSHARED)
    sh(clang, *tgt, "-O2", "-fPIC", "-c", src, "-o", os.path.join(tmp, "nonshared.o"))
    p = os.path.join(lib, "libc_nonshared.a")
    if os.path.exists(p):
        os.remove(p)
    sh("llvm-ar", "rcs", p, os.path.join(tmp, "nonshared.o"))

    # a GCC 13 "installation" so clang finds crtbegin/crtend and libgcc
    gcc = os.path.join(lib, "gcc/aarch64-linux-gnu/13")
    os.makedirs(gcc, exist_ok=True)
    shutil.copy(asm("crtbegin", CRTBEGIN, ["-DDSO=0"]), os.path.join(gcc, "crtbegin.o"))
    shutil.copy(asm("crtbegin", CRTBEGIN, ["-DDSO=0"]), os.path.join(gcc, "crtbeginT.o"))
    shutil.copy(asm("crtbeginS", CRTBEGIN, ["-DDSO=__dso_handle"]), os.path.join(gcc, "crtbeginS.o"))
    for n in ["crtend.o", "crtendS.o"]:
        shutil.copy(empty, os.path.join(gcc, n))
    sh("llvm-ar", "rc", os.path.join(gcc, "libgcc_eh.a"))
    # libgcc.a: the outline atomics (__aarch64_cas4_acq_rel ...); the A53 has
    # no LSE instructions, so the helpers always take the LL/SC path
    objs = []
    for op in ["cas", "swp", "ldadd", "ldclr", "ldeor", "ldset"]:
        for size in [1, 2, 4, 8, 16]:
            if size == 16 and op != "cas":
                continue
            for model in [1, 2, 3, 4]:
                o = os.path.join(tmp, "%s%d_%d.o" % (op, size, model))
                sh(clang, *tgt, "-c", "-O2", "-I" + rt, "-DL_" + op, "-DSIZE=%d" % size, "-DMODEL=%d" % model,
                   os.path.join(rt, "aarch64/lse.S"), "-o", o)
                objs.append(o)
    have = os.path.join(tmp, "have_lse.c")
    open(have, "w").write('int __aarch64_have_lse_atomics __attribute__((visibility("hidden"))) = 0; /* Cortex-A53 */\n')
    sh(clang, *tgt, "-O2", "-fPIC", "-c", have, "-o", have[:-2] + ".o")
    objs.append(have[:-2] + ".o")
    p = os.path.join(gcc, "libgcc.a")
    if os.path.exists(p):
        os.remove(p)
    sh("llvm-ar", "rcs", p, *objs)
    open(os.path.join(gcc, "libgcc_s.so"), "w").write("GROUP ( /lib/libgcc_s.so.1 )\n")
    for n in ["libstdc++.so"]:
        p = os.path.join(gcc, n)
        if not os.path.lexists(p):
            os.symlink(os.path.join(rootfs, "usr/lib/libstdc++.so.6"), p)
    print("sysroot ready:", out)


if __name__ == "__main__":
    main(*sys.argv[1:6])
