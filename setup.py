from setuptools import Extension, setup


class get_pybind_include:
    """Helper class to determine the pybind11 include path

    The purpose of this class is to postpone importing pybind11
    until it is actually installed, so that the ``get_include()``
    method can be invoked. """

    def __str__(self):
        import pybind11

        return pybind11.get_include()


ext_modules = [
    Extension(
        "afl37",
        ["afl37.cpp"],
        include_dirs=[get_pybind_include()],
        language="c++",
        extra_compile_args=["-std=c++14", "-O3"],
    ),
]


setup(
    name="python-afl37",
    version="0.1",
    description="",
    long_description="",
    author="Alexey Preobrazhenskiy",
    author_email="dizzy57@gmail.com",
    url="https://github.com/dizzy57/python-afl37",
    scripts=["py-afl37-fuzz"],
    ext_modules=ext_modules,
    python_requires=">=3.7",
    setup_requires=["pybind11>=2.5.0"],
)
