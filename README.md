# rdopng
Rate-Distortion Optimized Lossy PNG Encoding Tool

rdopng is a command line tool which uses LZ match optimization, Lagrangian multiplier [rate distortion optimization (RDO)](https://en.wikipedia.org/wiki/Rate%E2%80%93distortion_optimization), a simple perceptual error tolerance model, and [Oklab](https://bottosson.github.io/posts/oklab/)-based colorspace error metrics to encode 24/32bpp PNG files which are 30-80% smaller relative to lodepng/libpng. The tool defaults to reasonably fast near-lossless settings which writes PNG's around 30-40% smaller than lossless PNG encoders.

Unlike [pngquant](https://pngquant.org/), rdopng does not use 256-color palettes or dithering. PNG files encoded by rdopng typically range between roughly 2.5-7bpp, depending on the options used (and how much time and patience you have).

You can download a pre-built Windows binary [here](https://github.com/richgel999/rdopng/releases/tag/1.0.7). You may need to install the [VS 2022 runtime redistributable from Microsoft](https://docs.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist?view=msvc-170).

### Building

You'll need [cmake](https://cmake.org/). There are no other dependencies.

Linux (gcc/clang): 

```
cmake .
make
```

Windows (tested with Visual Studio 2022):

```
cmake .
rdopng.sln
```

### Instructions

Encodes a .PNG/.BMP/.TGA/.JPG file to "./file_rdo.png":

```
rdopng file.png
```

Encodes smaller files but will be 2x slower:

```
rdopng -two_pass file.png
```

Encodes at lower than default quality (which is 300), but writes smaller files:

```
rdopng -lambda 500 file.png
```

Significantly lower quality (which increases artifacts), using a higher than default parsing level to compensate for artifacts:

```
rdopng -level 3 -lambda 1000 file.png
```

Enable debug output and write output to z.png:

```
rdopng -debug file.png -output z.png
```

Level ranges from 0-29. Levels 0-9 use up to 4 pixel long matches, levels 10-17 use up to 6 pixel long matches, and 18-23 use up to 6 or 12 pixel long matches. Levels 24-29 use exhaustive matching and are beyond impractical except on tiny images. 

The higher the level within a match length category, the slower the encoder. Higher match length categories are needed for the higher lambdas/lower bitrates. At near-lossless settings (lower than approximately lambda 300), the smaller/less aggressive parsing levels are usually fine. At higher lambdas/lower bitrates the higher levels are needed to avoid artifacts. To get below roughly 3-4bpp you'll need to use high lambdas, two pass mode, and very slow parsing levels.

-lambda is the quality slider. Useful lambda values are roughly 1-20000, but values beyond approximately 500-1000 (depending on the image) will require fiddling with the level to compensate for artifacts. Higher levels are extremely slow because the current tool is single threaded.
