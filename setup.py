# coding=utf-8
"""Setup package 'decimalfp'."""

import os
from setuptools import setup, Extension


LIBFPDEC_PATH = 'src/decimalfp/libfpdec'
LIBFPDEC_SRC_FILES = sorted(f"{LIBFPDEC_PATH}/{fn}"
                            for fn in os.listdir(path=LIBFPDEC_PATH)
                            if fn.endswith(('.c', 'cpp')))


class PybindIncludePath:
    """Helper for determining the pybind11 include path."""

    def __str__(self) -> str:
        import pybind11
        return pybind11.get_include()


ext_modules = [
    Extension(
        'decimalfp._cdecimalfp',
        ['src/decimalfp/_cdecimalfp.cpp'] + LIBFPDEC_SRC_FILES,
        include_dirs=[LIBFPDEC_PATH, PybindIncludePath()],
        # extra_link_args="",
        language='c++',
    ),
]


with open('README.md') as file:
    long_description = file.read()

setup(
    name="decimalfp",
    version="0.11.0a1",
    author="Michael Amrhein",
    author_email="michael@adrhinum.de",
    url="https://github.com/mamrhein/decimalfp",
    description="Decimal fixed-point arithmetic",
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
        "Programming Language :: Python :: Implementation :: CPython",
        "Programming Language :: Python :: Implementation :: PyPy",
        "Topic :: Software Development",
        "Topic :: Software Development :: Libraries",
        "Topic :: Software Development :: Libraries :: Python Modules"
    ],
    zip_safe=False,
)
