# Compiling Fluidterm for Use With FluidNC

## Install Dependencies

On Windows (Cygwin), you need to have [Python 3](https://www.python.org/downloads/) installed and the
[pyserial](https://pyserial.readthedocs.io/en/latest/pyserial.html) module.

```bash
python3 -m pip install -q pyserial  # Only need to do this once
```

## Running Fluidterm on Linux/Mac/Windows

After the dependencies are installed, you can run the fluidterm.py script directly:

```bash
python3 fluidterm.py /dev/ttyUSB0 115200
```

where `/dev/ttyUSB0` is the serial port and `115200` is the baud rate. On Windows, the serial port would typically be 
`COM3` or similar.

## Compiling Fluidterm to an Executable

You have to do this on the host system on which you wish to run
the executable.

  python3 -m pip install pyinstaller
  python3 -m PyInstaller --onefile fluidterm.py

The output is in dist\fluidterm.exe (Windows) or dist\fluidterm
(Linux and MacOS).  On Linux and Mac, the executable may depend
on versions of libraries that might not present on your system.
On those platforms, it can sometimes be easier to run from the
source code, with:

  python3 -m pip install -q pyserial xmodem  # Only need to do this once
  python3 fluidterm.py

  
