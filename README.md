GDNative FFmpeg Screen Recorder
===============================

This GDNative program allows you to invoke FFmpeg Libraries (namely `libavcodec`,
`libavformat` and `libswscale`) to generate a non-realtime video of the output
of a Godot (3.x) application.

## Motivation

*See [this article][article] for a detailed description.*

This program was made because at the time of writing, no good and practical 
screen recording solutions for Godot (3.x) exist, at least for low range
computers. Non-Realtime video generation is a feature that, at this time, only
exists for Godot 4 Alpha. Existing projects in Godot 3 would require a lot of
effort for porting to Godot 4.

This project attempts to create a reasonable solution to this problem.


## Compiling

You will have to first download the [Godot C++ Bindings][godot-cpp]
for your target Godot version and then place the `godot-cpp` folder in the
project root folder.

Then you will have to install the required libraries: `libavcodec`, `libavformat` and `libswscale` and their development packages on your system. Their names may vary based on the package manager and systems you are using.

Then to compile, run:

```
scons platform=<platform name> target=<debug/release>

```

in this folder. 

## Usage

See the example project in `project/` for an example. You must first initialise
the encoder, then start the encoder. To pass a frame, you must call the
encoder every time in the process loop, and then finally call the encoder once
again to finish the recording at a desired time. This may be at the end of an
animation, or after recording a certain number of frames.

To run the application, it is first advisable to first set an output destination
by setting the `file_name`, then running the application without the editor by
navigating to  the project directory, then starting Godot from a terminal with
the `--fixed-fps <fps>` option, where `<fps>` is your application's set fps,
which is usually `60`.

The terminal output will show you the frame count and the current duration of
the recorded video.

## Bugs

The recorder has been tested for only performing one recording during the 
runtime of the application in a few standard resolutions (`640x360`, `1280x720`,
`1920x1080`), and then the exiting immediately after the recording finishes.
Other modes of usage have not been tested and thus may have bugs.

## Licensing

This project is licensed under the MIT/X11 license. See `LICENSE` for details.




[article]: https://visphort.net/articles/2
[godot-cpp]: https://github.com/godotengine/gdnative-demos/tree/master/cpp/simple
