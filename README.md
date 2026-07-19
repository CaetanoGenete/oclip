# oclip

Simple clipboard cli utility, using OSC52 escape sequences.

If available, use [xclip](https://github.com/astrand/xclip), or
[wl-clipboard](https://github.com/bugaevc/wl-clipboard) instead.

## Installation

> [!WARNING]
>
> Windows is **NOT** supported (for now).

### cmake

[cmake](https://cmake.org/) is the preferred tool for installs. If available,
simply run:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=release
cmake --build build --config release
cmake --install build --config release

```

### custom

`oclip` consists of a single c file: [./oclip.c](oclip.c), and no additional
dependencies (besides `libc`).

## Usage examples

Write stdin to the clipboad:

```sh
printf "string ro copy to clipboard" | oclip
```

Write `myfile.txt` to the clipboard:

```sh
oclip myfile.txt
```

Output clipboard contents to the console:

```sh
oclip -o
```

Write clipboard contents to `output.txt`:


```sh
oclip -o output.txt
```
