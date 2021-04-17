# coding=utf-8
"""Setup package 'decimalfp'."""

import os
import sysconfig

from setuptools import Extension, setup

LIBFPDEC_PATH = 'src/decimalfp/libfpdec'
LIBFPDEC_SRC_FILES = sorted(f"{LIBFPDEC_PATH}/{fn}"
                            for fn in os.listdir(path=LIBFPDEC_PATH)
                            if fn.endswith(('.c',)))

DEBUG = int(os.getenv("DEBUG", 0))
extra_compile_args = sysconfig.get_config_var('CFLAGS').split()
extra_compile_args += ["-Wall", "-Wextra"]
if DEBUG:
    extra_compile_args += ["-g3", "-O0", f"-DDEBUG={DEBUG}", "-UNDEBUG"]
else:
    extra_compile_args += ["-DNDEBUG", "-O3"]

ext_modules = [
    Extension(
        'decimalfp._cdecimalfp',
        ['src/decimalfp/_cdecimalfp.c'] + LIBFPDEC_SRC_FILES,
        include_dirs=['src/decimalfp', LIBFPDEC_PATH],
        extra_compile_args=extra_compile_args,
        # extra_link_args="",
        language='c',
        ),
    ]

with open('README.md') as file:
    long_description = file.read()

setup(
    name="decimalfp",
    version="0.11.4",
    author="Michael Amrhein",
    author_email="michael@adrhinum.de",
    url="https://github.com/mamrhein/decimalfp",
    description="Decimal numbers with fixed-point arithmetic",
    long_description=long_description,
    long_description_content_type="text/markdown",
    package_dir={'': 'src'},
    packages=['decimalfp'],
    ext_modules=ext_modules,
    python_requires=">=3.6",
    license='BSD',
    keywords='fixed-point decimal number datatype',
    platforms='all',
    classifiers=[
        "Development Status :: 5 - Production/Stable",
        "Intended Audience :: Developers",
        "License :: OSI Approved :: BSD License",
        "Operating System :: OS Independent",
        "Programming Language :: Python",
        "Programming Language :: Python :: 3",
        "Programming Language :: Python :: 3.6",
        "Programming Language :: Python :: 3.7",
        "Programming Language :: Python :: 3.8",
        "Programming Language :: Python :: 3.9",
        "Programming Language :: Python :: Implementation :: CPython",
        "Programming Language :: Python :: Implementation :: PyPy",
        "Topic :: Software Development",
        "Topic :: Software Development :: Libraries",
        "Topic :: Software Development :: Libraries :: Python Modules"
        ],
    zip_safe=False,
    )
