# Spin The Wheel

A desktop spin-the-wheel app built with C++17 and SFML 3.

## Runtime folder layout

The executable looks for fonts in its current folder. On Windows, keep the
finished application together in one folder:

```text
SpinTheWheel/
	SpinTheWheel.exe
	sfml-graphics-3.dll
	sfml-window-3.dll
	sfml-system-3.dll
	Poppins-Bold.ttf
	Poppins-Regular.ttf
```

Do not put the DLLs in `C:\Windows\System32`, and do not add them to the
repository. DLLs belong beside the executable that uses them. The three DLLs
must come from the same SFML 3.1.0 package and the same architecture as the
program, normally 64-bit MinGW on Windows.

The Poppins files are application assets rather than SFML dependencies. They
are copied beside the executable by the CMake build. If they are absent, the
program attempts to use Arial instead.

## Build with CMake on Windows

Install the SFML 3.1.0 development package and note its installation folder,
for example `C:\libs\SFML-3.1.0`. That folder should contain `include`, `lib`,
and `bin` directories.

From a Developer PowerShell in this repository:

```powershell
cmake -S . -B build -G "MinGW Makefiles" `
	-DCMAKE_CXX_COMPILER=C:/mingw64/bin/g++.exe `
	-DCMAKE_PREFIX_PATH=C:/libs/SFML-3.1.0 `
	-DSFML_RUNTIME_DIR=C:/libs/SFML-3.1.0/bin
cmake --build build --config Release
```

The post-build step copies the fonts and SFML DLLs into the executable
directory. If your SFML package uses a different folder layout, copy the three
DLLs manually from its `bin` directory beside `SpinTheWheel.exe`.

## Build with Code::Blocks

Use the SFML 3.1.0 MinGW package matching Code::Blocks' compiler:

1. Add `<SFML>\include` to compiler search directories.
2. Add `<SFML>\lib` to linker search directories.
3. Link `sfml-graphics`, `sfml-window`, and `sfml-system`.
4. Enable C++17.
5. Copy `sfml-graphics-3.dll`, `sfml-window-3.dll`, and `sfml-system-3.dll`
	 from `<SFML>\bin` beside the generated `.exe`.
6. Copy `Poppins-Bold.ttf` and `Poppins-Regular.ttf` beside the `.exe`.

The compiler, SFML package, and application must all use the same architecture.
Do not mix 32-bit and 64-bit binaries.

## Controls

Use `+ Add Contestant` to add a name, `x` to remove one, and `Clear All` to
empty the list. Click `SPIN THE WHEEL` or press Space to spin. Press Esc to
dismiss the winner dialog.
