import sys
from setuptools import setup, Extension

if sys.version_info[:2] < (3, 4):
    # We need the back-port of enum
    requirements = ['enum34']
else:
    requirements = []

with open('README.txt') as file:
    long_description = file.read()

setup(
    name="decimalfp",
    version="0.9.12",
    author="Michael Amrhein",
    author_email="michael@adrhinum.de",
    url="https://pypi.python.org/pypi/decimalfp",
    description="Decimal fixed-point arithmetic",
    long_description=long_description,
    package_dir = {'': 'src'},
    packages=['decimalfp'],
    ext_modules=[Extension('decimalfp._cdecimalfp',
                           ['src/decimalfp/_cdecimalfp.c'])],
    install_requires=requirements,
    license='BSD',
    keywords='fixed-point decimal number datatype',
    platforms='all',
    classifiers=[
        "Development Status :: 4 - Beta",
        "Intended Audience :: Developers",
        "License :: OSI Approved :: BSD License",
        "Operating System :: OS Independent",
        "Programming Language :: Python",
        "Programming Language :: Python :: 2.7",
        "Programming Language :: Python :: 3",
        "Programming Language :: Python :: 3.5",
        "Programming Language :: Python :: 3.6",
        "Programming Language :: Python :: Implementation :: CPython",
        "Programming Language :: Python :: Implementation :: PyPy",
        "Topic :: Software Development",
        "Topic :: Software Development :: Libraries",
        "Topic :: Software Development :: Libraries :: Python Modules"
    ]
)
