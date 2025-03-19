<!-- drm-test/README.md -->

# drm-test
> [!WARNING]
> this is currently a project just for learning
>
> i cannot guarantee that it will work on your machine,  
> but you can try editing the [build script](./makefile) to make it work,  
> especially **DRM_DIR**

# compilation
### requirements
- [GNU Make](https://www.gnu.org/software/make)
- [GNU C Compiler](https://www.gnu.org/software/gcc) (or your preferred compiler and edit **COMPILER** in [makefile](./makefile))
- [DRM library](https://dri.freedesktop.org/wiki/DRM)
- optional:
  - [Cppcheck](https://cppcheck.sourceforge.io)
  - [Valgrind](https://valgrind.org)
### commands
`make` for release build, run with `make run`  
`make dev` for debug build, run with `make run_dev`  

`make static_analysis` to use cppcheck on source  
`make memory_check` to use valgrind on debug build  

`make clean` to delete everything created by these commands

