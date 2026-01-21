# coding=utf-8
"""Setup package 'decimalfp'."""

import os
import subprocess
import sysconfig

from setuptools import Extension, setup


def have_gcc() -> bool:
    """Return true if gcc is available, false otherwise."""
    try:
        subprocess.run(["gcc", "--version"])
    except OSError:
        return False
    else:
        return True


if have_gcc():
    LIBFPDEC_PATH = "src/decimalfp/libfpdec"
    LIBFPDEC_SRC_FILES = sorted(
        f"{LIBFPDEC_PATH}/{fn}"
        for fn in os.listdir(path=LIBFPDEC_PATH)
        if fn.endswith((".c",))
    )

    DEBUG = int(os.getenv("DEBUG", 0))
    cflags = sysconfig.get_config_var("CFLAGS")
    if cflags:
        extra_compile_args = cflags.split() + ["-Wall", "-Wextra"]
    else:
        extra_compile_args = ["-Wall", "-Wextra"]
    if DEBUG:
        extra_compile_args += ["-g3", "-O0", f"-DDEBUG={DEBUG}", "-UNDEBUG"]
    else:
        extra_compile_args += ["-DNDEBUG", "-O3"]

    ext_modules = [
        Extension(
            "decimalfp._cdecimalfp",
            ["src/decimalfp/_cdecimalfp.c"] + LIBFPDEC_SRC_FILES,
            include_dirs=["src/decimalfp", LIBFPDEC_PATH],
            extra_compile_args=extra_compile_args,
            # extra_link_args="",
            language="c",
        ),
    ]
    package_data = {"decimalfp": ["py.typed", "_cdecimalfp.pyi"]}
else:
    ext_modules = []
    package_data = {}

setup(
    name="decimalfp",
    package_dir={"": "src"},
    packages=["decimalfp"],
    package_data=package_data,
    ext_modules=ext_modules,
    python_requires=">=3.11",
    license="BSD-2-Clause",
    keywords="fixed-point decimal number datatype",
    platforms="all",
    classifiers=[
        "Development Status :: 5 - Production/Stable",
        "Intended Audience :: Developers",
        "Operating System :: OS Independent",
        "Programming Language :: Python",
        "Programming Language :: Python :: 3",
        "Programming Language :: Python :: Implementation :: CPython",
        "Programming Language :: Python :: Implementation :: PyPy",
        "Topic :: Software Development",
        "Topic :: Software Development :: Libraries",
        "Topic :: Software Development :: Libraries :: Python Modules",
    ],
    zip_safe=False,
)
