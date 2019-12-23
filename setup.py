from setuptools import setup, Extension

with open('README.md') as file:
    long_description = file.read()

setup(
    name="decimalfp",
    version="0.10.1",
    author="Michael Amrhein",
    author_email="michael@adrhinum.de",
    url="https://github.com/mamrhein/decimalfp",
    description="Decimal fixed-point arithmetic",
    long_description=long_description,
    long_description_content_type="text/markdown",
    package_dir = {'': 'src'},
    packages=['decimalfp'],
    ext_modules=[Extension('decimalfp._cdecimalfp',
                           ['src/decimalfp/_cdecimalfp.c'])],
    python_requires=">=3.6",
    # install_requires=requirements,
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
        "Programming Language :: Python :: Implementation :: CPython",
        "Programming Language :: Python :: Implementation :: PyPy",
        "Topic :: Software Development",
        "Topic :: Software Development :: Libraries",
        "Topic :: Software Development :: Libraries :: Python Modules"
    ]
)
